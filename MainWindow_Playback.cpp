// MainWindow: DMX lights, recording, score, video/MP3 playback, pause/resume/stop, window events
#include "MainWindow_p.h"
#include <QTcpSocket>
#include <memory>
#include <QRandomGenerator>

void MainWindow::showDmxDialog()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("ไฟเวที DMX"));

    QComboBox *mode = new QComboBox(&dlg);
    mode->addItem(tr("ปิด"), DmxLights::Off);
    mode->addItem("Enttec DMX USB Pro", DmxLights::EnttecPro);
    mode->addItem("Open DMX / FTDI USB-DMX", DmxLights::OpenDmx);
    mode->setCurrentIndex(mode->findData(settings->value("DmxMode", 0).toInt()));

    QComboBox *port = new QComboBox(&dlg);
    for (const QSerialPortInfo &p : QSerialPortInfo::availablePorts())
        port->addItem(p.portName() + "  " + p.description(), p.portName());
    port->setCurrentIndex(qMax(0, port->findData(settings->value("DmxPort", "").toString())));

    QSpinBox *start = new QSpinBox(&dlg);
    start->setRange(1, 509);
    start->setValue(settings->value("DmxStartChannel", 1).toInt());
    QSpinBox *fixtures = new QSpinBox(&dlg);
    fixtures->setRange(1, 64);
    fixtures->setValue(settings->value("DmxFixtures", 4).toInt());

    QLabel *note = new QLabel(tr("โคมไฟแบบ RGB + Dimmer (4 ช่องต่อดวง: แดง เขียว น้ำเงิน ความสว่าง)\n"
                                 "กลองกระเดื่อง = ไฟกระพริบ, สแนร์ = แฟลชขาว, เปลี่ยนสีทุกจังหวะ"), &dlg);
    QLabel *status = new QLabel(&dlg);

    auto apply = [&]() -> bool {
        QString err;
        bool ok = dmxLights->configure(DmxLights::Mode(mode->currentData().toInt()), port->currentData().toString(),
                                       start->value(), fixtures->value(), &err);
        status->setText(ok ? (dmxLights->isActive() ? tr("เชื่อมต่อแล้ว") : tr("ปิดอยู่"))
                           : tr("เปิดพอร์ตไม่ได้: ") + err);
        return ok;
    };

    QPushButton *test = new QPushButton(tr("ทดสอบ (แฟลช)"), &dlg);
    connect(test, &QPushButton::clicked, &dlg, [&]() { if (apply()) dmxLights->testFlash(); });

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, [&]() { if (apply()) dlg.accept(); });
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    QFormLayout *f = new QFormLayout(&dlg);
    f->addRow(tr("อุปกรณ์ :"), mode);
    f->addRow(tr("พอร์ต :"), port);
    f->addRow(tr("ช่อง DMX เริ่มต้น :"), start);
    f->addRow(tr("จำนวนโคม :"), fixtures);
    f->addRow(note);
    f->addRow(test, status);
    f->addRow(bb);

    if (dlg.exec() == QDialog::Accepted) {
        settings->setValue("DmxMode", mode->currentData().toInt());
        settings->setValue("DmxPort", port->currentData().toString());
        settings->setValue("DmxStartChannel", start->value());
        settings->setValue("DmxFixtures", fixtures->value());
    } else {            // cancelled: back to the saved setup
        dmxLights->configure(DmxLights::Mode(settings->value("DmxMode", 0).toInt()),
                             settings->value("DmxPort", "").toString(),
                             settings->value("DmxStartChannel", 1).toInt(),
                             settings->value("DmxFixtures", 4).toInt());
    }
}

void MainWindow::toggleRecording()
{
    OutputRecorder &r = OutputRecorder::instance();
    if (r.isRecording()) {
        stopRecording();
        return;
    }

    MidiSynthesizer *synth = player->midiSynthesizer();
    QString name = playingSong.name().isEmpty() ? tr("บันทึก") : playingSong.name();
    name.replace(QRegExp("[\\\\/:*?\"<>|]"), "_");
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/PTHK Karaoke Recordings";
    QDir().mkpath(dir);
    QString file = dir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + " " + name + ".wav";

    QString err;
    bool micOk = MicInput::get().start(-1, &err);
    r.setMixMic(micOk);
    r.setGainDb(0.0);
    if (!synth->isOpened() || !r.start(synth->mainMixerHandle(), file, 16, &err)) {
        updateMicUse();
        QMessageBox::warning(dialogParent(), tr("บันทึกเสียงร้อง"), tr("เริ่มบันทึกไม่ได้: ") + err);
        return;
    }
    ui->detail->setDetail(tr("● บันทึกเสียง "), micOk ? tr("ดนตรี + ไมค์") : tr("ดนตรี (ไม่พบไมค์)"));
    ui->detail->show();
    detailTimer->start(3000);
}

void MainWindow::stopRecording()
{
    OutputRecorder &r = OutputRecorder::instance();
    if (!r.isRecording())
        return;
    r.stop();
    r.setMixMic(false);
    updateMicUse();
    ui->detail->setDetail(tr("บันทึกแล้ว "), QFileInfo(r.path()).fileName());
    ui->detail->show();
    detailTimer->start(5000);
}

void MainWindow::setVocalScore(bool on)
{
    vocalScore = on;
    settings->setValue("VocalScore", on);
    updateMicUse();
}

void MainWindow::updateMicUse()
{
    bool need = vocalScore || OutputRecorder::instance().isRecording();
    if (need)
        MicInput::get().start();
    else
        MicInput::get().stop();
}

void MainWindow::resetScore()
{
    melodyNote = -1;
    scoreFrames = 0;
    scoreHits = 0;
}

void MainWindow::showScore()
{
    // ~2 s of melody at least, otherwise the song was skipped
    if (!vocalScore || scoreFrames < 40)
        return;
    int score = qRound(40.0 + 60.0 * scoreHits / scoreFrames);
    score = qBound(40, score, 100);
    scoreLabel->setText(tr("คะแนน") + "\n" + QString::number(score));
    scoreLabel->adjustSize();
    scoreLabel->move((width() - scoreLabel->width()) / 2, (height() - scoreLabel->height()) / 2);
    scoreLabel->raise();
    scoreLabel->show();
    QTimer::singleShot(6000, scoreLabel, &QLabel::hide);
}

// LRC lyrics ("[mm:ss.xx]text") -> the LyricsWidget format: one cursor per character plus one
// at the end of every line, here in milliseconds (the MP3 position drives the lyrics).
static bool readLrc(const QString &file, QString *lyr, QVector<long> *curs)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QByteArray data = f.readAll();

    QTextCodec::ConverterState state;
    QString text = QTextCodec::codecForName("UTF-8")->toUnicode(data.constData(), data.size(), &state);
    if (state.invalidChars > 0)     // not UTF-8: Thai Windows files are TIS-620
        text = QTextCodec::codecForName("TIS-620")->toUnicode(data);

    QList<QPair<long, QString>> lines;
    QRegExp tag("\\[(\\d+):(\\d+(?:\\.\\d+)?)\\]");
    for (const QString &raw : text.split(QRegExp("\r\n|\n|\r"))) {
        QList<long> times;
        int pos = 0;
        while (tag.indexIn(raw, pos) == pos) {      // several time tags may share one line
            times << long(tag.cap(1).toInt() * 60000 + tag.cap(2).toDouble() * 1000);
            pos += tag.matchedLength();
        }
        QString words = raw.mid(pos).trimmed();
        if (times.isEmpty() || words.isEmpty())
            continue;
        for (long t : times)
            lines.append(qMakePair(t, words));
    }
    if (lines.isEmpty())
        return false;
    std::sort(lines.begin(), lines.end(),
              [](const QPair<long, QString> &a, const QPair<long, QString> &b) { return a.first < b.first; });

    QStringList out;
    for (int i = 0; i < lines.count(); i++) {
        const long start = lines[i].first;
        const long end = (i + 1 < lines.count()) ? lines[i + 1].first : start + 4000;
        const QString &w = lines[i].second;
        for (int c = 0; c <= w.length(); c++)
            curs->append(start + (end - start) * c / (w.length() + 1));
        out << w;
    }
    *lyr = out.join("\n");
    return true;
}

void MainWindow::playVideo(const QString &path, bool isVideo)
{
    videoActive = true;
    mediaIsAudio = !isVideo;
    updateAiStemsBackground();

    lyrWidget->hide();
    if (secondLyr != nullptr)
        secondLyr->hide();

    if (mediaIsAudio) {
        QString lyr;
        QVector<long> curs;
        QString lrc = path.left(path.lastIndexOf('.')) + ".lrc";
        if (readLrc(lrc, &lyr, &curs)) {
            lyrWidget->setLyrics(lyr, curs);
            lyrWidget->show();
        }
    }

    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->chMix->hide();
    ui->expandChMix->hide();

    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();
    ui->songDetail->show();
    timer2->start(songDetail_timeout);

    ui->sliderPosition->setValue(0);
    if (isVideo) {
        bgWidget->setVideoPaused(true);     // the karaoke video covers the background
        videoWidget->resize(ui->centralWidget->size());
        videoWidget->show();
        if (secondLyr != nullptr)
            secondMonitor->setVideoVisible(true);
    }

    if (mediaIsAudio) {
        if (!startMp3Stream(path)) {
            videoPlayer->setMuted(false);
            videoPlayer->setVolume(effectiveVolume());
            videoPlayer->setMedia(QUrl::fromLocalFile(path));
            videoPlayer->play();
        }
    } else {
        // Video songs (e.g. downloaded YouTube MP4s): route audio through BASS main mixer
        // to enable Vocal Cut (VocalRemover DSP), pitch/key transpose, and master EQ!
        QElapsedTimer stepClock;
        stepClock.start();
        videoWidget->newSong();
        const bool bassAudio = startVideoAudioStream(path);
        const qint64 tAudio = stepClock.restart();
        if (bassAudio) {
            videoPlayer->setMuted(true);
            videoPlayer->setVolume(0);
        } else {
            videoPlayer->setMuted(false);
            videoPlayer->setVolume(effectiveVolume());
        }
        videoPlayer->setMedia(QUrl::fromLocalFile(path));
        const qint64 tMedia = stepClock.restart();
        videoPlayer->play();
        uiLog(QString("video start: sound %1 ms, open picture %2 ms, play %3 ms (%4)")
              .arg(tAudio).arg(tMedia).arg(stepClock.elapsed()).arg(QFileInfo(path).fileName()));
    }
    if (!idlePlaying)
        db->addHistory(&playingSong);
    dmxLights->setPlaying(true);
}

bool MainWindow::startMp3Stream(const QString &path)
{
    MidiSynthesizer *synth = player->midiSynthesizer();
    const DWORD mixer = synth->isOpened() ? synth->mainMixerHandle() : 0;
    if (!mixer)
        return false;

    mp3Stream = BASS_StreamCreateFile(FALSE, path.utf16(), 0, 0,
                                      BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
    if (!mp3Stream)
        return false;
    if (!BASS_Mixer_StreamAddChannel(mixer, mp3Stream, BASS_MIXER_CHAN_NORAMPIN)) {
        BASS_StreamFree(mp3Stream);
        mp3Stream = 0;
        return false;
    }
    setMp3Volume();

    const qint64 ms = qint64(BASS_ChannelBytes2Seconds(mp3Stream, BASS_ChannelGetLength(mp3Stream, BASS_POS_BYTE)) * 1000);
    ui->sliderPosition->setMaximum(int(ms));
    onPlayerDurationMSChanged(ms);
    mp3Timer->start();
    return true;
}

void MainWindow::stopMp3Stream()
{
    mp3Timer->stop();
    if (mp3CrossfadeTimer && mp3CrossfadeTimer->isActive())
        mp3CrossfadeTimer->stop();
    if (mp3StreamNext) {
        BASS_Mixer_ChannelRemove(mp3StreamNext);
        BASS_StreamFree(mp3StreamNext);
        mp3StreamNext = 0;
    }
    if (!mp3Stream)
        return;
    BASS_Mixer_ChannelRemove(mp3Stream);
    BASS_StreamFree(mp3Stream);
    mp3Stream = 0;
}

bool MainWindow::startMp3Crossfade(const QString &nextPath, double durationSec, int curve)
{
    MidiSynthesizer *synth = player->midiSynthesizer();
    const DWORD mixer = synth->isOpened() ? synth->mainMixerHandle() : 0;
    if (!mixer || !mp3Stream)
        return false;

    if (mp3StreamNext) {
        BASS_Mixer_ChannelRemove(mp3StreamNext);
        BASS_StreamFree(mp3StreamNext);
        mp3StreamNext = 0;
    }

    mp3StreamNext = BASS_StreamCreateFile(FALSE, nextPath.utf16(), 0, 0,
                                          BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
    if (!mp3StreamNext)
        return false;

    if (!BASS_Mixer_StreamAddChannel(mixer, mp3StreamNext, BASS_MIXER_CHAN_NORAMPIN)) {
        BASS_StreamFree(mp3StreamNext);
        mp3StreamNext = 0;
        return false;
    }

    BASS_ChannelSetAttribute(mp3StreamNext, BASS_ATTRIB_VOL, 0.0f);

    mp3CrossfadeTotalMs = qMax(500, int(durationSec * 1000));
    mp3CrossfadeCurve = curve;
    mp3CrossfadeElapsedMs = 0;

    if (!mp3CrossfadeTimer) {
        mp3CrossfadeTimer = new QTimer(this);
        mp3CrossfadeTimer->setInterval(30);
        connect(mp3CrossfadeTimer, &QTimer::timeout, this, [this]() {
            mp3CrossfadeElapsedMs += 30;
            const double t = qBound(0.0, double(mp3CrossfadeElapsedMs) / double(mp3CrossfadeTotalMs), 1.0);
            const float master = effectiveVolume() / 100.0f;
            float gainA = 0.0f;
            float gainB = 0.0f;

            if (mp3CrossfadeCurve == 0) {
                const double pi_2 = 3.14159265358979323846 / 2.0;
                gainA = float(std::cos(t * pi_2)) * master;
                gainB = float(std::sin(t * pi_2)) * master;
            } else {
                gainA = float(1.0 - t) * master;
                gainB = float(t) * master;
            }

            if (mp3Stream)
                BASS_ChannelSetAttribute(mp3Stream, BASS_ATTRIB_VOL, gainA);
            if (mp3StreamNext)
                BASS_ChannelSetAttribute(mp3StreamNext, BASS_ATTRIB_VOL, gainB);

            if (t >= 1.0) {
                mp3CrossfadeTimer->stop();
                if (mp3Stream) {
                    BASS_Mixer_ChannelRemove(mp3Stream);
                    BASS_StreamFree(mp3Stream);
                }
                mp3Stream = mp3StreamNext;
                mp3StreamNext = 0;
                setMp3Volume();

                const qint64 ms = qint64(BASS_ChannelBytes2Seconds(mp3Stream, BASS_ChannelGetLength(mp3Stream, BASS_POS_BYTE)) * 1000);
                ui->sliderPosition->setMaximum(int(ms));
                onPlayerDurationMSChanged(ms);
            }
        });
    }

    mp3CrossfadeTimer->start();
    return true;
}

void MainWindow::setMp3Volume()
{
    if (mp3Stream) {
        BASS_ChannelSetAttribute(mp3Stream, BASS_ATTRIB_VOL, effectiveVolume() / 100.0f);
    }
}

qint64 MainWindow::mp3PositionMs()
{
    QWORD pos = BASS_Mixer_ChannelGetPosition(mp3Stream, BASS_POS_BYTE);
    return qint64(BASS_ChannelBytes2Seconds(mp3Stream, pos) * 1000);
}

void MainWindow::pause()
{
    if (mp3Stream) {
        BASS_Mixer_ChannelFlags(mp3Stream, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        mp3Timer->stop();
        return;
    }
    if (videoActive) {
        videoPlayer->pause();
        if (videoAudioTempo)
            BASS_Mixer_ChannelFlags(videoAudioTempo, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        return;
    }
#ifdef PTHK_WEBENGINE
    if (ytActive) {
        ytPlayer->pause();
        return;
    }
#endif

    scoreTimer->stop();
    positionTimer->stop();
    lyricsTimer->stop();
    lyrWidget->stopAnimation();
    if (secondLyr != nullptr)
        secondLyr->stopAnimation();
    player->stop();

    #ifdef _WIN32
    taskbarButton->progress()->pause();
    #endif
}

void MainWindow::resume()
{
    afterLyrics = false;
    if (mp3Stream) {
        BASS_Mixer_ChannelFlags(mp3Stream, 0, BASS_MIXER_CHAN_PAUSE);
        mp3Timer->start();
        return;
    }
    if (videoActive) {
        videoPlayer->play();
        if (videoAudioTempo)
            BASS_Mixer_ChannelFlags(videoAudioTempo, 0, BASS_MIXER_CHAN_PAUSE);
        return;
    }
#ifdef PTHK_WEBENGINE
    if (ytActive) {
        ytPlayer->resume();
        return;
    }
#endif

    player->play();
    positionTimer->start();
    lyricsTimer->start();
    scoreTimer->start();

    #ifdef _WIN32
    taskbarButton->progress()->resume();
    #endif
}

void MainWindow::stop()
{
    ytFetchOnline = false;      // (a copy still downloading goes on: it plays from disk next time)
    if (!ytCopyPending.isEmpty() && aiStems)
        aiStems->clearFocus();  // the AI does not go on with that copy while the next song plays
    ytCopyPending.clear();      // (the copy stays: it plays from disk next time)
    ytPrepId.clear();           // (its download goes on: it is ready next time)
    ytPrepFile.clear();
    updateAiStemsBackground();  // other AI work may go on again
    finishSongLevel();          // a first measurement is remembered; no song-level gain while stopped
    SongLevel::get().setGainDb(0.0, 300);
    if (!endingSong) {          // the fade-out that ends a song calls stop() itself
        fadeTimer->stop();
        fadeDone = nullptr;
    }
    if (videoActive) {
        videoActive = false;
        mediaIsAudio = false;
        stopMp3Stream();
        stopVideoAudioStream();
        videoPlayer->stop();
        videoPlayer->setMedia(QMediaContent());
        videoWidget->clear();
        videoWidget->hide();
        bgWidget->setVideoPaused(false);
    }
#ifdef PTHK_WEBENGINE
    if (ytActive) {
        ytActive = false;
        ytMirrorTimer->stop();
        ytPlayer->stopVideo();
        ytPlayer->hide();
        videoWidget->clear();
        videoWidget->hide();
        bgWidget->setVideoPaused(false);
#ifdef KM_HAVE_PROCLOOP
        // The AI holds several seconds of processed sound: drop it, or the song plays on after Stop
        if (ytAudio) {
            ytAudio->setInputOpen(false);   // reopened when the next clip really plays (mirror timer)
            if (ytAudio->aiMode() && aiEngine)
                aiEngine->reset();
        }
        ytInputGateOpen = false;
        delayedFrames.clear();
#endif
    }
#endif

    if (secondLyr != nullptr)
        secondMonitor->setVideoVisible(false);

    scoreTimer->stop();
    melodyNote = -1;
    dmxLights->setPlaying(false);
    positionTimer->stop();
    lyricsTimer->stop();
    player->stop(true);

    ui->sliderPosition->setValue(0);
    lyrWidget->hide();
    lyrWidget->reset();
    if (secondLyr != nullptr) {
        secondLyr->hide();
        secondLyr->reset();
    }
    ui->sliderPosition->setValue(0);
    onPlayerPositionMSChanged(0);

    ui->rhmWidget->reset();

    #ifdef _WIN32
    taskbarButton->progress()->hide();
    taskbarButton->progress()->setValue(0);
    #endif
    idlePlaying = false;
    idleSwitching = false;
    idleVocalByUser = false;
    idleClock.restart();
    updateAiStemsBackground();
}

void MainWindow::playNext()
{
    if (playlist.count() > 0 && playingIndex < playlist.count()-1) {
        play(playingIndex+1);
    } else if (idlePlaying && settings->value("IdleMusic/On", false).toBool()) {
        startNextIdleSong();
    } else {
        stop();
    }
}

void MainWindow::playPrevious()
{
    if (playingIndex > 0 && playlist.count() > 0) {
        play(playingIndex - 1);
    }
}

void MainWindow::showSavedWindowState()
{
    // same monitor and place as last time (Qt moves it onto a screen if that monitor is gone)
    restoreGeometry(settings->value("WindowGeometry").toByteArray());

    if (startFullScreen)
        showFullScreen();
    else if (startMaximized || true)
        showMaximized();
    else
        show();
    setFocus();     // number keys go straight to the song search
    if (settings->value("SoundPadsPanel", false).toBool())     // the sound pads panel, as last time
        QTimer::singleShot(800, this, [this]() { showSoundPadsPanel(true); });
    QTimer::singleShot(500, this, [this]() { updateTicker(); });
    updateShopLockUi();
    idleClock.start();
    if (!idleSmart) {
        idleSmart = new IdleSmart(this);
        idleSmart->dbFile = db->database()->databaseName();
        idleSmart->log = [this](const QString &t) { uiLog(t); };
        // the songs to follow: tonight's (last 3 hours, newest first), else the ones sung most at
        // this hour on the same kind of day (weekend or not) in the last 8 weeks
        idleSmart->seeds = [this]() {
            QVector<IdleSmart::Seed> out;
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            struct T { qint64 when; IdleSmart::Seed s; };
            QVector<T> tonight;
            for (const SongDatabase::SungSong &s : db->sungSince(now - 3 * 3600, 10)) {
                IdleSmart::Seed sd;
                sd.name = s.name; sd.artist = s.artist; sd.key = s.type + s.path;
                sd.songType = s.type; sd.tempo = s.tempo;
                tonight << T{ s.n, sd };
            }
            for (const YtHistory::Entry &e : YtHistory::all())      // YouTube songs are not in that history
                if (e.last / 1000 >= now - 3 * 3600) {
                    IdleSmart::Seed sd;
                    sd.name = e.item.title; sd.artist = e.item.channel; sd.ytId = e.item.id;
                    sd.key = "YOUTUBE" + e.item.id; sd.songType = "YOUTUBE";
                    tonight << T{ e.last / 1000, sd };
                }
            std::sort(tonight.begin(), tonight.end(), [](const T &a, const T &b) { return a.when > b.when; });
            for (const T &t : tonight)
                out << t.s;
            // Few songs tonight, or songs without a singer's name (many library songs have none):
            // add this hour's, then the shop's most sung ones, so the music follows the shop's style
            // (chart only played pop to a luk thung shop).
            auto withSinger = [&out]() {
                int n = 0;
                for (const IdleSmart::Seed &s : out)
                    n += s.artist.trimmed().isEmpty() ? 0 : 1;
                return n;
            };
            QSet<QString> have;
            for (const IdleSmart::Seed &s : out)
                have.insert(s.key);
            if (withSinger() < 3) {
                const QDateTime dt = QDateTime::currentDateTime();
                QVector<IdleSmart::Seed> hour;
                for (const SongDatabase::SungSong &s : db->sungAtHour(dt.time().hour(), dt.date().dayOfWeek() >= 6))
                    if (!have.contains(s.type + s.path)) {
                        IdleSmart::Seed sd;
                        sd.name = s.name; sd.artist = s.artist; sd.key = s.type + s.path;
                        sd.songType = s.type; sd.tempo = s.tempo;
                        hour << sd;
                    }
                // most sung first; let them take turns
                for (int i = hour.size() - 1; i > 0; --i)
                    std::swap(hour[i], hour[QRandomGenerator::global()->bounded(qMin(i + 1, 5))]);
                for (const IdleSmart::Seed &s : hour)
                    have.insert(s.key);
                out += hour;
            }
            if (withSinger() < 3) {
                QVector<IdleSmart::Seed> top;
                for (Song s : db->songList(SongDatabase::ListKind::Top, 20))
                    if (!have.contains(s.songType() + s.path())) {
                        IdleSmart::Seed sd;
                        sd.name = s.name(); sd.artist = s.artist(); sd.key = s.songType() + s.path();
                        sd.songType = s.songType(); sd.tempo = s.tempo();
                        top << sd;
                    }
                for (int i = top.size() - 1; i > 0; --i)
                    std::swap(top[i], top[QRandomGenerator::global()->bounded(i + 1)]);
                out += top;
            }
            // the ones with a singer first (they are the ones that can be searched)
            std::stable_partition(out.begin(), out.end(),
                                  [](const IdleSmart::Seed &s) { return !s.artist.trimmed().isEmpty(); });
            return out;
        };
    }
    if (!idleTimer) {
        idleTimer = new QTimer(this);
        idleTimer->setInterval(1000);
        connect(idleTimer, &QTimer::timeout, this, [this]() { checkIdleMusic(); });
        idleTimer->start();
    }
}

// ---- Scrolling text ("ข้อความวิ่ง", Widgets/TickerBar.h) ---------------------------------------------
// The shop's messages, then the next songs in the queue, round and round; on the player's screen and
// the 2nd screen. Checked once a second, so a change in the queue shows on the next pass.

QString MainWindow::tickerText()
{
    const QString gap = QStringLiteral("          •          ");
    QStringList parts;
    for (const QString &m : settings->value("Ticker/Messages", TickerDialog::defaultMessages()).toStringList())
        if (!m.trimmed().isEmpty())
            parts << m.trimmed();
    if (settings->value("Ticker/ShowNext", true).toBool()) {
        const int first = remove_playlist ? 0 : playingIndex + 1;
        const int count = qBound(1, settings->value("Ticker/NextCount", 2).toInt(), 5);
        QStringList next;
        for (int i = first; i >= 0 && i < playlist.size() && next.size() < count; ++i) {
            Song *s = playlist[i];
            next << (s->artist().isEmpty() ? s->name() : s->name() + " – " + s->artist());
        }
        if (!next.isEmpty())
            parts << (next.size() == 1 ? tr("เพลงถัดไป: %1").arg(next.first())
                                        : tr("เพลงถัดไป: %1").arg(next.join(QStringLiteral("   ▸   "))));
    }
    return parts.join(gap);
}

void MainWindow::updateTicker()
{
    const bool on = settings->value("Ticker/On", false).toBool();
    if (!tickerTimer) {
        tickerTimer = new QTimer(this);
        tickerTimer->setInterval(1000);
        connect(tickerTimer, &QTimer::timeout, this, [this]() { updateTicker(); });
    }
    if (!on) {
        tickerTimer->stop();
        if (tickerMain) tickerMain->setActive(false);
        if (tickerSecond) tickerSecond->setActive(false);
        return;
    }
    if (!tickerTimer->isActive())
        tickerTimer->start();

    TickerBar::Style st;
    st.top = settings->value("Ticker/Top", true).toBool();
    st.speed = qBound(40, settings->value("Ticker/Speed", 110).toInt(), 300);
    st.sizePct = qBound(60, settings->value("Ticker/Size", 100).toInt(), 200);
    st.fg = settings->value("Ticker/Fg", QColor(255, 255, 255)).value<QColor>();
    st.bg = settings->value("Ticker/Bg", QColor(0, 0, 0)).value<QColor>();
    st.bg.setAlpha(qBound(0, settings->value("Ticker/BgAlpha", 150).toInt(), 255));
    const QString text = tickerText();

    auto apply = [&](QPointer<TickerBar> &t, QWidget *host, bool want) {
        if (!want || !host) {
            if (t) t->setActive(false);
            return;
        }
        if (!t) {
            t = new TickerBar(host);
            if (host == ui->centralWidget) {
                // on the player's screen: not over the clock / beat box in the top right corner,
                // and above the control bar at the bottom
                t->reserveRight = [this]() {
                    int left = ui->centralWidget->width();
                    for (QWidget *w : { static_cast<QWidget *>(ui->lcdTime), static_cast<QWidget *>(ui->rhmWidget),
                                        static_cast<QWidget *>(ui->detail) }) {
                        if (!w || !w->isVisible()) continue;
                        const QPoint p = ui->centralWidget->mapFromGlobal(w->mapToGlobal(QPoint(0, 0)));
                        if (p.y() < 120)
                            left = qMin(left, p.x() - 24);     // a clear gap before the clock
                    }
                    return ui->centralWidget->width() - left;
                };
                t->reserveBottom = [this]() { return ui->frameControl->isVisible() ? ui->frameControl->height() : 0; };
                // the song search, the queue and the medley cut box open at the top: step aside
                t->stepAside = [this]() {
                    return ui->frameSearch->isVisible() || ui->playlistWidget->isVisible() || ui->songMedley->isVisible()
                        || ui->songDetail->isVisible();
                };
            }
        }
        t->setStyle(st);                // (nothing happens when it is the same)
        t->setText(text);
        t->setActive(true);
        t->placeAgain();
    };
    apply(tickerMain, ui->centralWidget, settings->value("Ticker/Main", true).toBool());
    apply(tickerSecond, secondMonitor && secondLyr ? static_cast<QWidget *>(secondMonitor) : nullptr,
          settings->value("Ticker/Second", true).toBool());
}

void MainWindow::openTickerSettings()
{
    TickerDialog dlg(dialogParent(), settings);
    dlg.onChanged = [this]() { updateTicker(); };
    dlg.exec();
    updateTicker();
    setFocus();
}

// ---- Screen lock for shops + automatic idle background music (Dialogs/ShopModeDialog.h) ---------------

bool MainWindow::keepIdleVocal() const
{
    return idlePlaying && !idleVocalByUser && settings->value("IdleMusic/KeepVocal", true).toBool();
}

bool MainWindow::songInMedley()
{
    return player->isUseMedley() && !idlePlaying;
}

bool MainWindow::isShopLocked() const
{
    return settings && settings->value("ShopLock/On", false).toBool();
}

bool MainWindow::isShopStrictLocked() const
{
    return isShopLocked() && settings->value("ShopLock/Strict", false).toBool();
}

void MainWindow::notifyShopStrictLocked(const QString &actionName)
{
    ui->detail->setDetail(tr("🔒 หน้าจอล็อกอยู่"),
                          tr("ไม่อนุญาตให้%1 (กด Ctrl+L เพื่อปลดล็อก)").arg(actionName));
    ui->detail->show();
    detailTimer->start(3000);
}

bool MainWindow::requireShopUnlock(const QString &actionName)
{
    if (!isShopLocked())
        return true;
    if (!ShopPinDialog::verify(dialogParent(), settings, actionName)) {
        setFocus();
        return false;
    }
    settings->setValue("ShopLock/On", false);
    updateShopLockUi();
    ui->detail->setDetail(tr("🔓 หน้าจอร้าน"), tr("ปลดล็อกแล้ว"));
    ui->detail->show();
    detailTimer->start(2500);
    setFocus();
    return true;
}

void MainWindow::toggleShopLock()
{
    if (isShopLocked()) {
        if (!ShopPinDialog::verify(dialogParent(), settings)) {
            updateShopLockUi();
            setFocus();
            return;
        }
        settings->setValue("ShopLock/On", false);
        updateShopLockUi();
        ui->detail->setDetail(tr("🔓 หน้าจอร้าน"), tr("ปลดล็อกแล้ว"));
        ui->detail->show();
        detailTimer->start(2500);
    } else {
        settings->setValue("ShopLock/On", true);
        if (synthMix && synthMix->isVisible())
            synthMix->hide();
        updateShopLockUi();
        ui->detail->setDetail(tr("🔒 หน้าจอร้าน"),
                              isShopStrictLocked() ? tr("ล็อกเข้มงวดแล้ว (กด Ctrl+L เพื่อปลดล็อก)")
                                                   : tr("ล็อกแล้ว (กด Ctrl+L เพื่อปลดล็อก)"));
        ui->detail->show();
        detailTimer->start(3000);
    }
    setFocus();
}

void MainWindow::openShopModeSettings()
{
    if (isShopLocked() && !settings->value("ShopLock/Pin", "").toString().trimmed().isEmpty()) {
        if (!ShopPinDialog::verify(dialogParent(), settings, tr("ตั้งค่าล็อกหน้าจอ และเพลงคลอ"))) {
            setFocus();
            return;
        }
    }
    ShopModeDialog dlg(dialogParent(), settings);
    dlg.onChanged = [this]() {
        updateShopLockUi();
        applyVolume();
        applyVideoVocalCut();
        updateBarStatus();
        if (!settings->value("IdleMusic/On", false).toBool() && idlePlaying)
            stop();
    };
    dlg.onPlayIdleNow = [this]() {
        if (!isSongPlaying() || idlePlaying)
            startNextIdleSong();
    };
    dlg.exec();
    updateShopLockUi();
    applyVolume();
    updateBarStatus();
    setFocus();
}

void MainWindow::updateShopLockUi()
{
    const bool locked = isShopLocked();
    if (btnShopLock) {
        QSignalBlocker b(btnShopLock);
        btnShopLock->setChecked(locked);
        btnShopLock->setText(locked ? tr("ล็อกอยู่") : tr("ล็อก"));
    }
    if (ui && ui->sliderPosition)
        ui->sliderPosition->setEnabled(!isShopStrictLocked());
    fitControlBar();
    updateBarStatus();
}

bool MainWindow::pickIdleSong(Song *out)
{
    if (!out || !db)
        return false;
    const int source = qBound(0, settings->value("IdleMusic/Source", 4).toInt(), 4);

    // Source 4: smart (IdleSmart.h): like tonight's customers + YouTube hits; a library song from
    // its file, another from YouTube. Nothing ready yet (no internet...): the shop's hits below.
    if (source == 4 && idleSmart) {
        IdlePick p;
        if (!ytOnline)
            uiLog("idle smart: no internet, library songs only");
        if (idleSmart->next(&p, [this](Song s) { return !songFilePath(s).isEmpty(); }, ytOnline)) {
            if (p.local) {
                *out = p.song;
            } else {
                *out = Song();
                out->setId("YT");
                out->setName(p.yt.title);
                out->setArtist(p.yt.channel);
                out->setSongType("YOUTUBE");
                out->setPath(p.yt.id);
            }
            idleContextTag = p.contextTag;
            uiLog(QString("idle smart pick: %1 %2 | %3 [%4]").arg(p.local ? "library" : "YouTube", out->name(), out->artist(), p.contextTag));
            return true;
        }
        idleSmart->refill();
    }

    // Source 3: custom folder of MP3 / video files
    if (source == 3) {
        const QString folder = settings->value("IdleMusic/Folder", "").toString().trimmed();
        if (!folder.isEmpty() && QDir(folder).exists()) {
            // the types the players play (SongFormats.h; webm was listed here but does not play)
            const QStringList &videoExts = SongFormats::videoExts();
            const QStringList mediaExts = SongFormats::audioExts() + videoExts;
            QStringList files;
            QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
            int seen = 0;       // runs on the UI thread: stop early in a huge folder (a whole drive)
            while (it.hasNext() && files.size() < 1000 && ++seen <= 20000) {
                const QString f = it.next();
                if (mediaExts.contains(QFileInfo(f).suffix().toLower()))
                    files << f;
            }
            if (!files.isEmpty()) {
                QRandomGenerator *rnd = QRandomGenerator::global();
                QString chosen = files.at(rnd->bounded(files.size()));
                if (files.size() > 1 && chosen == lastIdlePath)
                    chosen = files.at((files.indexOf(chosen) + 1 + rnd->bounded(files.size() - 1)) % files.size());
                const QFileInfo fi(chosen);
                const bool isVid = videoExts.contains(fi.suffix().toLower());
                *out = Song();
                out->setId("IDLE");
                out->setName(fi.completeBaseName());
                out->setSongType(isVid ? "VIDEO" : "MP3");
                out->setPath(fi.absoluteFilePath());
                return true;
            }
        }
    }

    // Source 0 (Top/History) or Source 1 (Favorites); also smart with nothing ready
    if (source == 0 || source == 1 || source == 4) {
        QList<Song> list = db->songList(source == 1 ? SongDatabase::ListKind::Favorites
                                                    : SongDatabase::ListKind::Top, 150);
        if (list.isEmpty() && source == 0)
            list = db->songList(SongDatabase::ListKind::History, 150);
        if (!list.isEmpty()) {
            for (int tries = 0; tries < 20; ++tries) {
                Song cand = list.at(QRandomGenerator::global()->bounded(list.size()));
                if (list.size() > 1 && cand.path() == lastIdlePath && tries < 10)
                    continue;
                if (!songFilePath(cand).isEmpty()) {
                    *out = cand;
                    return true;
                }
            }
        }
    }

    // Source 2 (Random from library) or fallback when Top/Favorites/Folder is empty
    for (int tries = 0; tries < 25; ++tries) {
        Song cand;
        if (!db->randomSong(&cand))
            break;
        if (cand.path() == lastIdlePath && tries < 15)
            continue;
        if (!songFilePath(cand).isEmpty()) {
            *out = cand;
            return true;
        }
    }
    return false;
}

void MainWindow::startNextIdleSong()
{
    Song next;
    idleContextTag.clear();
    if (!pickIdleSong(&next))
        return;
    lastIdlePath = next.path();
    playingSong = next;
    idlePlaying = true;
    idleSwitching = false;
    play(-2);
    const QString head = idleContextTag.isEmpty() ? tr("♪ เพลงคลออัตโนมัติ")
                                                  : tr("♪ เพลงคลอ (%1)").arg(idleContextTag);
    ui->detail->setDetail(head,
                          next.artist().isEmpty() ? next.name() : (next.name() + " - " + next.artist()));
    ui->detail->show();
    detailTimer->start(3500);
}

// Can we reach YouTube? A TCP connection to www.youtube.com:443 within 4 s, in the background.
void MainWindow::checkYouTubeOnline()
{
    if (ytOnlineChecking)
        return;
    ytOnlineChecking = true;
    ytOnlineClock.restart();
    QTcpSocket *sock = new QTcpSocket(this);
    QTimer *limit = new QTimer(sock);
    limit->setSingleShot(true);
    auto finished = std::make_shared<bool>(false);
    auto done = [this, sock, finished](bool ok) {
        if (*finished)
            return;
        *finished = true;
        ytOnlineChecking = false;
        ytOnlineClock.restart();
        if (ok != ytOnline)
            uiLog(ok ? QString("YouTube reachable again: idle music from YouTube")
                     : QString("YouTube not reachable: idle music plays library songs"));
        ytOnline = ok;
        sock->abort();
        sock->deleteLater();
    };
    // test switch: a file "debug-offline.txt" next to the exe = no internet (to try the offline path
    // without cutting the shop's network)
    if (QFile::exists(QCoreApplication::applicationDirPath() + "/debug-offline.txt")) {
        done(false);
        return;
    }
    connect(sock, &QTcpSocket::connected, this, [done]() { done(true); });
    connect(sock, &QAbstractSocket::errorOccurred, this, [done](QAbstractSocket::SocketError) { done(false); });
    connect(limit, &QTimer::timeout, this, [done]() { done(false); });
    limit->start(4000);
    sock->connectToHost("www.youtube.com", 443);
}

void MainWindow::checkIdleMusic()
{
    if (!settings || !settings->value("IdleMusic/On", false).toBool())
        return;
    if (idleSmart && settings->value("IdleMusic/Source", 4).toInt() == 4) {
        // internet for the YouTube picks: every minute, every 20 s while it is gone
        if (!ytOnlineClock.isValid() || ytOnlineClock.elapsed() > (ytOnline ? 60000 : 20000))
            checkYouTubeOnline();
        // prepared in the background, also while the customers sing: a new customer song -> its Mix
        if (!idlePlaying && isSongPlaying()) {
            const QString k = playingSong.songType() + playingSong.path();
            if (k != idleSeedKey) {
                idleSeedKey = k;
                idleSmart->customerSang();
            }
        }
        idleSmart->setStyle(settings->value("IdleMusic/Style", "auto").toString());
        idleSmart->refill();
    }
    if (db && db->isRunning()) {
        idleClock.restart();
        return;
    }
    const int unplayedCount = remove_playlist ? playlist.count()
                                              : qMax(0, playlist.count() - (playingIndex + 1));
    if (isSongPlaying() || unplayedCount > 0) {
        idleClock.restart();
        return;
    }
    if (!idleClock.isValid()) {
        idleClock.start();
        return;
    }
    const int delaySec = qBound(3, settings->value("IdleMusic/DelaySec", 10).toInt(), 120);
    if (idleClock.elapsed() >= qint64(delaySec) * 1000) {
        idleClock.restart();
        startNextIdleSong();
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    #ifdef _WIN32
    taskbarButton->setWindow(this->windowHandle());
    #endif

    if (firstShow)
    {
        firstShow = false;
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (ui->chMix->isLock())
        return;

    if (event->pos().y() < 70 && event->pos().x() < 700) {
        if (false) showChMix();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    bgWidget->resize(ui->centralWidget->size());
    videoWidget->resize(ui->centralWidget->size());
#ifdef PTHK_WEBENGINE
    if (ytPlayer)
        ytPlayer->resize(854, 480); // 854x480 reduces GPU readback bandwidth by >55%
#endif
    lyrWidget->resize(ui->centralWidget->size());
    fitSearchBar();
    fitQueue();
    updateDetail->move(width() - 260, 70);
    if (reqToast && reqToast->isVisible())
        reqToast->move(width() - reqToast->width() - 16, 16);
    emit resized(event->size());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!requireShopUnlock(tr("ออกจากโปรแกรม"))) {
        event->ignore();
        return;
    }
    QMessageBox::StandardButton resBtn = QMessageBox::question(
                                            this, tr("ออกจากโปรแกรม"),
                                            tr("ท่านต้องการออกจากโปรแกรม?"),
                                            QMessageBox::Yes|QMessageBox::No);
    if (resBtn != QMessageBox::Yes) {
        event->ignore();
    } else {
        synthMix->close();      // saves the mixer settings
        if (secondLyr != nullptr) {
            secondMonitor->setClosingByPlayer();
            secondMonitor->close();
            delete secondMonitor;
            secondLyr = nullptr;
        }
        event->accept();
        qApp->quit();   // the hidden mixer window must not keep the program running
    }
}
