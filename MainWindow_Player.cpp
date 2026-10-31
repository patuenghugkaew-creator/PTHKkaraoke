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
        ytPlayer->seek(ui->sliderPosition->value(), true);
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
    ui->detail->setDetail(tr("เมดเล่ย์: ลูกส่งรอเพลงใหม่ (ห้อง %1/%2)").arg(bar).arg(totalBars),
                          tr("เพลง: %1 - จังหวะ: %2").arg(playingSong.name(), countStr.trimmed()));
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

// AI vocal separation of a video song on disk, in the background (see AiStems.h).
void MainWindow::enqueueAiStems(const QString &video)
{
    if (video.isEmpty() || AiStemMaker::hasStems(video))
        return;
    if (!aiStems) {
        aiStems = new AiStemMaker();
        aiStems->onProgress = [this](const QString &v, int pct) {
            static int lastShown = -100;
            if (pct != 0 && pct != 100 && pct - lastShown < 10)
                return;
            lastShown = pct;
            const QString name = QFileInfo(v).completeBaseName();
            QMetaObject::invokeMethod(this, [this, name, pct]() {
                ui->detail->setDetail(tr("AI แยกเสียงร้อง (ทำครั้งเดียว)"), tr("%1  %2%").arg(name).arg(pct));
                ui->detail->show();
                detailTimer->start(4000);
            }, Qt::QueuedConnection);
        };
        aiStems->onDone = [this](const QString &v, bool ok, const QString &err) {
            QMetaObject::invokeMethod(this, [this, v, ok, err]() {
                const QString name = QFileInfo(v).completeBaseName();
                ui->detail->setDetail(ok ? tr("AI แยกเสียงร้องเสร็จ") : tr("AI แยกเสียงร้องไม่สำเร็จ"),
                                      ok ? tr("%1 — ตัดเสียงร้องได้ทันทีเมื่อเล่นเพลงนี้").arg(name)
                                         : tr("%1 — %2").arg(name, err));
                ui->detail->show();
                detailTimer->start(6000);
            }, Qt::QueuedConnection);
        };
    }
    const QString models = QFileInfo(YtTools::toolsDir()).absolutePath() + "/models";
    const QString key = settings->value("YtAiModel", "hq").toString();
    const bool gpu = settings->value("YtAiDevice", 0).toInt() == 2;   // CPU unless "GPU" was chosen
    aiStems->enqueue(video, models, key, gpu);
    ui->detail->setDetail(tr("AI แยกเสียงร้อง (ทำครั้งเดียว)"),
                          tr("%1  เริ่ม...").arg(QFileInfo(video).completeBaseName()));
    ui->detail->show();
    detailTimer->start(4000);
}

// Video audio routed through BASS main mixer: enables Vocal Cut (center cancellation DSP),
// Pitch transpose, Master volume, and Equalizer on local / downloaded YouTube video songs.
bool MainWindow::startVideoAudioStream(const QString &path)
{
    stopVideoAudioStream();
    videoPitch = 0;

    MidiSynthesizer *synth = player ? player->midiSynthesizer() : nullptr;
    const DWORD mixer = (synth && synth->isOpened()) ? synth->mainMixerHandle() : 0;
    if (!mixer)
        return false;

    // AI stems prepared ahead of time (see AiStems.h): 4 channels music L/R + vocals L/R,
    // used instead of the video's own sound; the vocal cut is a mixing matrix (instant).
    videoStemMode = false;
    if (AiStemMaker::hasStems(path)) {
        const QString stem = AiStemMaker::stemPath(path);
        videoAudioStream = BASS_StreamCreateFile(FALSE, stem.utf16(), 0, 0,
                                                 BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
        BASS_CHANNELINFO si;
        if (videoAudioStream && BASS_ChannelGetInfo(videoAudioStream, &si) && si.chans == 4) {
            videoStemMode = true;
        } else if (videoAudioStream) {
            BASS_StreamFree(videoAudioStream);
            videoAudioStream = 0;
        }
    }
    if (!videoAudioStream)
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

    delete videoVocalRemover;
    videoVocalRemover = nullptr;
    if (info.chans == 2 && !videoStemMode) {
        videoVocalRemover = new VocalRemover(freq);
        BASS_ChannelSetDSP(videoAudioTempo, &MainWindow::videoVocalDspCallback, this, 0);
    }

    {
        BASS_CHANNELINFO mi;
        videoMixerChans = BASS_ChannelGetInfo(mixer, &mi) ? int(mi.chans) : 2;
    }
    videoStemMatrixSet = false;
    const DWORD addFlags = BASS_MIXER_CHAN_NORAMPIN | BASS_MIXER_CHAN_PAUSE
                           | (videoStemMode ? BASS_MIXER_CHAN_MATRIX : 0);
    if (!BASS_Mixer_StreamAddChannel(mixer, videoAudioTempo, addFlags)) {
        BASS_StreamFree(videoAudioTempo);
        videoAudioTempo = videoAudioStream = 0;
        delete videoVocalRemover;
        videoVocalRemover = nullptr;
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
        BASS_StreamFree(videoAudioTempo);
        videoAudioTempo = 0;
        videoAudioStream = 0;
    }
    delete videoVocalRemover;
    videoVocalRemover = nullptr;
    videoStemMode = false;
}

void MainWindow::videoVocalDspCallback(HDSP, DWORD, void *buffer, DWORD length, void *user)
{
    MainWindow *self = static_cast<MainWindow *>(user);
    if (self && self->videoVocalRemover) {
        self->videoVocalRemover->process(static_cast<float *>(buffer), length / (2 * sizeof(float)));
    }
}

void MainWindow::applyVideoVocalCut()
{
    if (videoStemMode && videoAudioTempo) {
        // music L/R = in 0/1, vocals L/R = in 2/3; same vocal / music levels as the YouTube AI
        const bool cut = ui->chMix->isMuteVoice();
        const float gV = cut ? settings->value("YtVocalLevel", 0).toInt() / 100.0f : 1.0f;
        const float gM = cut ? settings->value("YtMusicLevel", 100).toInt() / 100.0f : 1.0f;
        if (audioMeter)
            audioMeter->setStemVocalGain(gV);
        // The matrix must be [mixer outputs][4]: the main mixer has 8 outputs (multi-speaker
        // routing), and a 2x4 matrix made BASS read past it (noise, then silence).
        // Front L/R carry the song, like a stereo channel added without a matrix.
        const int outs = qMax(2, videoMixerChans);
        std::vector<float> mtx(size_t(outs) * 4, 0.0f);
        mtx[0 * 4 + 0] = gM; mtx[0 * 4 + 2] = gV;     // out L = music L + vocals L
        mtx[1 * 4 + 1] = gM; mtx[1 * 4 + 3] = gV;     // out R = music R + vocals R
        if (videoStemMatrixSet)
            BASS_Mixer_ChannelSetMatrixEx(videoAudioTempo, mtx.data(), 0.08f);   // short ramp: no click
        else
            BASS_Mixer_ChannelSetMatrix(videoAudioTempo, mtx.data());   // first time: at once, so no
        videoStemMatrixSet = true;                                    // sound starts on other outputs
        return;
    }
    if (videoVocalRemover) {
        const bool cut = ui->chMix->isMuteVoice();
        videoVocalRemover->setEnabled(cut);
    }
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
