// MainWindow: YouTube search, queue and playback
#include "MainWindow_p.h"
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <memory>

// any song type is loaded: MIDI (playing or paused), video, MP3 or YouTube
bool MainWindow::isSongPlaying()
{
    return !player->isPlayerStopped() || player->isPlayerPaused() || videoActive || ytActive || mp3Stream != 0
           || !ytPrepId.isEmpty();     // a YouTube song getting ready to play (startYtPrep)
}

void MainWindow::showYouTubeSearch()
{
    YtItem seed;        // "next songs" follow the YouTube song that is playing
    if (ytActive) {
        seed.id = playingSong.path();
        seed.title = playingSong.name();
        seed.channel = playingSong.artist();
    }
    YouTubeSearchDialog dlg(dialogParent(), seed);
    {
        QString dir = QDir::fromNativeSeparators(db->videoPath());
        while (dir.endsWith('/'))
            dir.chop(1);
        dlg.setLocalVideoDir(dir + "/YouTube");
    }
    connect(&dlg, &YouTubeSearchDialog::playNow, this, [this](const YtItem &it) { addYouTubeToQueue(it, true); });
    connect(&dlg, &YouTubeSearchDialog::addToQueue, this, [this](const YtItem &it) { addYouTubeToQueue(it, false); });
    connect(&dlg, &YouTubeSearchDialog::downloadRequested, this, [this](const QString &url) { showYouTubeDownload(url); });
    dlg.exec();
    setFocus();
}

void MainWindow::addYouTubeToQueue(const YtItem &item, bool playNow)
{
    if (item.id.startsWith("file:")) {
        // a downloaded clip: play it as the video song it is in the library (AI vocal cut, key...)
        const QString file = item.id.mid(5);
        QString rel = QDir::fromNativeSeparators(file);
        QString base = QDir::fromNativeSeparators(db->videoPath());
        while (base.endsWith('/'))
            base.chop(1);
        if (rel.startsWith(base, Qt::CaseInsensitive))
            rel = rel.mid(base.length());
        // Look the song up by its file, not with db->search(): that follows the search mode picked
        // in the search box (name, artist...) and missed the code when the mode was not "code".
        Song found;
        if (!db->songByPath("VIDEO", rel, &found)) {
            importSongs({ file }, true);
            if (!db->songByPath("VIDEO", rel, &found)) {
                QMessageBox::warning(dialogParent(), tr("เล่นวิดีโอไม่ได้"), tr("ไม่พบเพลงนี้ในคลังเพลง: ") + file);
                return;
            }
        }
        addToPlaylist(&found);
        if (!isSongPlaying() && (playNow || auto_playnext))
            play(playlist.count() - 1);
        else {
            showPlaylist();
            timer2->start(playlist_timeout);
        }
        return;
    }
    Song s;
    s.setId("YT");
    s.setName(item.title);
    s.setArtist(item.channel);
    s.setSongType("YOUTUBE");
    s.setPath(item.id);
    addToPlaylist(&s);

    // while a song plays, "play" only queues the song (it starts after the current one)
    if (!isSongPlaying() && (playNow || auto_playnext))
        play(playlist.count() - 1);
    else {
        showPlaylist();
        timer2->start(playlist_timeout);        // the queue shows briefly, then hides again
    }
}

// ---- AI vocal cut for YouTube songs: played from a copy on disk -----------------------------------
// Online, the AI needs the sound before it can separate it, so the picture waits 6-9 s. A copy on
// disk is separated ahead of the play position instead (AiStems.h): no waiting picture, no
// stutter. The first time, the song plays online while the copy downloads, then goes on from the
// copy. Copies are remembered by video id (youtube-local.json) and play at once next time.

static QString ytLocalMapFile()
{
    return QFileInfo(YtTools::toolsDir()).absolutePath() + "/youtube-local.json";
}

static QString ytLocalCopy(const QString &id)
{
    QFile f(ytLocalMapFile());
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QString p = QJsonDocument::fromJson(f.readAll()).object().value(id).toString();
    return (!p.isEmpty() && QFile::exists(p)) ? p : QString();
}

static void ytRememberCopy(const QString &id, const QString &file)
{
    QJsonObject o;
    {
        QFile f(ytLocalMapFile());
        if (f.open(QIODevice::ReadOnly))
            o = QJsonDocument::fromJson(f.readAll()).object();
    }
    o[id] = file;
    QFile f(ytLocalMapFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString MainWindow::ytCopyOf(const QString &id)
{
    return id.isEmpty() ? QString() : ytLocalCopy(id);
}

// the copy is being deleted: the song plays online (and downloads again) next time
void MainWindow::ytForgetCopy(const QString &id)
{
    QJsonObject o;
    {
        QFile f(ytLocalMapFile());
        if (!f.open(QIODevice::ReadOnly))
            return;
        o = QJsonDocument::fromJson(f.readAll()).object();
    }
    if (!o.contains(id))
        return;
    o.remove(id);
    QFile f(ytLocalMapFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void MainWindow::cancelYtFetch()
{
    if (!ytFetch)
        return;
    ytFetch->disconnect(this);
    ytFetch->kill();
    ytFetch->waitForFinished(2000);
    ytFetch->deleteLater();
    ytFetch = nullptr;
    if (ytFetchConverting && !ytFetchTemp.isEmpty()) {
        QFile::remove(YtTools::convertedName(ytFetchTemp));
        QFile::remove(ytFetchTemp);
    }
    ytFetchConverting = false;
    ytFetchTemp.clear();
}

// Play a downloaded copy as a video song: the library's entry when it has one, otherwise the YouTube
// song's own name (not imported here: adding to a large library froze the window for seconds).
// atMs > 0: from there (switching from the online song), paused if it was.
void MainWindow::playLocalCopy(const QString &file, qint64 atMs, bool paused)
{
    QString rel = QDir::fromNativeSeparators(file);
    QString base = QDir::fromNativeSeparators(db->videoPath());
    while (base.endsWith('/'))
        base.chop(1);
    if (rel.startsWith(base, Qt::CaseInsensitive))
        rel = rel.mid(base.length());
    const int keepKey = playingSong.transpose();    // the online song's key goes on with the copy
    Song found;
    if (db->songByPath("VIDEO", rel, &found)) {
        playingSong = found;
    } else {
        playingSong.setSongType("VIDEO");
        playingSong.setPath(rel);
    }
    if (keepKey != 0)
        playingSong.setTranspose(keepKey);
    const QString path = playingSong.path();
    playVideo(file);
    if (atMs <= 1000 && !paused)
        return;
    // Go on from where the online song was (the sound follows the picture: see positionChanged).
    // A jump asked for while the file is still opening can be ignored, so it is asked again until
    // the picture is really there: when the file becomes seekable, and a few times after.
    auto pausedDone = std::make_shared<bool>(false);
    auto jump = [this, pausedDone, atMs, paused, path]() {
        if (!videoActive || playingSong.path() != path || !videoPlayer->isSeekable())
            return;
        if (atMs > 1000 && videoPlayer->position() < atMs - 2000)
            videoPlayer->setPosition(atMs);
        if (paused && !*pausedDone) {
            *pausedDone = true;
            pause();
        }
    };
    jump();
    auto once = std::make_shared<QMetaObject::Connection>();
    *once = connect(videoPlayer, &QMediaPlayer::seekableChanged, this, [once, jump](bool can) {
        if (!can)
            return;
        QObject::disconnect(*once);
        jump();
    });
    for (int ms : { 200, 500, 1000, 1500, 2500, 4000 })
        QTimer::singleShot(ms, this, jump);
}

// With the AI vocal cut, a YouTube song is played from a copy on disk. A copy already there: true,
// it plays. Otherwise the song plays online at once (not cut yet, no waiting picture) while the
// copy downloads; when it is there, playing goes on from the copy at the same place, with the AI.
// The cut button shows the progress; a cut pressed meanwhile is done once the AI is ready.
bool MainWindow::playYouTubeDownloaded()
{
    ytFetchOnline = false;
    if (!settings->value("YtCutAi", false).toBool() || !settings->value("YtDownloadFirst", true).toBool())
        return false;
    const QString id = playingSong.path();
    if (id.isEmpty())
        return false;
    if (idlePlaying) {                          // idle music: a copy if there is one, never a download
        const QString copy = ytLocalCopy(id);
        if (copy.isEmpty())
            return false;
        playLocalCopy(copy);
        return true;
    }
    const QString local = ytLocalCopy(id);
    // Cut wanted: wait until the AI can cut from the first note (no online start, no switch later)
    const bool wantCut = ui->chMix->isMuteVoice() && !ytSkipPrepOnce;
    ytSkipPrepOnce = false;
    if (wantCut && (!local.isEmpty() || (!YtTools::ytdlpPath().isEmpty() && !YtTools::ffmpegPath().isEmpty()))) {
        startYtPrep(id, local);
        if (local.isEmpty() && ytFetch && ytFetchId == id && !ytFetchConverting
                && ytFetchHeight > ytCopyHeight(true) && ytFetchPct < 40) {
            // downloading for the queue with a big picture and not half way: the song waits
            // now, a small picture is there sooner (22:49, 2026-11-14: 36 s of waiting for 720p)
            uiLog(QString("YouTube %1: the %2p download is far from ready, starts again smaller").arg(id).arg(ytFetchHeight));
            cancelYtFetch();
        }
        if (local.isEmpty() && !(ytFetch && ytFetchId == id)) {
            cancelYtFetch();                    // (a queued song's download goes on later)
            startYtFetch(id);
        }
        return true;
    }
    if (!local.isEmpty()) {
        playLocalCopy(local);
        return true;
    }
    if (YtTools::ytdlpPath().isEmpty() || YtTools::ffmpegPath().isEmpty())
        return false;                           // tools not downloaded yet: online only
    if (ytFetch && ytFetchId == id) {           // already downloading this one
        ytFetchOnline = true;
        return false;
    }
    cancelYtFetch();                            // the previous song's download (if any) is not needed now
    startYtFetch(id);
    ytFetchOnline = true;
    ui->detail->setDetail(tr("เล่นออนไลน์ไปก่อน"), tr("กำลังเตรียมตัดเสียงร้องด้วย AI (ดูที่ปุ่ม \"ตัดเสียงร้อง\")"));
    ui->detail->show();
    detailTimer->start(4000);
    return false;                               // plays online now
}

// 720p needs ~30 MB for a 4 min song, 480p ~17 MB, 360p ~11 MB. A song that waits: the biggest
// picture that downloads in ~15 s (then ~10 s more for the sound conversion and the AI). A queued
// song with time to spare: the biggest in ~40 s.
int MainWindow::ytCopyHeight(bool waiting)
{
    const double kb = settings->value("YtNetSpeedKB", 0.0).toDouble();
    if (kb <= 0)
        return waiting ? 360 : 480;             // not measured yet
    if (waiting)
        return kb >= 2000 ? 720 : kb >= 1100 ? 480 : 360;
    return kb >= 800 ? 720 : kb >= 350 ? 480 : 360;
}

void MainWindow::ytRememberSpeed(double kbps)
{
    if (kbps <= 0)
        return;
    const double old = settings->value("YtNetSpeedKB", 0.0).toDouble();
    const double now = old > 0 ? 0.5 * old + 0.5 * kbps : kbps;
    settings->setValue("YtNetSpeedKB", int(now));
    uiLog(QString("YouTube copy: downloaded at %1 KB/s, remembered %2 KB/s -> next copy %3p")
          .arg(int(kbps)).arg(int(now)).arg(ytCopyHeight(true)));
}

void MainWindow::startYtFetch(const QString &id, bool waiting)
{
    QString dir = QDir::fromNativeSeparators(db->videoPath());
    while (dir.endsWith('/'))
        dir.chop(1);
    dir += "/YouTube";
    QDir().mkpath(dir);

    QString printFile;
    {
        QTemporaryFile tf(QDir::tempPath() + "/pthk-ytplay-XXXXXX.txt");
        tf.setAutoRemove(false);
        tf.open();
        printFile = tf.fileName();
    }
    ytFetchId = id;

    QProcess *p = new QProcess(this);
    ytFetch = p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    p->setProcessEnvironment(env);
    p->setProcessChannelMode(QProcess::MergedChannels);
    ytFetchPct = 0;
    ytFetchConverting = false;
    ytFetchError.clear();
    ytFetchSpeedSum = 0;
    ytFetchSpeedN = 0;
    auto fetchClock = std::make_shared<QElapsedTimer>();
    fetchClock->start();
    connect(p, &QProcess::readyRead, this, [this, p]() {
        // progress for the cut button: the picture is fetched first (~90 %), then the sound
        static const QRegularExpression pct("\\[PCT\\]\\s*(\\d+(?:\\.\\d+)?)%");
        static const QRegularExpression spd("\\[SPD\\]\\s*(\\d+(?:\\.\\d+)?)");
        static int part = 0, last = 0;
        if (ytFetchPct == 0)
            part = last = 0;
        while (p->canReadLine()) {
            const QString line = QString::fromUtf8(p->readLine()).trimmed();
            if (line.startsWith("ERROR"))
                ytFetchError = line.left(300);
            const QRegularExpressionMatch m = pct.match(line);
            if (!m.hasMatch())
                continue;
            const QRegularExpressionMatch sm = spd.match(line);
            if (sm.hasMatch() && sm.captured(1).toDouble() > 0) {
                ytFetchSpeedSum += sm.captured(1).toDouble();
                ++ytFetchSpeedN;
            }
            const int v = int(m.captured(1).toDouble());
            if (v + 50 < last)
                ++part;
            last = v;
            ytFetchPct = qBound(1, part == 0 ? v * 75 / 100 : 75 + v * 5 / 100, 80);
        }
    });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p, id, printFile, fetchClock](int code, QProcess::ExitStatus st) {
        if (ytFetch == p) {
            ytFetch = nullptr;
            ytFetchId.clear();
        }
        p->deleteLater();
        const double sampled = ytFetchSpeedN >= 3 ? ytFetchSpeedSum / ytFetchSpeedN / 1024.0 : 0.0;
        QString file;
        QFile pf(printFile);
        if (pf.open(QIODevice::ReadOnly)) {
            const QStringList lines = QString::fromUtf8(pf.readAll()).split('\n', Qt::SkipEmptyParts);
            if (!lines.isEmpty())
                file = lines.last().trimmed();
            pf.close();
        }
        QFile::remove(printFile);
        if (!(st == QProcess::NormalExit && code == 0 && !file.isEmpty() && QFile::exists(file))) {
            if (ytFetchSpeedN >= 10)
                ytRememberSpeed(sampled);       // a slow line is learnt from a failed download too
            ytPrefetchFailed.insert(id);        // not tried again for the queue
            ytFetchGaveUp(QString("YouTube download for the AI cut failed (exit %1) %2").arg(code).arg(ytFetchError));
            return;                             // it goes on online with the cut of the online sound
        }
        // the speed of the line: the samples, or at least the whole file over the whole time (a
        // small copy on a fast line is there before a few samples are)
        ytRememberSpeed((std::max)(sampled, QFileInfo(file).size() / 1024.0
                                            / (std::max)(1.0, fetchClock->elapsed() / 1000.0)));
        // the sound into AAC 256k (Windows plays it), its progress on the cut button (80..99 %)
        ytFetchPct = 80;
        ytFetchConverting = true;
        ytFetchTemp = file;
        QElapsedTimer *convClock = new QElapsedTimer;
        convClock->start();
        QProcess *cv = YtTools::convertForWindows(this, file, [this](int pct) {
            ytFetchPct = qBound(80, 80 + pct * 19 / 100, 99);
        }, [this, id, convClock](const QString &mp4) {
            const qint64 convMs = convClock->elapsed();
            delete convClock;
            ytFetch = nullptr;
            ytFetchId.clear();
            ytFetchConverting = false;
            ytFetchTemp.clear();
            if (mp4.isEmpty()) {
                ytFetchGaveUp("YouTube copy for the AI cut: the sound could not be converted");
                return;                         // it goes on online with the cut of the online sound
            }
            uiLog(QString("YouTube copy for the AI cut ready: sound converted in %1 ms").arg(convMs));
            ytCopyReady(id, mp4);
        });
        if (cv) {
            ytFetch = cv;                       // cancelYtFetch stops the conversion too
            ytFetchId = id;
        }
    });
    const QString ffmpegDir = QDir::toNativeSeparators(QFileInfo(YtTools::ffmpegPath()).absolutePath());
    // H.264 + AAC (Windows plays it) with YouTube's best sound; 720p is enough and quick to fetch
    const int height = ytCopyHeight(waiting);
    ytFetchHeight = height;
    uiLog(QString("YouTube copy %1: internet %2 KB/s, %3 -> %4p").arg(id)
          .arg(settings->value("YtNetSpeedKB", 0).toInt())
          .arg(waiting ? "the song waits" : "queued, time to spare").arg(height));
    p->start(YtTools::ytdlpPath(), YtTools::formatArgs(height) + QStringList{
        "--no-playlist", "--windows-filenames", "--no-mtime", "--newline",
        "--ffmpeg-location", ffmpegDir,
        "--progress-template", "download:[PCT]%(progress._percent_str)s [SPD]%(progress.speed)s",
        "--print-to-file", "after_move:filepath", QDir::toNativeSeparators(printFile),
        "-o", QDir::toNativeSeparators(dir) + "\\%(title).120s.%(ext)s",
        "https://www.youtube.com/watch?v=" + id
    });
}

// The copy of the YouTube song is on disk (converted): remembered, and when that song still plays
// online, playing goes on from the copy at the same place, with the AI.
void MainWindow::ytCopyReady(const QString &id, const QString &file)
{
    ytRememberCopy(id, file);
    if (!ytPrepId.isEmpty() && ytPrepId == id) {        // the song waiting to play: now its AI
        ytPrepFile = file;
        ensureAiStems();
        aiStems->setFocus(file, 0.0);
        updateAiStemsBackground();
        ytPrepTick();
        return;
    }
#ifdef PTHK_WEBENGINE
    if (ytActive && playingSong.path() == id) {
        // The song goes on online without a break (switching at once was a jerk, also with the cut
        // off). The AI separates the copy from where the song is; ytCopyPendingTick moves playing to
        // the copy once the cut is on and the AI is ready there.
        ytCopyPending = file;
        ensureAiStems();
        aiStems->setFocus(file, ui->sliderPosition->value() / 1000.0);
        ytCopyPendingTick();
        return;
    }
#endif
    enqueueAiStems(file);                       // another song plays: separated later, in the background
}

// The copy could not be made: the song goes on online, and its own (online) vocal cut works again.
// Before, the "waiting for the copy" state stayed: no cut at all for the rest of the song, ring stuck.
void MainWindow::ytFetchGaveUp(const QString &why)
{
    if (!ytPrepId.isEmpty() && ytPrepFile.isEmpty()) {  // it was the waiting song's download
        ytPrepGiveUp(why);
        return;
    }
    uiLog(why);
    if (!ytActive)
        return;
    ytFetchOnline = false;
    ytFetchConverting = false;
#ifdef KM_HAVE_PROCLOOP
    applyYtVocalCut();
#endif
    updateCutButton();
}

// percent: how much of the copy the AI has made (0..100)
bool MainWindow::ytCopyAiReadyAt(qint64 ms, int *percent)
{
    if (ytCopyPending.isEmpty() || !aiStems)
        return false;
    const std::shared_ptr<StemTrack> t = aiStems->track(ytCopyPending);
    if (!t)
        return false;
    if (percent)
        *percent = t->complete.load() ? 100
                 : (t->ready.load() && t->nWin > 0) ? qBound(0, t->doneCount.load() * 100 / t->nWin, 99) : 0;
    const long long f = (long long)(qMax<qint64>(0, ms) / 1000.0 * AiVocalEngine::SR);
    // a little more than the switch takes (~1 s) and a few seconds after it
    return t->complete.load() || (t->frameReady(f) && t->frameReady(f + 5LL * AiVocalEngine::SR));
}

// called with the cut button (status timer): keep the AI on the play position; switch when wanted
void MainWindow::ytCopyPendingTick()
{
#ifdef PTHK_WEBENGINE
    if (ytCopyPending.isEmpty())
        return;
    if (!ytActive || !QFile::exists(ytCopyPending)) {
        ytCopyPending.clear();
        return;
    }
    const qint64 at = ui->sliderPosition->value();
    if (aiStems)
        aiStems->setFocus(ytCopyPending, at / 1000.0);
    if (!ui->chMix->isMuteVoice() || keepIdleVocal() || !ytCopyAiReadyAt(at, nullptr))
        return;
    // the cut is wanted and the AI is ready here: on from the copy, cut from the first moment
    const QString file = ytCopyPending;
    ytCopyPending.clear();
    const bool paused = ytPlayer && ytPlayer->isPaused();
    uiLog(QString("YouTube: on from the copy at %1 ms with the AI cut").arg(at));
    stop();
    playLocalCopy(file, at, paused);
    ui->detail->setDetail(tr("ตัดเสียงร้องด้วย AI แล้ว"), tr("เล่นต่อจากไฟล์ในเครื่อง"));
    ui->detail->show();
    detailTimer->start(4000);
#endif
}

// YouTube songs play online with YouTube's own embedded player, in place of the video
void MainWindow::playYouTube()
{
    if (playYouTubeDownloaded())
        return;
#ifdef PTHK_WEBENGINE
    if (!ytPlayer) {
        ytPlayer = new YouTubePlayer(this);
        // Both screens show copies of the YouTube picture (30 a second) painted by videoWidget, which
        // covers the live player. Shown directly, the player's GPU picture vanished from the screen
        // for seconds whenever a menu opened over it; the copies are not affected.
        ytMirrorTimer = new QTimer(this);
        ytMirrorTimer->setTimerType(Qt::PreciseTimer);
        ytMirrorTimer->setInterval(33); // 30 FPS matches YouTube native frame rate for judder-free playback
        connect(ytMirrorTimer, &QTimer::timeout, this, [this]() {
            if (!ytActive || !ytPlayer || !ytPlayer->isVisible() || !ytPlayer->isStarted())
                return;
#ifdef KM_HAVE_PROCLOOP
            if (!ytInputGateOpen) {
                // The new clip is really playing now: start taking its sound and its picture at
                // the same moment, so neither carries anything of the previous song.
                ytInputGateOpen = true;
                if (timingLogsOn()) {
                    QFile f(QCoreApplication::applicationDirPath() + "/yt-video.log");
                    if (f.open(QIODevice::Append | QIODevice::Text))
                        f.write(QString("%1 sound gate open: the new clip %2 really plays\n")
                                    .arg(QTime::currentTime().toString("hh:mm:ss.zzz"), playingSong.path()).toUtf8());
                }
                if (ytAudio) {
                    if (ytAudio->aiMode() && aiEngine)
                        aiEngine->reset();
                    ytAudio->setInputOpen(true);
                }
                delayedFrames.clear();
                frameClock.invalidate();
            }
#endif

            static bool busy = false;
            // --- measurement (yt-video.log next to the exe, one line every 2 s) ---
            static QElapsedTimer statClock, tickClock;
            static int stTicks = 0, stShown = 0, stSkipped = 0, stAiTicks = 0;
            static double stGrabSum = 0, stGrabMax = 0, stTickGapMax = 0, stShowGapMax = 0;
            static qint64 lastShownMs = -1;
            if (!statClock.isValid()) { statClock.start(); tickClock.start(); }
            {
                const double gap = tickClock.nsecsElapsed() / 1e6;
                tickClock.restart();
                stTickGapMax = qMax(stTickGapMax, gap);
            }
            if (busy) {
                ++stSkipped;
                return;     // Drop frame if previous grab/render is still active to prevent timer event pile-up
            }
            busy = true;
            ++stTicks;
#ifdef KM_HAVE_PROCLOOP
            if (aiEngine && aiEngine->computing()) ++stAiTicks;
#endif
            auto markShown = [&]() {
                ++stShown;
                const qint64 nowMs = statClock.elapsed();
                if (lastShownMs >= 0) stShowGapMax = qMax(stShowGapMax, double(nowMs - lastShownMs));
                lastShownMs = nowMs;
            };

            QElapsedTimer grabT; grabT.start();
            QPixmap pm = ytPlayer->grab();
            const double grabMs = grabT.nsecsElapsed() / 1e6;
            stGrabSum += grabMs;
            stGrabMax = qMax(stGrabMax, grabMs);
            if (statClock.elapsed() >= 2000) {
                double dly = 0;
#ifdef KM_HAVE_PROCLOOP
                if (ytAudio) dly = ytAudio->delaySeconds();
#endif
                QFile f(QCoreApplication::applicationDirPath() + "/yt-video.log");
                if (timingLogsOn() && f.open(QIODevice::Append | QIODevice::Text)) {
                    f.write(QString("%1 grabs %2 shown %3 skipped %4 | grab avg %5 max %6 ms | worst gap: timer %7 ms, picture %8 ms | AI busy %9% | delay %10 s, queued %11 | %12x%13")
                        .arg(QTime::currentTime().toString("hh:mm:ss.zzz")).arg(stTicks).arg(stShown).arg(stSkipped)
                        .arg(stTicks ? stGrabSum / stTicks : 0, 0, 'f', 1).arg(stGrabMax, 0, 'f', 1)
                        .arg(stTickGapMax, 0, 'f', 0).arg(stShowGapMax, 0, 'f', 0)
                        .arg(stTicks ? 100 * stAiTicks / stTicks : 0).arg(dly, 0, 'f', 1)
                        .arg(int(delayedFrames.size())).arg(pm.width()).arg(pm.height()).toUtf8() + QByteArray(1, char(10)));
                }
#ifdef KM_HAVE_PROCLOOP
                // The AI cannot keep up with the clip (busy all the time for ~10 s): its delay grows
                // until the picture stops. Step down once: first the graphics card (same quality),
                // then the lighter model, then the fast (non-AI) cut.
                const int busyPct = stTicks ? 100 * stAiTicks / stTicks : 0;
                if (ytAudio && ytAudio->aiMode() && busyPct >= 97)
                    ++ytAiOverload;
                else
                    ytAiOverload = 0;
                if (ytAiOverload >= 5) {
                    ytAiOverload = -15;         // give the change ~30 s before judging again
                    QString what;
                    if (settings->value("YtAiDevice", 0).toInt() == 1) {
                        settings->setValue("YtAiDevice", 0);
                        AiVocalEngine::setDevicePreference(0);
                        aiEngine->reloadModel();
                        what = tr("ให้ AI ใช้การ์ดจอช่วย (คุณภาพเท่าเดิม)");
                    } else if (settings->value("YtAiModel", "light").toString() != "light") {
                        settings->setValue("YtAiModel", "light");
                        what = tr("เปลี่ยนเป็น AI รุ่นเบา");
                    } else {
                        settings->setValue("YtCutAi", false);
                        what = tr("เปลี่ยนเป็นตัดเสียงแบบเร็ว (ไม่ใช้ AI)");
                    }
                    if (aiEngine) aiEngine->reset();
                    delayedFrames.clear();
                    applyYtVocalCut();
                    ui->detail->setDetail(tr("AI ตัดเสียงร้องทำงานไม่ทัน"), what + tr(" ภาพจะกลับมาลื่น"));
                    ui->detail->show();
                    detailTimer->start(6000);
                    uiLog("AI overloaded on YouTube: " + what);
                }
#endif
                stTicks = stShown = stSkipped = stAiTicks = 0;
                stGrabSum = stGrabMax = stTickGapMax = stShowGapMax = 0;
                statClock.restart();
                lastShownMs = -1;
            }
            if (!pm.isNull()) {
#ifdef KM_HAVE_PROCLOOP
                static bool delayedPrimed = false;
                static bool firstFrameShown = false;
                // The measured AI delay jitters a little on every audio block. Following it frame by
                // frame made some ticks show no picture and others skip one (judder). Keep the
                // picture delay steady: follow slowly (2 ms per frame), jump only on a big change.
                static double steadyD = 0;
                const double rawD = (ytAudio && settings->value("YtDelayPicture", true).toBool()) ? ytAudio->delaySeconds() : 0.0;
                if (rawD <= 0.15)
                    steadyD = 0;
                else if (steadyD <= 0 || delayedFrames.empty() || std::fabs(rawD - steadyD) > 0.5)
                    steadyD = rawD;
                else
                    steadyD += qBound(-0.002, rawD - steadyD, 0.002);
                const double d = steadyD;
                if (d > 0.15) {
                    if (!frameClock.isValid())
                        frameClock.start();
                    const qint64 now = frameClock.elapsed();

                    // ถ้าคิวว่างเปล่า แปลว่าเพิ่งเริ่มเพลงใหม่ ให้รีเซ็ตสถานะการ Prime อัตโนมัติ
                    if (delayedFrames.empty()) {
                        delayedPrimed = false;
                        firstFrameShown = false;
                    }

                    delayedFrames.push_back({ now, pm });

                    const qint64 target = now - qint64(d * 1000);
                    QPixmap show;
                    while (!delayedFrames.empty() && delayedFrames.front().t <= target) {
                        show = delayedFrames.front().img;
                        delayedFrames.pop_front();
                    }
                    while (delayedFrames.size() > 480)   // ~16 s at 30 fps (CPU-run AI has a longer delay)
                        delayedFrames.pop_front();

                    if (!show.isNull()) {
                        delayedPrimed = true;
                        const QPixmap &showPm = show;
                        videoWidget->setPixmap(showPm);
                        markShown();
                        if (secondLyr != nullptr && secondMonitor)
                            secondMonitor->setVideoPixmap(showPm);
                    } else if (!delayedPrimed) {
                        // ช่วงที่กำลังเริ่มบัฟเฟอร์ แสดงเฉพาะภาพแรก (Frame 0) ค้างไว้
                        // เพื่อไม่ให้ภาพวิดีโอเล่นล่วงหน้าไปก่อนแล้วกระโดดถอยหลังกลับมาเล่นซ้ำ
                        if (!firstFrameShown) {
                            firstFrameShown = true;
                            videoWidget->setPixmap(pm);
                            if (secondLyr != nullptr && secondMonitor)
                                secondMonitor->setVideoPixmap(pm);
                        }
                    }
                } else {
                    delayedPrimed = false;
                    if (!delayedFrames.empty())
                        delayedFrames.clear();

                    // FAST PATH: Zero CPU conversions! Direct GPU hardware texture blit via QPixmap
                    videoWidget->setPixmap(pm);
                    markShown();
                    if (secondLyr != nullptr && secondMonitor)
                        secondMonitor->setVideoPixmap(pm);
                }
#else
                videoWidget->setPixmap(pm);
                if (secondLyr != nullptr && secondMonitor)
                    secondMonitor->setVideoPixmap(pm);
#endif
            }
            busy = false;
        });
        ytPlayer->stackUnder(videoWidget);  // under the copies; search box, queue and lyrics stay on top
        connect(ytPlayer, &YouTubePlayer::durationChanged, this, [this](qint64 d) {
            if (!ytActive)
                return;
            ui->sliderPosition->setMaximum(int(d));
            onPlayerDurationMSChanged(d);
            YtHistory::setDuration(playingSong.path(), int(d / 1000));
        });
        connect(ytPlayer, &YouTubePlayer::positionChanged, this, [this](qint64 p) {
            if (!ytActive)
                return;
            if (!ui->sliderPosition->isSliderDown())
                ui->sliderPosition->setValue(int(p));
            onPlayerPositionMSChanged(p);
            updateYtAudio();

            // Medley Auto-Cut Outro for YouTube:
            if (songInMedley() && player->medleyAudioAutoCue() && !endingSong) {
                const qint64 totalD = ytPlayer->durationMs();
                const qint64 remainingMs = (totalD > 0) ? (totalD - p) : 999999;
                if (remainingMs <= 30000 && remainingMs > 1500) {
#ifdef KM_HAVE_PROCLOOP
                    if (ytAudio && ytAudio->running()) {
                        const float curDb = ytAudio->currentLevelDb();
                        const float threshDb = float(player->medleyAudioThresholdDb());
                        if (curDb <= threshDb) {
                            ytSilenceCount++;
                            // 250ms interval * 6 = 1.5 seconds of sustained silence
                            if (ytSilenceCount >= 6) {
                                ytSilenceCount = 0;
                                endSongSmoothly();
                            }
                        } else {
                            ytSilenceCount = 0;
                        }
                    }
#endif
                } else {
                    ytSilenceCount = 0;
                }
            }
        });
        connect(ytPlayer, &YouTubePlayer::ended, this, [this]() {
            if (!ytActive)
                return;
            // with the AI the sound (and picture) are a few seconds behind: let them finish first
            int lateMs = 0;
#ifdef KM_HAVE_PROCLOOP
            if (ytAudio)
                lateMs = int(ytAudio->delaySeconds() * 1000);
#endif
            if (lateMs <= 150) {
                songFinished();
                return;
            }
            const QString clip = playingSong.path();
            QTimer::singleShot(lateMs, this, [this, clip]() {
                if (ytActive && playingSong.path() == clip)     // not stopped / changed meanwhile
                    songFinished();
            });
        });
        connect(ytPlayer, &YouTubePlayer::failed, this, [this](const QString &msg, bool canDownload) {
            if (!ytActive)
                return;
            const QString url = "https://www.youtube.com/watch?v=" + playingSong.path();
            const QString vid = playingSong.path();
            const bool wasIdle = idlePlaying;
            stop();
            if (wasIdle) {
                // idle music: no question on the screen of an unattended shop; the next song instead.
                // A clip that can never play here (not allowed outside YouTube / removed) is not picked again.
                if (idleSmart && (canDownload || msg.startsWith(tr("ไม่พบคลิป"))))
                    idleSmart->markBad(vid);
                if (msg.contains(tr("อินเทอร์เน็ต"))) {     // the clip never loaded: no internet now
                    ytOnline = false;
                    ytOnlineClock.restart();                // checked again in 20 s
                    uiLog("YouTube clip did not load: idle music plays library songs");
                }
                idlePlaying = true;
                songUnplayable(msg);
                return;
            }
            if (canDownload) {
                if (QMessageBox::question(dialogParent(), tr("เล่นจาก YouTube ไม่ได้"),
                        msg + tr("\n\nดาวน์โหลดเก็บไว้แล้วเล่นแทนไหม?")) == QMessageBox::Yes)
                    showYouTubeDownload(url);
            } else {
                QMessageBox::warning(dialogParent(), tr("เล่นจาก YouTube ไม่ได้"), msg);
            }
        });
    }

    ytActive = true;
    updateAiStemsBackground();
    ytSilenceCount = 0;
#ifdef PTHK_WEBENGINE
    if (ytPlayer && btnSubtitles)
        ytPlayer->setSubtitlesVisible(btnSubtitles->isChecked());
#endif
    lyrWidget->hide();
    lyrWidget->reset();
    if (secondLyr != nullptr) {
        secondLyr->hide();
        secondLyr->reset();
    }
    if (lyricsTimer && lyricsTimer->isActive())
        lyricsTimer->stop();
#ifdef KM_HAVE_PROCLOOP
    delayedFrames.clear();
    if (frameClock.isValid())
        frameClock.restart();
    if (ytAudio && ytAudio->aiMode() && aiEngine)
        aiEngine->reset();
#endif
    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->chMix->hide();
    ui->expandChMix->hide();

    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();
    ui->songDetail->show();
    timer2->start(songDetail_timeout);

    ui->sliderPosition->setValue(0);
    bgWidget->setVideoPaused(true);
    ytPlayer->resize(854, 480); // 854x480 reduces GPU readback bandwidth by >55%
    ytPlayer->show();
    videoWidget->clear();
    videoWidget->resize(ui->centralWidget->size());
    videoWidget->show();
    if (secondLyr != nullptr)
        secondMonitor->setVideoVisible(true);
    ytMirrorTimer->start();
    if (!idlePlaying) {     // remembered for the "sung lately / most" lists of the YouTube search
        YtItem it;
        it.id = playingSong.path();
        it.title = playingSong.name();
        it.channel = playingSong.artist();
        YtHistory::record(it);
    }
#ifdef KM_HAVE_PROCLOOP
    if (ytAudio) {
        ytAudio->setPitch(rememberedKey());     // the song's key (queue or remembered)
        ytAudio->setInputOpen(false);   // reopened when the new clip really plays (mirror timer)
        if (ytAudio->aiMode())
            aiEngine->reset();
    }
    ytInputGateOpen = false;
    delayedFrames.clear();
    frameClock.invalidate();
#endif
    ytPlayer->playVideo(playingSong.path(), ytAudioLive() ? 100 : effectiveVolume());
    dmxLights->setPlaying(true);
#else
    QMessageBox::warning(dialogParent(), tr("YouTube"), tr("เวอร์ชันนี้เล่น YouTube ออนไลน์ไม่ได้"));
#endif
}

// ---- "wait until ready" (startYtPrep) and the queue (ytQueueTick) ---------------------------------

// the AI sound of a file is there for its first 10 s (or all of it)
bool MainWindow::aiReadyAtStart(const QString &file)
{
    if (file.isEmpty())
        return false;
    if (AiStemMaker::hasStems(file))
        return true;
    ensureAiStems();
    const std::shared_ptr<StemTrack> t = aiStems->track(file);
    return t && (t->complete.load() || (t->frameReady(0) && t->frameReady(10LL * AiVocalEngine::SR)));
}

void MainWindow::startYtPrep(const QString &id, const QString &file)
{
    ytPrepId = id;
    ytPrepFile = file;
    ytPrepPct = 0;
    ytPrepClock.restart();
    ensureAiStems();
    if (!file.isEmpty())
        aiStems->setFocus(file, 0.0);
    else
        aiStems->clearFocus();      // the song before is not worked on while this one downloads
    updateAiStemsBackground();      // nor anything in the background (see there)
    uiLog(QString("YouTube %1: waits until the AI can cut from the start (%2)")
          .arg(id, file.isEmpty() ? QString("downloading") : QString("copy here")));
    ytPrepTick();
}

// with the status timer (twice a second): progress, and the start when ready
void MainWindow::ytPrepTick()
{
    if (ytPrepId.isEmpty())
        return;
    if (playingSong.path() != ytPrepId) {               // another song was chosen meanwhile
        ytPrepId.clear();
        ytPrepFile.clear();
        updateAiStemsBackground();
        return;
    }
    int pct = 0;
    if (ytPrepFile.isEmpty()) {
        pct = (ytFetchId == ytPrepId) ? ytFetchPct * 70 / 100 : 0;      // download + sound: 0..70
    } else {
        if (aiReadyAtStart(ytPrepFile)) {
            const QString f = ytPrepFile;
            uiLog(QString("YouTube %1: ready after %2 ms, plays from the copy with the AI cut")
                  .arg(ytPrepId).arg(ytPrepClock.elapsed()));
            ytPrepId.clear();
            ytPrepFile.clear();
            updateAiStemsBackground();
            playLocalCopy(f);
            return;
        }
        ensureAiStems();
        aiStems->setFocus(ytPrepFile, 0.0);
        const std::shared_ptr<StemTrack> t = aiStems->track(ytPrepFile);
        const int need = int(10LL * AiVocalEngine::SR / AiWindowSeparator::STEP) + 1;   // windows of the first 10 s
        int done = 0;
        for (int k = 0; t && k < need; ++k)
            if (t->frameReady((long long)k * AiWindowSeparator::STEP))
                ++done;
        pct = 70 + 29 * done / qMax(1, need);                               // the AI: 70..99
    }
    ytPrepPct = qBound(0, pct, 99);
    if (ytPrepClock.elapsed() > 120000) {
        ytPrepGiveUp(QString("YouTube %1: not ready after 2 min, plays online").arg(ytPrepId));
        return;
    }
    ui->detail->setDetail(tr("กำลังเตรียมตัดเสียงร้อง %1%").arg(ytPrepPct),
                          tr("เพลงจะเริ่มเองเมื่อพร้อม ตัดเสียงร้องได้ตั้งแต่ต้นเพลง"));
    ui->detail->show();
    detailTimer->start(1500);
}

void MainWindow::ytPrepGiveUp(const QString &why)
{
    uiLog(why);
    const QString id = ytPrepId;
    ytPrepId.clear();
    ytPrepFile.clear();
    updateAiStemsBackground();
    if (id.isEmpty() || playingSong.path() != id)
        return;
    ytSkipPrepOnce = true;                              // this time: online as before
    playYouTube();
}

// every few seconds: the next songs in the queue are made ready (their copy, then their AI), one
// download at a time and never in the way of the song that plays
void MainWindow::ytQueueTick()
{
    static QElapsedTimer clock;
    if (clock.isValid() && clock.elapsed() < 3000)
        return;
    clock.restart();
    if (!settings->value("YtCutAi", false).toBool() || idlePlaying)
        return;
    const bool download = settings->value("YtDownloadFirst", true).toBool()
                          && !YtTools::ytdlpPath().isEmpty() && !YtTools::ffmpegPath().isEmpty();
    const int from = remove_playlist ? 0 : playingIndex + 1;
    QStringList files;
    int taken = 0;
    for (int i = qMax(0, from); i < playlist.size() && taken < 3; ++i) {
        Song *s = playlist[i];
        if (!s || (s->songType() == playingSong.songType() && s->path() == playingSong.path()))
            continue;
        ++taken;
        if (s->songType() == "YOUTUBE") {
            const QString id = s->path();
            const QString local = ytLocalCopy(id);
            if (!local.isEmpty()) {
                files << local;
            } else if (download && !ytFetch && ytPrepId.isEmpty() && !ytFetchOnline
                       && !ytPrefetchFailed.contains(id)) {
                uiLog(QString("queue: downloads YouTube %1 ahead (%2)").arg(id, s->name()));
                // the next song with the playing one near its end is needed soon: a small picture
                const int left = ui->sliderPosition->maximum() - ui->sliderPosition->value();
                startYtFetch(id, !(taken > 1 || left >= 90000));
            }
        } else if (s->songType() == "VIDEO" || s->songType() == "MP3") {
            const QString f = songFilePath(*s);
            if (!f.isEmpty())
                files << f;
        }
    }
    QStringList todo;
    for (const QString &f : files)
        if (!AiStemMaker::hasStems(f))
            todo << f;
    if (todo.isEmpty())
        return;
    ensureAiStems();
    for (const QString &f : todo)
        aiStems->enqueue(f);
    aiStems->prioritize(todo);
}
