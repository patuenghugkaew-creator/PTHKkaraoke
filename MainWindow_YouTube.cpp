// MainWindow: YouTube search, queue and playback
#include "MainWindow_p.h"

// any song type is loaded: MIDI (playing or paused), video, MP3 or YouTube
bool MainWindow::isSongPlaying()
{
    return !player->isPlayerStopped() || player->isPlayerPaused() || videoActive || ytActive || mp3Stream != 0;
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
        QString id = db->songIdFor("VIDEO", rel);
        if (id.isEmpty()) {
            const QStringList ids = importSongs({ file }, true);
            id = ids.isEmpty() ? QString() : ids.first();
        }
        Song *found = id.isEmpty() ? nullptr : db->search(id);
        if (!found || found->id() != id) {
            QMessageBox::warning(dialogParent(), tr("เล่นวิดีโอไม่ได้"), tr("ไม่พบเพลงนี้ในคลังเพลง: ") + file);
            return;
        }
        addToPlaylist(found);
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

// YouTube songs play online with YouTube's own embedded player, in place of the video
void MainWindow::playYouTube()
{
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
                stTicks = stShown = stSkipped = stAiTicks = 0;
                stGrabSum = stGrabMax = stTickGapMax = stShowGapMax = 0;
                statClock.restart();
                lastShownMs = -1;
            }
            if (!pm.isNull()) {
#ifdef KM_HAVE_PROCLOOP
                static bool delayedPrimed = false;
                static bool firstFrameShown = false;
                const double d = (ytAudio && settings->value("YtDelayPicture", true).toBool()) ? ytAudio->delaySeconds() : 0.0;
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
            if (player->isUseMedley() && player->medleyAudioAutoCue() && !endingSong) {
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
            stop();
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
    {   // remembered for the "sung lately / most" lists of the YouTube search
        YtItem it;
        it.id = playingSong.path();
        it.title = playingSong.name();
        it.channel = playingSong.artist();
        YtHistory::record(it);
    }
#ifdef KM_HAVE_PROCLOOP
    if (ytAudio) {
        ytAudio->setPitch(0);
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
