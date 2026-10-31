// MainWindow: DMX lights, recording, score, video/MP3 playback, pause/resume/stop, window events
#include "MainWindow_p.h"

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
        const bool bassAudio = startVideoAudioStream(path);
        if (bassAudio) {
            videoPlayer->setMuted(true);
            videoPlayer->setVolume(0);
        } else {
            videoPlayer->setMuted(false);
            videoPlayer->setVolume(effectiveVolume());
        }
        videoPlayer->setMedia(QUrl::fromLocalFile(path));
        videoPlayer->play();
    }
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
}

void MainWindow::playNext()
{
    if (playlist.count() > 0 && playingIndex < playlist.count()-1) {
        play(playingIndex+1);
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
    updateDetail->move(width() - 260, 70);
    if (reqToast && reqToast->isVisible())
        reqToast->move(width() - reqToast->width() - 16, 16);
    emit resized(event->size());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
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
