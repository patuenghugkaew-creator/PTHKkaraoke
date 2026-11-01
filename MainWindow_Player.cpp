// MainWindow: Player timers and slots, medley switching, tempo/key, drum pads, playlist files, drum hold, SIB backfill, AI stems for videos
#include "MainWindow_p.h"

void MainWindow::showAboutDialog()
{
    AboutDialog d(dialogParent());
    d.setModal(true);
    d.adjustSize();
    d.setFixedSize(d.size());
    d.exec();
}

void MainWindow::showAboutQtDialog()
{
    QMessageBox::aboutQt(dialogParent());
}

void MainWindow::onPositiomTimerTimeOut()
{
    if (ui->sliderPosition->isSliderDown())
        return;
    int tick = player->positionTick();
    ui->sliderPosition->setValue(tick);
    #ifdef _WIN32
    static int lastTaskbarUpdate = 0;
    if (qAbs(tick - lastTaskbarUpdate) > 200) {
        taskbarButton->progress()->setValue(tick);
        lastTaskbarUpdate = tick;
    }
    #endif
    onPlayerPositionMSChanged(player->positionMs());
    ui->rhmWidget->setCurrentBeat( player->currentBeat() );
    dmxLights->beat(player->currentBeat());
}

void MainWindow::onLyricsTimerTimeOut()
{
    lyrWidget->setPositionCursor(player->positionTick() + 25);
    if (secondLyr != nullptr)
        secondLyr->setPositionCursor(player->positionTick() + 25);
    checkEndAfterLyrics(player->positionTick(), player->durationMs() - player->positionMs());
}

void MainWindow::onPlayerDurationMSChanged(qint64 d)
{
    QDateTime t = QDateTime::fromMSecsSinceEpoch(d);
    ui->lbDuration->setText( locale.toString(t, "mm:ss") );
}

void MainWindow::onPlayerPositionMSChanged(qint64 p)
{
    static qint64 lastSec = -1;
    qint64 curSec = p / 1000;
    if (p != 0 && curSec == lastSec)
        return;
    lastSec = curSec;

    QDateTime t = QDateTime::fromMSecsSinceEpoch(p);
    ui->lbPosition->setText( locale.toString(t, "mm:ss") );
}

void MainWindow::onPlayerDurationTickChanged(int d)
{
    ui->sliderPosition->setMaximum(d);
}

void MainWindow::onSliderPositionPressed()
{
    if (mp3Stream) {
        playAfterSeek = mp3Timer->isActive();
        pause();
        return;
    }
    if (videoActive) {
        playAfterSeek = videoPlayer->state() == QMediaPlayer::PlayingState;
        videoPlayer->pause();
        if (videoAudioTempo)
            BASS_Mixer_ChannelFlags(videoAudioTempo, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        return;
    }
#ifdef PTHK_WEBENGINE
    if (ytActive) {
        playAfterSeek = !ytPlayer->isPaused();
        return;     // YouTube keeps playing while the slider is dragged; it jumps on release
    }
#endif

    playAfterSeek = player->isPlayerPlaying();
    pause();
}

void MainWindow::onSliderPositionReleased()
{
    if (mp3Stream) {        // slider in milliseconds
        const qint64 ms = ui->sliderPosition->value();
        BASS_Mixer_ChannelSetPosition(mp3Stream, BASS_ChannelSeconds2Bytes(mp3Stream, ms / 1000.0), BASS_POS_BYTE);
        lyrWidget->setSeekPositionCursor(int(ms));
        onPlayerPositionMSChanged(ms);
        if (playAfterSeek)
            resume();
        return;
    }
    if (videoActive) {      // video / MP3 slider is in milliseconds
        const qint64 seekVal = ui->sliderPosition->value();
        videoPlayer->setPosition(seekVal);
        if (videoAudioTempo)
            BASS_Mixer_ChannelSetPosition(videoAudioTempo, BASS_ChannelSeconds2Bytes(videoAudioTempo, seekVal / 1000.0), BASS_POS_BYTE);
        if (mediaIsAudio)
            lyrWidget->setSeekPositionCursor(seekVal);
        if (playAfterSeek) {
            videoPlayer->play();
            if (videoAudioTempo)
                BASS_Mixer_ChannelFlags(videoAudioTempo, 0, BASS_MIXER_CHAN_PAUSE);
        }
        return;
    }
#ifdef PTHK_WEBENGINE
    if (ytActive) {
        qint64 to = ui->sliderPosition->value();
        const qint64 dur = ytPlayer->durationMs();
        if (dur > 3000 && to > dur - 2000)
            to = dur - 2000;
        ytPlayer->seek(to, true);
#ifdef KM_HAVE_PROCLOOP
        if (ytAudio && ytAudio->aiMode())
            aiEngine->reset();
        delayedFrames.clear();
#endif
        return;
    }
#endif

    if (player->isPlayerStopped() && playingSong.id().isEmpty() && !player->midiFile()) {
        ui->sliderPosition->setValue(0);
        return;
    }

    #ifdef _WIN32
    taskbarButton->progress()->setValue(ui->sliderPosition->value());
    #endif

    player->setPositionTick(ui->sliderPosition->value());
    lyrWidget->setSeekPositionCursor(ui->sliderPosition->value());
    if (secondLyr != nullptr)
        secondLyr->setSeekPositionCursor(ui->sliderPosition->value());
    onPlayerPositionMSChanged(player->positionMs());

    // Seek beat
    ui->rhmWidget->setCurrentBeat(player->currentBeat());

    if (playAfterSeek) resume();
}

void MainWindow::on_btnVolumeMute_clicked()
{
    if (ui->sliderVolume->isEnabled()) {
        ui->sliderVolume->setEnabled(false);
        ui->btnVolumeMute->setIconFiles(":/Icons/volume-mute-white", ":/Icons/volume-mute-blue");
    } else {
        ui->sliderVolume->setEnabled(true);
        ui->btnVolumeMute->setIconFiles(":/Icons/volume-white", ":/Icons/volume-blue");
    }
    applyVolume();
}

void MainWindow::on_btnPlay_clicked()
{
    if (mp3Stream) {
        if (!mp3Timer->isActive())
            resume();
    } else if (videoActive && videoPlayer->state() == QMediaPlayer::PausedState) {
        resume();
    } else if (videoActive) {
        return;
#ifdef PTHK_WEBENGINE
    } else if (ytActive) {
        if (ytPlayer->isPaused())
            resume();
#endif
    } else if (player->isPlayerPaused()) {
        resume();
    } else {
        if (playingSong.id() != "")
            play(-1);
    }
}


void MainWindow::onSliderVolumeValueChanged(int value)
{
    Q_UNUSED(value);
    applyVolume();

    ui->detail->setDetail(tr("ระดับเสียง"), QString::number(value));
    ui->detail->show();
    detailTimer->start(3000);
    if (this->width() < 1160 && ui->chMix->isVisible()) {
        ui->lcdTime->hide();
    }
}

void MainWindow::switchMedleyLyrics()
{
    lyrWidget->switchToLyricsTemp();
    lyrWidget->setSeekPositionCursor(player->positionTick());
    lyrWidget->update();
    if (!lyricsTimer->isActive())
        lyricsTimer->start();
}

void MainWindow::switchMedleyLyrics2()
{
    if (secondLyr != nullptr) {
        secondLyr->switchToLyricsTemp();
        secondLyr->setSeekPositionCursor(player->positionTick());
        secondLyr->update();
    }
}

void MainWindow::onNextMedleyStarted()
{
    const int index = (remove_playlist ? 0 : playingIndex + 1);

    if (index >= 0 && index < playlist.size()) {
        playingSong = *playlist[index];
        if (remove_playlist) {
            delete playlist.takeAt(index);
            ui->playlistWidget->removeRow(index);
            playingIndex = -1;
        } else {
            playingIndex = index;
        }
    }

    // 1. Cancel any active fade-out and stale finish callbacks from previous song
    if (fadeTimer && fadeTimer->isActive())
        fadeTimer->stop();
    fadeDone = nullptr;
    fadeGain = 1.0;
    endingSong = false;
    applyVolume();

    // 2. Reset lyric completion tracking for the new song
    lastLyricPos = -2;
    afterLyrics = false;

    // 3. Reset UI & timers
    positionTimer->stop();

    onPlayerDurationTickChanged(player->durationTick());
    onPlayerDurationMSChanged(player->durationMs());
    ui->sliderPosition->setValue(0);
    onPlayerPositionMSChanged(player->positionMs());

    #ifdef _WIN32
    taskbarButton->progress()->setValue(0);
    taskbarButton->progress()->setMaximum(player->durationTick());
    taskbarButton->progress()->resume();
    taskbarButton->progress()->show();
    #endif

    ui->rhmWidget->setBeat(MidiHelper::calculateBeats(player->midiFile()), player->beatCount());

    positionTimer->start();

    if (!lyricsTimer->isActive())
        lyricsTimer->start();

    resetScore();
    scoreTimer->start();
    dmxLights->setPlaying(true);
    db->addHistory(&playingSong);

    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->chMix->hide();
    ui->expandChMix->hide();

    // SongDetail
    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();
    ui->songDetail->show();
    timer2->start(songDetail_timeout);
}

void MainWindow::onNextMedleyAfterStarted()
{
    if (!lyricsTimer->isActive())
        lyricsTimer->start();

    const int nextIdx = (remove_playlist ? 0 : playingIndex + 1);
    if (nextIdx >= 0 && nextIdx < playlist.size()) {
        loadNextMedley(playlist[nextIdx]);
    } else {
        player->unloadNextMedley();
    }
}

void MainWindow::onMedleyCountIn(int beat, int beatsPerBar, int bar, int totalBars)
{
    ui->rhmWidget->setCurrentBeat(beat);
    QString countStr;
    for (int b = 1; b <= beatsPerBar; ++b) {
        if (b == beat)
            countStr += QString(" [ %1 ] ").arg(b);
        else
            countStr += QString(" %1 ").arg(b);
    }
    const int barsLeft = qMax(1, totalBars - bar + 1);
    const QString next = nextMedleyTitle.isEmpty() ? tr("เพลงถัดไป") : nextMedleyTitle;
    ui->detail->setDetail(tr("ลูกส่ง: อีก %1 ห้อง %2 เข้าเพลง %3").arg(barsLeft).arg(QChar(0x2192)).arg(next),
                          tr("ห้อง %1/%2   จังหวะ: %3").arg(bar).arg(totalBars).arg(countStr.trimmed()));
    ui->detail->show();
    detailTimer->start(1200);
}

void MainWindow::onPlayerThreadFinished()
{
    if (player->isUseMedley() && medleyLoader != nullptr && medleyLoader->isRunning()) {
        medleyLoader->wait();
        if (player->midiSequencerTemp() != nullptr) {
            player->onSeqFinished();
            return;
        }
    }
    if (player->isPlayerFinished()) {
        playNext();
    }
}

void MainWindow::onDbUpdateChanged(int v)
{
    int p = (100 * v / db->updateCount());
    if (p < 0) p = 0;
    updateDetail->setValue(QString::number(p) + "%");
}

void MainWindow::onDetailTimerTimeout()
{
     ui->detail->hide();
     ui->lcdTime->show();
}

void MainWindow::addBpmSpeed(int speed)
{
    // Shift + PageUp/PageDown = 5 BPM steps.
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
        speed *= 5;

    const int oldBpm = player->currentBpm();
    int bpm = oldBpm + speed;
    int bpmSp = player->bpmSpeed() + speed;

    player->setBpmSpeed(bpmSp);

    // Medley with a locked tempo: the change becomes the new lock, so the following
    // songs keep the tempo the operator just chose (scaled, so a half-time ballad
    // nudged by 1 BPM moves the lock by 2).
    if (player->isUseMedley() && player->medleyBPM() > 0 && oldBpm > 0) {
        const int newLock = qBound(40, int(player->medleyBPM() * double(bpm) / oldBpm + 0.5), 250);
        if (newLock != player->medleyBPM()) {
            player->setMedleyBPM(newLock);
            player->saveMedleySettings();
            // Re-prepare the queued next song with the new tempo (debounced while keys repeat).
            if (!medleyRetempoTimer) {
                medleyRetempoTimer = new QTimer(this);
                medleyRetempoTimer->setSingleShot(true);
                connect(medleyRetempoTimer, &QTimer::timeout, this, [this]() {
                    const int nextIdx = (remove_playlist ? 0 : playingIndex + 1);
                    // Not while the next song is already starting (its bridge is playing).
                    const bool nextStarted = player->midiSequencerTemp() != nullptr
                                             && player->midiSequencerTemp()->isRunning();
                    if (player->isUseMedley() && !player->isPlayerStopped() && !nextStarted
                        && nextIdx >= 0 && nextIdx < playlist.size())
                        loadNextMedley(playlist[nextIdx]);
                });
            }
            medleyRetempoTimer->start(700);
        }
    }

    if (playingSong.id() != "")
        playingSong.setBpmSpeed(bpmSp);
    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();

    QString value = QString::number(bpm);
    if (bpmSp != 0) {
        value += bpmSp > 0 ? " (+" : " (";
        value += QString::number(bpmSp) + ")";
    }

    if (player->isUseMedley() && player->medleyBPM() > 0)
        value += tr("  • เมดเลย์ ") + QString::number(player->medleyBPM());

    ui->detail->setDetail(tr("ความเร็ว"), value);
    ui->detail->show();
    detailTimer->start(3000);
    if (this->width() < 1160 && ui->chMix->isVisible()) {
        ui->lcdTime->hide();
    }
}

void MainWindow::preSetBpmSpeed(int speed)
{
    Song *song = db->currentSong();
    song->setBpmSpeed(speed);
    searchBoxChangeBpm = true;

    QString s;
    if (speed > 0)
        s = "+" + QString::number(speed);
    else
        s = QString::number(speed);

    ui->lbSearch->setText(tr("ความเร็ว : ") + QString::number(song->tempo() + speed) + " (" + s + ")");
    timer2->start(search_timeout);
}

void MainWindow::preSetTranspose(int transpose)
{
    Song *song = db->currentSong();
    song->setTranspose(transpose);
    searchBoxChangeBpm = true;

    QString s;
    if (transpose > 0)
        s = "+" + QString::number(transpose);
    else
        s = QString::number(transpose);


    ui->lbSearch->setText(tr("คีย์เพลง : ") + s );
    timer2->start(search_timeout);
}

void MainWindow::sendDrumPads(QKeyEvent *key, bool noteOn)
{
    MidiEvent e;
    e.setChannel(9);

    if (noteOn) {
        e.setData2(100);
        e.setEventType(MidiEventType::NoteOn);
    } else {
        e.setData2(0);
        e.setEventType(MidiEventType::NoteOff);
    }

    switch (key->key()) {
        case DrumPadsKey::bassDrum:
            e.setData1(36);
            player->sendEvent(e);
            break;
        case DrumPadsKey::snare:
            e.setData1(38);
            player->sendEvent(e);
            break;
        case DrumPadsKey::hihatClose:
            e.setData1(42);
            player->sendEvent(e);
            break;
        case DrumPadsKey::hihatOpen:
            e.setData1(46);
            player->sendEvent(e);
            break;
        case DrumPadsKey::cowbell:
            e.setData1(56);
            player->sendEvent(e);
            break;
        case DrumPadsKey::tambourine:
            e.setData1(54);
            player->sendEvent(e);
            break;
        case DrumPadsKey::tom1:
            e.setData1(50);
            player->sendEvent(e);
            break;
        case DrumPadsKey::tom2:
            e.setData1(48);
            player->sendEvent(e);
            break;
        case DrumPadsKey::crashCymbal:
            e.setData1(49);
            player->sendEvent(e);
            break;
        default:
            break;
    }
}

void MainWindow::savePlaylist()
{
    if (playlist.count() == 0)
        return;

    QString fileName = QFileDialog::getSaveFileName(this, tr("บันทึกรายการเล่น"),
                                                    Utils::LAST_OPEN_DIR,
                                                    APP_PLAYLIST_FILTER_TEXT);

    if (fileName == "")
        return;

    Utils::LAST_OPEN_DIR = QFileInfo(fileName).dir().absolutePath();

    if (!Utils::savePlaylist(fileName, playlist))
    {
        QMessageBox::critical(this, tr("บันทึกรายการเล่นล้มเหลว"),
                              tr("ไม่สามารถบันทึกรายการเล่นได้\nโปรดลองอีกครั้ง"));
    }
}

void MainWindow::loadPlaylist()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("เปิดรายการเล่น"),
                                                    Utils::LAST_OPEN_DIR,
                                                    APP_PLAYLIST_FILTER_TEXT);

    if (fileName == "")
        return;

    Utils::LAST_OPEN_DIR = QFileInfo(fileName).dir().absolutePath();

    QList<Song*> songs;
    if (Utils::loadPlaylist(fileName, songs))
    {
        for (Song *s : playlist)
            delete s;
        playlist.clear();
        playlist = songs;
        ui->playlistWidget->setPlaylist(playlist);
        playingIndex = -1;
    }
    else
    {
        QMessageBox::critical(this, tr("เปิดรายการเล่นล้มเหลว"),
                              tr("ไม่สามารถเปิดรายการเล่นได้\nโปรดลองอีกครั้ง"));
    }
}

// Drum hold ("ค้างจังหวะกลอง"): MIDI songs only (audio songs have no separate drums)
// Drum hold banner: a blinking orange strip at the top of the screen, so nobody forgets
// the band is waiting. 1 = stopping at the next bar, 2 = holding, 3 = fill, 0 = off.
void MainWindow::showDrumHoldBanner(int state)
{
    if (state == 0) {
        if (drumHoldBlink) drumHoldBlink->stop();
        if (drumHoldBanner) drumHoldBanner->hide();
        return;
    }
    if (!drumHoldBanner) {
        drumHoldBanner = new QLabel(this);
        drumHoldBanner->setAlignment(Qt::AlignCenter);
        drumHoldBanner->setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont f = drumHoldBanner->font();
        f.setPointSize(16);
        f.setBold(true);
        drumHoldBanner->setFont(f);
        drumHoldBlink = new QTimer(this);
        drumHoldBlink->setInterval(500);
        connect(drumHoldBlink, &QTimer::timeout, this, [this]() {
            const bool on = drumHoldBanner->property("lit").toBool();
            drumHoldBanner->setProperty("lit", !on);
            drumHoldBanner->setStyleSheet(QString("QLabel{color:#111;border-radius:10px;padding:8px 22px;"
                                                  "background:%1;border:2px solid #fde68a;}")
                                          .arg(on ? "rgba(245,158,11,0.55)" : "rgba(245,158,11,0.95)"));
        });
    }
    QString text;
    if (state == 1)
        text = tr("ค้างกลอง: ดนตรีจะหยุดที่ต้นห้องถัดไป");
    else if (state == 2)
        text = tr("ค้างจังหวะกลองอยู่  |  กดปุ่ม \"ส่งเข้าเพลง\" หรือ Ctrl+D เพื่อลูกส่งเข้าเพลงต่อ");
    else
        text = tr("ลูกส่ง... เข้าเพลงต่อในห้องถัดไป");
    drumHoldBanner->setText(text);
    drumHoldBanner->adjustSize();
    drumHoldBanner->move((width() - drumHoldBanner->width()) / 2, qMax(8, height() / 12));
    drumHoldBanner->setProperty("lit", true);   // first frame: bright
    drumHoldBanner->setStyleSheet("QLabel{color:#111;border-radius:10px;padding:8px 22px;"
                                  "background:rgba(245,158,11,0.95);border:2px solid #fde68a;}");
    drumHoldBanner->show();
    drumHoldBanner->raise();
    if (state == 2)
        drumHoldBlink->start();
    else
        drumHoldBlink->stop();         // steady while stopping / during the fill
}

void MainWindow::toggleDrumHold()
{
    if (videoActive || ytActive || mp3Stream || !player->isPlayerPlaying()) {
        ui->detail->setDetail(tr("ค้างจังหวะกลอง"), tr("ใช้ได้กับเพลง MIDI ที่กำลังเล่นเท่านั้น"));
        ui->detail->show();
        detailTimer->start(2500);
        return;
    }
    player->toggleDrumHold();
}

void MainWindow::startSibBackfill()
{
    if (!db || db->sibPath().isEmpty() || (sibFill && sibFill->running()))
        return;
    {
        QSqlQuery q(*db->database());
        if (!q.exec("SELECT 1 FROM songs WHERE songtype = 'SIB' AND (lyrics IS NULL OR lyrics = '') LIMIT 1") || !q.next())
            return;     // nothing to fill
    }
    delete sibFill;
    sibFill = new SibBackfill();
    // works silently in the background (no messages on screen)
    sibFill->onProgress = nullptr;
    sibFill->onFinished = nullptr;
    sibFill->start(db->database()->databaseName(), db->sibPath());
}

// The AI separation thread (see AiStems.h), made on first use, with the model chosen now.
void MainWindow::ensureAiStems()
{
    if (!aiStems) {
        aiStems = new AiStemMaker();
        aiStems->onDone = [this](const QString &f, bool ok, const QString &err) {
            QMetaObject::invokeMethod(this, [this, f, ok, err]() {
                const QString name = QFileInfo(f).completeBaseName();
                uiLog(QString("AI stems %1: %2 %3").arg(ok ? "done" : "failed", name, err));
                if (ok && QFileInfo(videoAudioFile) == QFileInfo(f))
                    return;                     // the playing song: its AI sound is already heard
                ui->detail->setDetail(ok ? tr("AI แยกเสียงร้องเสร็จ") : tr("AI แยกเสียงร้องไม่สำเร็จ"),
                                      ok ? tr("%1 — ตัดเสียงร้องได้สะอาดทันทีเมื่อเล่นเพลงนี้").arg(name)
                                         : tr("%1 — %2").arg(name, err));
                ui->detail->show();
                detailTimer->start(6000);
            }, Qt::QueuedConnection);
        };
    }
    aiStems->configure(QFileInfo(YtTools::toolsDir()).absolutePath() + "/models",
                       settings->value("YtAiModel", "hq").toString(),
                       settings->value("YtAiDevice", 0).toInt() == 2);     // CPU unless "GPU" was chosen
    updateAiStemsBackground();
}

// AI separation of a song file in the background (e.g. a clip just downloaded)
void MainWindow::enqueueAiStems(const QString &file)
{
    if (file.isEmpty() || AiStemMaker::hasStems(file))
        return;
    ensureAiStems();
    aiStems->enqueue(file);
}

// Background separation takes CPU: only while no video / YouTube song plays (a song playing with
// the vocal cut on is separated anyway, from its play position on).
void MainWindow::updateAiStemsBackground()
{
    if (aiStems)
        aiStems->setBackgroundAllowed(!videoActive && !ytActive);
}

// At start: downloaded clips whose separation is not finished go on in the background.
void MainWindow::resumeAiStems()
{
    if (!db)
        return;
    const QString models = QFileInfo(YtTools::toolsDir()).absolutePath() + "/models";
    const QString model = settings->value("YtAiModel", "hq").toString();
    if (!QFile::exists(models + "/" + AiVocalEngine::model(model)->file))
        return;                                 // the AI was never used: nothing to go on with
    QString dir = QDir::fromNativeSeparators(db->videoPath());
    while (dir.endsWith('/'))
        dir.chop(1);
    const QFileInfoList files = QDir(dir + "/YouTube").entryInfoList({ "*.mp4", "*.mkv", "*.webm", "*.mov" }, QDir::Files);
    for (const QFileInfo &fi : files)
        if (!AiStemMaker::hasStems(fi.absoluteFilePath()))
            enqueueAiStems(fi.absoluteFilePath());
}

// Video / MP3 song sound through the BASS main mixer: vocal cut (AI parts + fast cut, see
// StemMixer in AiStems.h), key change, master volume and equalizer.
bool MainWindow::startVideoAudioStream(const QString &path)
{
    stopVideoAudioStream();
    videoPitch = 0;

    MidiSynthesizer *synth = player ? player->midiSynthesizer() : nullptr;
    const DWORD mixer = (synth && synth->isOpened()) ? synth->mainMixerHandle() : 0;
    if (!mixer)
        return false;

    videoAudioStream = BASS_StreamCreateFile(FALSE, path.utf16(), 0, 0,
                                             BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
    if (!videoAudioStream)
        return false;

    videoAudioTempo = BASS_FX_TempoCreate(videoAudioStream, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_FX_FREESOURCE);
    if (!videoAudioTempo) {
        BASS_StreamFree(videoAudioStream);
        videoAudioStream = 0;
        return false;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(videoAudioStream, &info);
    const int freq = info.freq ? int(info.freq) : 44100;

    if (info.chans == 2) {
        // on the song's own sound (before the key change), which knows its position: the AI parts
        // are read from the stems file at the same place
        ensureAiStems();
        videoStemMixer = new StemMixer(aiStems->track(path), freq);
        BASS_ChannelSetDSP(videoAudioStream, &MainWindow::videoVocalDspCallback, this, 0);
        BASS_ChannelSetSync(videoAudioStream, BASS_SYNC_SETPOS | BASS_SYNC_MIXTIME, 0, &MainWindow::videoSetPosSync, this);
    }
    videoAudioFile = path;
    aiCutNoticeShown = false;

    if (!BASS_Mixer_StreamAddChannel(mixer, videoAudioTempo, BASS_MIXER_CHAN_NORAMPIN | BASS_MIXER_CHAN_PAUSE)) {
        BASS_StreamFree(videoAudioTempo);
        videoAudioTempo = videoAudioStream = 0;
        delete videoStemMixer;
        videoStemMixer = nullptr;
        videoAudioFile.clear();
        return false;
    }

    setVideoAudioVolume();
    applyVideoVocalCut();
    return true;
}

void MainWindow::stopVideoAudioStream()
{
    if (videoAudioTempo) {
        MidiSynthesizer *synth = player ? player->midiSynthesizer() : nullptr;
        const DWORD mixer = (synth && synth->isOpened()) ? synth->mainMixerHandle() : 0;
        if (mixer)
            BASS_Mixer_ChannelRemove(videoAudioTempo);
        BASS_StreamFree(videoAudioTempo);       // frees videoAudioStream and its DSP too
        videoAudioTempo = 0;
        videoAudioStream = 0;
    }
    delete videoStemMixer;
    videoStemMixer = nullptr;
    videoAudioFile.clear();
    if (aiStems)
        aiStems->clearFocus();
}

void MainWindow::videoVocalDspCallback(HDSP, DWORD, void *buffer, DWORD length, void *user)
{
    MainWindow *self = static_cast<MainWindow *>(user);
    if (self && self->videoStemMixer)
        self->videoStemMixer->process(static_cast<float *>(buffer), int(length / (2 * sizeof(float))));
}

// the song's sound was moved (seek / resync with the picture): the stems follow
void MainWindow::videoSetPosSync(HSYNC, DWORD channel, DWORD, void *user)
{
    MainWindow *self = static_cast<MainWindow *>(user);
    if (self && self->videoStemMixer) {
        const QWORD pos = BASS_ChannelGetPosition(channel, BASS_POS_BYTE);
        if (pos != QWORD(-1))
            self->videoStemMixer->setPosition((long long)(pos / (2 * sizeof(float))));
    }
}

void MainWindow::applyVideoVocalCut()
{
    if (!videoStemMixer)
        return;
    const bool cut = ui->chMix->isMuteVoice();
    const bool aiMode = settings->value("YtCutAi", false).toBool();
    videoStemMixer->setLevels(settings->value("YtVocalLevel", 0).toInt() / 100.0f,
                              settings->value("YtMusicLevel", 100).toInt() / 100.0f);
    videoStemMixer->setFallbackFast(!aiMode);   // AI mode: the voice stays until the AI is ready (no echo)
    videoStemMixer->setCut(cut);
    if (audioMeter)
        audioMeter->setStemVocalGain(1.0f);
    ensureAiStems();                            // takes a newly chosen model for new work
    if (!aiPrepareVideo()) {
        aiStems->clearFocus();                  // no AI work for this song
        return;
    }
    // the windows from the play position on are made first (the cut button shows how far it is)
    if (!videoStemMixer->stems()->complete.load())
        aiStems->setFocus(videoAudioFile, videoAudioPositionMs() / 1000.0);
    updateCutButton();
}

// The AI separates the playing video / MP3 song: AI mode, and the cut is on or "prepare from the
// start of the song" is set (then the cut is ready by the time it is wanted).
bool MainWindow::aiPrepareVideo() const
{
    return videoStemMixer && settings->value("YtCutAi", false).toBool()
           && (ui->chMix->isMuteVoice() || settings->value("YtAiPreStart", true).toBool());
}

// The cut button tells whether the AI cut is ready: "รอ AI 45%" while downloading / separating,
// "ตัดเสียงร้อง ✓" when the AI sound is there at the play position.
void MainWindow::updateCutButton()
{
    if (!btnMuteVocal)
        return;
    QString text = tr("ตัดเสียงร้อง");
    QString tip = tr("ปิด / เปิด เสียงร้องนำ (*)");
    if (ytActive && ytFetchOnline) {
        text = tr("รอ AI %1%").arg(ytFetchPct);
        tip = tr("กำลังโหลดเพลงลงเครื่องเพื่อตัดเสียงร้องด้วย AI (%1%)\n"
                 "กดไว้ได้เลย: เมื่อพร้อมจะตัดเสียงร้องให้เอง").arg(ytFetchPct);
    } else if (videoStemMixer && settings->value("YtCutAi", false).toBool()) {
        const std::shared_ptr<StemTrack> t = videoStemMixer->stems();
        BASS_CHANNELINFO ci;
        const double rate = (videoAudioStream && BASS_ChannelGetInfo(videoAudioStream, &ci) && ci.freq) ? ci.freq : 44100.0;
        const long long f = (long long)(videoStemMixer->position() * AiVocalEngine::SR / rate);
        if (t->complete.load() || (t->frameReady(f) && t->frameReady(f + 3LL * AiVocalEngine::SR))) {
            text = tr("ตัดเสียงร้อง ✓");
            tip = tr("AI พร้อมตัดเสียงร้องตรงนี้แล้ว (*)");
        } else if (aiPrepareVideo()) {
            const int pct = (t->ready.load() && t->nWin > 0) ? t->doneCount.load() * 100 / t->nWin : 0;
            text = tr("รอ AI %1%").arg(pct);
            tip = tr("AI กำลังแยกเสียงร้อง (ทำไปแล้ว %1% ของเพลง)\n"
                     "กดไว้ได้เลย: เมื่อ AI พร้อมตรงที่เล่นอยู่จะตัดเสียงร้องให้เอง").arg(pct);
        }
    }
    if (btnMuteVocal->text() != text)
        btnMuteVocal->setText(text);
    if (btnMuteVocal->toolTip() != tip)
        btnMuteVocal->setToolTip(tip);
}

void MainWindow::setVideoAudioVolume()
{
    if (videoAudioTempo) {
        const float v = float(fadeGain) * (float(effectiveVolume()) / 100.0f);
        BASS_ChannelSetAttribute(videoAudioTempo, BASS_ATTRIB_VOL, v);
    }
}

void MainWindow::changeVideoKey(int step)
{
    if (videoAudioTempo) {
        videoPitch = qBound(-12, videoPitch + step, 12);
        BASS_ChannelSetAttribute(videoAudioTempo, BASS_ATTRIB_TEMPO_PITCH, float(videoPitch));
        ui->detail->setDetail(tr("คีย์เพลง "), videoPitch > 0 ? "+" + QString::number(videoPitch) : QString::number(videoPitch));
        ui->detail->show();
        detailTimer->start(3000);
    }
}

qint64 MainWindow::videoAudioPositionMs()
{
    if (!videoAudioTempo)
        return 0;
    QWORD pos = BASS_Mixer_ChannelGetPosition(videoAudioTempo, BASS_POS_BYTE);
    return qint64(BASS_ChannelBytes2Seconds(videoAudioTempo, pos) * 1000);
}
