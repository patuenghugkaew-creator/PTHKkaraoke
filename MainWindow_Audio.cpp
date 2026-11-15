// MainWindow: Volume, YouTube audio / AI vocal cut, fades, song endings and transitions
#include "MainWindow_p.h"

int MainWindow::effectiveVolume() const
{
    if (!ui->sliderVolume->isEnabled())
        return 0;
    const double idleScale = idlePlaying
        ? qBound(0.10, settings->value("IdleMusic/VolumePct", 45).toInt() / 100.0, 1.0)
        : 1.0;
    return qRound(ui->sliderVolume->value() * fadeGain * idleScale);
}

// every player at once: whichever is playing follows the volume slider and the fade
void MainWindow::applyVolume()
{
    const int v = effectiveVolume();
    const double idleScale = idlePlaying
        ? qBound(0.10, settings->value("IdleMusic/VolumePct", 45).toInt() / 100.0, 1.0)
        : 1.0;
    // MIDI: the slider volume only when it changes (it also goes to the MIDI out ports);
    // the fade itself only to the built-in synth, many times a second
    const int base = ui->sliderVolume->isEnabled() ? ui->sliderVolume->value() : 0;
    if (base != midiBaseVolume) {
        midiBaseVolume = base;
        player->setVolume(base);
    }
    player->setFadeGain(float(fadeGain * idleScale));
    videoPlayer->setVolume(videoAudioTempo ? 0 : v);
    setVideoAudioVolume();
#ifdef PTHK_WEBENGINE
    if (ytPlayer)
        ytPlayer->setVolume(ytAudioLive() ? 100 : v);
#endif
#ifdef KM_HAVE_PROCLOOP
    if (ytAudio && ytAudio->running()) {
        const float ytVol = float(fadeGain) * (float(v) / 100.0f);
        ytAudio->fadeVolume(ytVol, 0);
    }
#endif
    setMp3Volume();

    // the song level meter takes back out what the volume did to the song before the mixer
    // (video / MP3 / YouTube are turned down on their own channel; MIDI after the meter)
    float src = 1.0f;
    if (ytAudioLive() || videoAudioTempo)
        src = float(fadeGain) * (float(v) / 100.0f);
    else if (mp3Stream)
        src = float(v) / 100.0f;
    SongLevel::get().setSourceGain(src);
}

bool MainWindow::ytAudioLive() const
{
#ifdef KM_HAVE_PROCLOOP
    return ytAudio && ytAudio->running();
#else
    return false;
#endif
}

// YouTube sound into the main mixer: once the clip plays, find the browser's audio process and
// capture it (the browser itself is turned down to inaudible; see YtAudio.h)
void MainWindow::updateYtAudio()
{
#ifdef KM_HAVE_PROCLOOP
    const bool wasLive = ytAudioLive();
    if (ytAudioLive()) {
        if (!ytQuietClock.isValid() || ytQuietClock.elapsed() > 1000) {
            ytQuietClock.start();
            ytAudio->keepQuiet();               // also a new audio session of the next clip
        }
    } else if (!ytAudioTry.isValid() || ytAudioTry.elapsed() > 2000) {
        ytAudioTry.start();
        MidiSynthesizer *synth = player->midiSynthesizer();
        const DWORD mixer = synth->isOpened() ? synth->mainMixerHandle() : 0;
        const DWORD pid = ProcessLoopback::findAudioChild(GetCurrentProcessId());
        if (pid && mixer) {
            if (!aiEngine)
                aiEngine = PTHKCore::createAiVocalEngine(QFileInfo(YtTools::toolsDir()).absolutePath() + "/models");
            if (!ytAudio) {
                ytAudio = new YtAudio(aiEngine);
                if (ytActive)
                    ytAudio->setPitch(rememberedKey());     // the song's key (queue or remembered)
            }
            applyYtVocalCut();
            if (!ytAudio->start(mixer, pid)) {
                ytAudio->stop();
                if (!ytAudioWarned) {
                    ytAudioWarned = true;
                    ui->detail->setDetail(tr("YouTube "), tr("เสียงไม่ผ่านมิกเซอร์ในเครื่องนี้"));
                    ui->detail->show();
                    detailTimer->start(4000);
                }
            }
        } else if (pid) {
            ProcessLoopback::setSessionVolume(pid, 1.0f);   // no mixer: the browser is heard directly
        }
    }
    if (ytAudioLive() != wasLive)
        applyVolume();      // player at full level while the mixer has the sound, else back to the slider
#endif
}

// the vocal cut key / button and the settings. The AI needs ~6 s of sound before its first result, so
// once it was switched on it keeps running (the cut key then only changes the vocal / music levels,
// at once); until then, and in fast mode, YouTube plays without delay.
void MainWindow::applyYtVocalCut()
{
#ifdef KM_HAVE_PROCLOOP
    if (!ytAudio)
        return;
    // idle music keeps its singer (IdleMusic/KeepVocal) unless the operator pressed the cut during it
    const bool keepVocal = keepIdleVocal();
    const bool cut = ui->chMix->isMuteVoice() && !keepVocal;
    // while the copy for the AI downloads, the online song is not cut (it is, with the AI, from the copy)
    const bool ai = settings->value("YtCutAi", false).toBool() && !ytFetchOnline && !keepVocal;
    AiVocalEngine::setDevicePreference(settings->value("YtAiDevice", 0).toInt());
    if (ai)
        aiEngine->selectModel(settings->value("YtAiModel", "light").toString());
    ytAudio->setLevels(settings->value("YtVocalLevel", 0).toInt() / 100.0f,
                       settings->value("YtMusicLevel", 100).toInt() / 100.0f);
    const bool wasLatched = ytAiLatched;
    const bool preStart = settings->value("YtAiPreStart", true).toBool();
    if (!ai)
        ytAiLatched = false;
    else if (cut || preStart)
        ytAiLatched = true;
    if (ai && ytAiLatched && !wasLatched && aiEngine && aiEngine->ready()) {
        // First switch-on in this song: picture holds and sound is silent until the AI's
        // first result (one AI delay), then both continue from the same point.
        ui->detail->setDetail(tr("AI ตัดเสียงร้อง"),
                              tr("กำลังเตรียม รอประมาณ %1 วินาที").arg(aiEngine->delaySeconds(), 0, 'f', 0));
        ui->detail->show();
        detailTimer->start(int(aiEngine->delaySeconds() * 1000));
    }
    ytAudio->setVocalCut(cut && !ytFetchOnline);
    ytAudio->setAiMode(ai && ytAiLatched);
    if (!(ai && ytAiLatched))
        delayedFrames.clear();
#endif
}

void MainWindow::showYtVocalDialog()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("ตัดเสียงร้อง YouTube"));
    dlg.setMinimumWidth(460);



    QRadioButton *rFast = new QRadioButton(tr("เร็ว (แนะนำ) - เริ่มเล่นและตัดเสียงร้องทันทีตั้งแต่วินาทีแรก ไม่ค้าง เครื่องเบา"), &dlg);
    QRadioButton *rAi = new QRadioButton(tr("AI ไฮบริด - เริ่มด้วยโหมดเร็ว 6 วินาทีแรก แล้วสลับเป็น AI แยกเสียงร้องสะอาด"), &dlg);
    (settings->value("YtCutAi", false).toBool() ? rAi : rFast)->setChecked(true);

    QComboBox *model = new QComboBox(&dlg);
    int n = 0;
    const AiVocalEngine::ModelInfo *mi = AiVocalEngine::models(&n);
    const QString cur = settings->value("YtAiModel", "light").toString();
    for (int i = 0; i < n; i++) {
        model->addItem(QString::fromUtf8(mi[i].nameTh), QString(mi[i].key));
        if (cur == mi[i].key)
            model->setCurrentIndex(i);
    }

    auto slider = [&dlg](int v) {
        QSlider *s = new QSlider(Qt::Horizontal, &dlg);
        s->setRange(0, 100);
        s->setValue(v);
        return s;
    };
    QSlider *voc = slider(settings->value("YtVocalLevel", 0).toInt());
    QSlider *mus = slider(settings->value("YtMusicLevel", 100).toInt());
    mus->setRange(0, 200);              // music alone is quieter than the full song: allow a boost
    mus->setValue(settings->value("YtMusicLevel", 100).toInt());
    mus->setToolTip(tr("ดนตรีหลังตัดเสียงร้องมักเบาลง เลื่อนเกิน 100% เพื่อเร่งให้ดังเท่าเดิม (แนะนำ 120-140%)"));
    QLabel *vocL = new QLabel(&dlg), *musL = new QLabel(&dlg);
    auto showLevels = [=]() {
        vocL->setText(QString::number(voc->value()) + "%");
        musL->setText(QString::number(mus->value()) + "%");
    };
    showLevels();
    QCheckBox *delayPic = new QCheckBox(tr("หน่วงภาพให้ตรงกับเสียง (โหมด AI)"), &dlg);
    delayPic->setChecked(settings->value("YtDelayPicture", true).toBool());
    QCheckBox *preStart = new QCheckBox(tr("เตรียม AI ตั้งแต่เริ่มเพลง (รอครั้งเดียวตอนต้นเพลง กดตัดเสียงร้องแล้วได้ทันที)"), &dlg);
    preStart->setToolTip(tr("ติ๊ก: AI เริ่มทำงานตั้งแต่เพลง YouTube เริ่ม ภาพรอ 5-6 วินาทีตอนต้นเพลงแทนกลางเพลง\n"
                            "การ์ดจอ/CPU จะทำงานตลอดทุกเพลง YouTube แม้ไม่ได้กดตัดเสียงร้อง\n"
                            "ไม่ติ๊ก: AI เริ่มเมื่อกดตัดเสียงร้องครั้งแรก (ภาพรอตอนนั้น)"));
    preStart->setChecked(settings->value("YtAiPreStart", true).toBool());
    QLabel *status = new QLabel(&dlg);
    status->setWordWrap(true);
    status->setStyleSheet("color: gray;");

    // changes apply at once, so they can be heard while the song plays
    auto apply = [=]() {
        settings->setValue("YtCutAi", rAi->isChecked());
        settings->setValue("YtAiModel", model->currentData().toString());
        settings->setValue("YtVocalLevel", voc->value());
        settings->setValue("YtMusicLevel", mus->value());
        settings->setValue("YtDelayPicture", delayPic->isChecked());
        settings->setValue("YtAiPreStart", preStart->isChecked());
        showLevels();
        applyYtVocalCut();
        applyVideoVocalCut();           // the same levels / model for video songs
    };
    for (QRadioButton *r : { rAi, rFast })
        connect(r, &QRadioButton::toggled, &dlg, apply);
    connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, apply);
    connect(voc, &QSlider::valueChanged, &dlg, apply);
    connect(mus, &QSlider::valueChanged, &dlg, apply);
    connect(delayPic, &QCheckBox::toggled, &dlg, apply);
    connect(preStart, &QCheckBox::toggled, &dlg, apply);

    auto refresh = [=]() {
        QString t;
        if (!aiEngine || !ytAudio) {
            t = tr("เริ่มทำงานเมื่อเล่นเพลง YouTube");
        } else {
            t = tr("AI: ") + aiEngine->statusText();
            const double d = ytAudio->delaySeconds();
            if (d > 0)
                t += "\n" + tr("ภาพและเสียงช้ากว่าต้นฉบับ %1 วินาที").arg(d, 0, 'f', 1);
        }
        t += "\n" + tr("เปิด / ปิดการตัดเสียงร้อง: ปุ่ม \"ตัดเสียงร้อง\" หรือปุ่ม *  (ตอนนี้: %1)")
                 .arg(ui->chMix->isMuteVoice() ? tr("ตัดอยู่") : tr("ไม่ได้ตัด"));
        status->setText(t);
    };
    QTimer *tick = new QTimer(&dlg);
    connect(tick, &QTimer::timeout, &dlg, refresh);
    tick->start(500);
    refresh();

    QFormLayout *form = new QFormLayout();


    form->addRow(tr("วิธีตัด:"), rFast);
    form->addRow(QString(), rAi);
    form->addRow(tr("รุ่น AI:"), model);

    QComboBox *aiDev = new QComboBox(&dlg);
    aiDev->addItem(tr("อัตโนมัติ (แนะนำ - ใช้การ์ดจอถ้าเร็วกว่า)"), 0);
    aiDev->addItem(tr("CPU (ช้า - รุ่น hq อาจตามไม่ทัน)"), 1);
    aiDev->addItem(tr("การ์ดจอ (เร็ว ภาพไม่กระตุก)"), 2);
    aiDev->setCurrentIndex(qBound(0, settings->value("YtAiDevice", 0).toInt(), 2));
    connect(aiDev, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [this, aiDev]() {
        const int pref = aiDev->currentData().toInt();
        settings->setValue("YtAiDevice", pref);
        AiVocalEngine::setDevicePreference(pref);
        if (aiEngine)
            aiEngine->reloadModel();
    });
    form->addRow(tr("ประมวลผล AI ด้วย:"), aiDev);

    QComboBox *vqCombo = new QComboBox(&dlg);
    vqCombo->addItem(tr("720p HD (แนะนำ - คมชัด ลื่นไหล)"), "hd720");
    vqCombo->addItem(tr("1080p Full HD (คมชัดสูงสุด)"), "hd1080");
    vqCombo->addItem(tr("480p (ประหยัดเน็ต / เครื่องสเปคต่ำ)"), "large");
    vqCombo->addItem(tr("360p (เน็ตช้ามาก)"), "medium");
    const QString curVq = settings->value("YtVideoQuality", "hd720").toString();
    for (int i = 0; i < vqCombo->count(); i++) {
        if (vqCombo->itemData(i).toString() == curVq)
            vqCombo->setCurrentIndex(i);
    }
    connect(vqCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [this, vqCombo]() {
        settings->setValue("YtVideoQuality", vqCombo->currentData().toString());
    });
    form->addRow(tr("ความละเอียดภาพ YouTube:"), vqCombo);

    // YouTube picture drawn by the CPU: the AI (DirectML) keeps the only GPU busy for
    // ~0.3 s at a time; with a GPU-drawn player every picture copy had to wait for it.
    QCheckBox *swVideo = new QCheckBox(tr("วาดภาพ YouTube ด้วย CPU (ภาพไม่กระตุกตอนใช้ AI) — มีผลเมื่อเปิดโปรแกรมใหม่"), &dlg);
    swVideo->setChecked(settings->value("YtSoftwareVideo", true).toBool());
    connect(swVideo, &QCheckBox::toggled, &dlg, [this](bool on) { settings->setValue("YtSoftwareVideo", on); });
    form->addRow(QString(), swVideo);

    QHBoxLayout *presetRow = new QHBoxLayout();
    for (int cutPct : { 0, 25, 50, 75, 100 }) {
        QPushButton *pb = new QPushButton(QString::number(cutPct) + "%", &dlg);
        pb->setToolTip(tr("ตัดเสียงร้อง %1% (เหลือเสียงร้อง %2%)").arg(cutPct).arg(100 - cutPct));
        connect(pb, &QPushButton::clicked, &dlg, [voc, cutPct]() {
            voc->setValue(100 - cutPct);
        });
        presetRow->addWidget(pb);
    }
    form->addRow(tr("ปุ่มลัดสัดส่วนตัดเสียงร้อง:"), presetRow);
    QHBoxLayout *hv = new QHBoxLayout();
    hv->addWidget(voc, 1);
    hv->addWidget(vocL);
    form->addRow(tr("เสียงร้อง (ตอนตัด):"), hv);
    QHBoxLayout *hm = new QHBoxLayout();
    hm->addWidget(mus, 1);
    hm->addWidget(musL);
    form->addRow(tr("ดนตรี:"), hm);
    form->addRow(delayPic);
    form->addRow(preStart);
    QCheckBox *dlFirst = new QCheckBox(tr("โหมด AI: โหลดเพลง YouTube ลงเครื่องระหว่างเล่น แล้วตัดเสียงร้องด้วย AI จากไฟล์ (ภาพไม่ต้องรอ ไม่กระตุก ครั้งต่อไปเล่นจากเครื่องทันที)"), &dlg);
    dlFirst->setChecked(settings->value("YtDownloadFirst", true).toBool());
    connect(dlFirst, &QCheckBox::toggled, &dlg, [this](bool on) { settings->setValue("YtDownloadFirst", on); });
    form->addRow(dlFirst);

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addLayout(form);
    v->addWidget(status);
    v->addWidget(bb);
    dlg.exec();
    setFocus();
}

void MainWindow::changeYtKey(int step)
{
#ifdef KM_HAVE_PROCLOOP
    if (ytAudioLive()) {
        ytAudio->setPitch(qBound(-12, ytAudio->pitch() + step, 12));
        const int k = ytAudio->pitch();
        playingSong.setTranspose(k);
        rememberPlayingKey(k);
        ui->detail->setDetail(tr("คีย์เพลง "), k > 0 ? "+" + QString::number(k) : QString::number(k));
        ui->detail->show();
        detailTimer->start(3000);
        return;
    }
#endif
    Q_UNUSED(step);
    ui->detail->setDetail(tr("คีย์เพลง "), tr("YouTube เปลี่ยนคีย์ไม่ได้ในเครื่องนี้"));
    ui->detail->show();
    detailTimer->start(3000);
}

void MainWindow::startFade(double to, int ms, std::function<void()> done)
{
    fadeTimer->stop();
    if (ms <= 0) {
        fadeGain = to;
        applyVolume();
        if (done)
            done();
        return;
    }
    fadeFrom = fadeGain;
    fadeTo = to;
    fadeMs = ms;
    fadeDone = done;
    fadeClock.start();
    fadeTimer->start();
}

void MainWindow::loadTransitionSettings()
{
    fadeInMs = settings->value("TransitionFadeInMs", 0).toInt();
    fadeOutMs = settings->value("TransitionFadeOutMs", 2000).toInt();
    endAfterLyrics = settings->value("TransitionEndAfterLyrics", true).toBool();
    endAfterLyricsSec = settings->value("TransitionEndAfterLyricsSec", 2).toInt();
    songLevelOn = settings->value("SongLevelOn", true).toBool();
    songLevelTarget = qBound(-30.0, settings->value("SongLevelTarget", -16.0).toDouble(), -8.0);
}

// ---- Same loudness for every song (see Midi/SongLevel.h) ----------------------------------------
// Each song gets one steady gain: target - its loudness (at most +6 / -12 dB), plus the operator's
// own dB for it (Alt + Up / Down). The loudness comes from the database (measured before), from its
// file (video / MP3, measured in the background when queued), or - the first time a MIDI / YouTube
// song plays - from its first ~10 s of music; the whole song's reading is then remembered.

double MainWindow::autoLevelDbFor(double lufs) const
{
    // down to -24 dB: MIDI songs through several loud soundfonts measured up to +7 LUFS (23 dB over
    // the -16 target); -12 left them so hot that the output clipped
    return qBound(-24.0, songLevelTarget - lufs, 6.0);
}

void MainWindow::applySongLevelGain(double glideMs)
{
    SongLevel::get().setGainDb((songLevelOn ? levelAutoDb : 0.0) + levelOffsetDb, glideMs);
}

void MainWindow::startSongLevel()
{
    finishSongLevel();
    levelType = playingSong.songType();
    levelPath = playingSong.path();
    levelKnown = false;
    levelAdjusted = false;
    levelAutoDb = 0.0;
    levelOffsetDb = 0.0;
    double lufs = std::nan(""), own = 0.0;
    if (db)
        db->songLevel(levelType, levelPath, &lufs, &own);
    levelOffsetDb = own;
    double typical = std::nan("");
    if (!std::isnan(lufs)) {
        levelKnown = true;
        levelAutoDb = autoLevelDbFor(lufs);
    } else {
        if (levelType == "VIDEO" || levelType == "MP3")
            measureSongFile(&playingSong);      // ready in a moment (see onSongFileMeasured)
        // Not measured yet: start where songs of its kind usually are (MIDI through these soundfonts
        // ~ 0 LUFS = -16 dB), not at 0 dB - else it played 10 s far too loud, then dropped a lot.
        static const QStringList midiTypes = { "NCN", "EMK", "SIB", "HNK", "KAR" };
        if (db)
            typical = db->typicalLoudness(midiTypes.contains(levelType) ? midiTypes : QStringList{ levelType });
        if (!std::isnan(typical))
            levelAutoDb = autoLevelDbFor(typical);
    }
    SongLevel::get().restartMeasure();
    applySongLevelGain(250);
    uiLog(QString("song level: %1 %2: %3 -> auto %4 dB (own %5 dB)")
          .arg(levelType, levelPath)
          .arg(levelKnown ? QString("%1 LUFS").arg(lufs, 0, 'f', 1)
                          : std::isnan(typical) ? QString("not measured yet")
                                                : QString("not measured yet, starts as its kind (%1 LUFS)").arg(typical, 0, 'f', 1))
          .arg(levelAutoDb, 0, 'f', 1).arg(levelOffsetDb, 0, 'f', 1));

    if (!songLevelTimer) {
        songLevelTimer = new QTimer(this);
        songLevelTimer->setInterval(1000);
        connect(songLevelTimer, &QTimer::timeout, this, [this]() {
            if (levelPath.isEmpty() || levelKnown || levelAdjusted) {
                songLevelTimer->stop();
                return;
            }
            double l = -100.0, sec = 0.0;
            SongLevel::get().reading(&l, &sec);
            if (sec >= 10.0 && l > -70.0) {     // enough music heard: set the gain once, softly
                levelAutoDb = autoLevelDbFor(l);
                levelAdjusted = true;
                // over 8 s: the song started at its kind's usual level, so this is a few dB at most,
                // too slow to hear as a change (2 s was heard as "loud, then it drops")
                applySongLevelGain(8000);
                songLevelTimer->stop();
                uiLog(QString("song level: first play, %1 LUFS after %2 s -> %3 dB")
                      .arg(l, 0, 'f', 1).arg(sec, 0, 'f', 0).arg(levelAutoDb, 0, 'f', 1));
            }
        });
    }
    if (!levelKnown)
        songLevelTimer->start();
}

void MainWindow::finishSongLevel()
{
    if (levelPath.isEmpty())
        return;
    if (!levelKnown && db) {
        double l = -100.0, sec = 0.0;
        SongLevel::get().reading(&l, &sec);
        if (sec >= 30.0 && l > -70.0) {         // most of a song: good enough to remember
            db->rememberSongLoudness(levelType, levelPath, l);
            uiLog(QString("song level: remembered %1 LUFS (%2 s) for %3").arg(l, 0, 'f', 1).arg(sec, 0, 'f', 0).arg(levelPath));
        }
    }
    levelPath.clear();
    levelType.clear();
    if (songLevelTimer)
        songLevelTimer->stop();
}

void MainWindow::changeSongLevel(double stepDb)
{
    if (levelPath.isEmpty() || !isSongPlaying())
        return;
    levelOffsetDb = qBound(-12.0, levelOffsetDb + stepDb, 12.0);
    applySongLevelGain(150);
    if (db)
        db->rememberSongGain(levelType, levelPath, levelOffsetDb);
    auto signedDb = [](double d) { return (d > 0 ? "+" : "") + QString::number(d, 'f', 0) + " dB"; };
    QString value = signedDb(levelOffsetDb);
    if (songLevelOn && (levelKnown || levelAdjusted))
        value += tr("  • ปรับอัตโนมัติ %1").arg(signedDb(levelAutoDb));
    ui->detail->setDetail(tr("ความดังเพลงนี้"), value);
    ui->detail->show();
    detailTimer->start(3000);
}

void MainWindow::measureSongFile(Song *s)
{
    if (!s || !db)
        return;
    const QString type = s->songType();
    if (type != "VIDEO" && type != "MP3")
        return;
    double lufs = std::nan("");
    db->songLevel(type, s->path(), &lufs, nullptr);
    if (!std::isnan(lufs))
        return;                                 // measured before
    const QString key = type + '|' + s->path();
    if (levelMeasuring.contains(key))
        return;
    const QString file = isFullPath(s->path())
        ? s->path()
        : ((type == "VIDEO" ? db->videoPath() : db->mp3Path()) + s->path());
    if (!QFile::exists(file))
        return;
    levelMeasuring.insert(key);
    if (!levelPool) {
        levelPool = new QThreadPool(this);
        levelPool->setMaxThreadCount(1);        // one file at a time, in the background
    }
    QPointer<MainWindow> self(this);
    const QString path = s->path();
    levelPool->start([self, type, path, file]() {
        const double v = SongLevel::measureFile(file);
        QMetaObject::invokeMethod(qApp, [self, type, path, v]() {
            if (self)
                self->onSongFileMeasured(type, path, v);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onSongFileMeasured(const QString &type, const QString &path, double lufs)
{
    levelMeasuring.remove(type + '|' + path);
    if (lufs <= -70.0)
        return;                                 // not readable / silent: measured while it plays instead
    if (db)
        db->rememberSongLoudness(type, path, lufs);
    uiLog(QString("song level: file %1 measured %2 LUFS").arg(path).arg(lufs, 0, 'f', 1));
    if (type == levelType && path == levelPath && !levelKnown) {   // the song playing now
        levelKnown = true;
        levelAutoDb = autoLevelDbFor(lufs);
        applySongLevelGain(levelAdjusted ? 2000 : 1000);
    }
}

// ---- Sound pads ("แป้นเสียงเชียร์", see Midi/SoundPads.h) ---------------------------------------------
void MainWindow::ensureSoundPads()
{
    if (!soundPads) {
        soundPads = new SoundPads(player->midiSynthesizer(), this);
        // first use: the sound banks of karaoke programs on this computer (only searched once)
        QApplication::setOverrideCursor(Qt::WaitCursor);
        soundPads->autoFindRoots();
        QApplication::restoreOverrideCursor();
    }
}

void MainWindow::nextSoundBank()
{
    ensureSoundPads();
    if (soundPads->bankCount() <= 1) {
        ui->detail->setDetail(tr("ชุดเสียง"), tr("มีชุดเดียว (เพิ่มโฟลเดอร์ชุดเสียงได้ในตั้งค่าแป้นเสียง)"));
    } else {
        soundPads->nextBank();
        ui->detail->setDetail(tr("ชุดเสียง %1/%2").arg(soundPads->currentBank() + 1).arg(soundPads->bankCount()),
                              soundPads->bankName(soundPads->currentBank()));
        if (padsPanel)
            padsPanel->refresh();
    }
    ui->detail->show();
    detailTimer->start(2500);
}

void MainWindow::triggerSoundPad(int index)
{
    ensureSoundPads();
    QTimer::singleShot(0, this, [this, index]() {   // after trigger(): what was really played
        uiLog(QString("sound pad %1 [%2] %3: %4").arg(index + 1).arg(soundPads->bankName(soundPads->currentBank()))
              .arg(soundPads->label(index)).arg(soundPads->lastPlayed()));
    });
    if (soundPads->isEmpty(index)) {
        ui->detail->setDetail(tr("แป้นเสียง %1").arg(index + 1), tr("ยังไม่มีเสียง (คลิกขวา → แป้นเสียงเชียร์ → เพิ่มเสียงเอฟเฟกต์)"));
    } else if (!soundPads->trigger(index)) {
        const SoundPads::Pad p = soundPads->pad(index);
        const bool builtin = soundPads->currentBank() == 0 && p.file.isEmpty();
        ui->detail->setDetail(tr("แป้นเสียง %1").arg(index + 1),
                              builtin ? tr("เล่นไม่ได้: ยังไม่มีซาวด์ฟอนต์") : tr("เล่นไฟล์ไม่ได้: %1").arg(soundPads->label(index)));
    } else if (soundPads->wasStop()) {
        ui->detail->setDetail(tr("แป้นเสียง %1").arg(index + 1), tr("หยุด: %1").arg(soundPads->label(index)));
    } else {
        ui->detail->setDetail(tr("แป้นเสียง %1").arg(index + 1), soundPads->label(index));
    }
    ui->detail->show();
    detailTimer->start(1500);
}

void MainWindow::stopAllSoundPads()
{
    if (!soundPads)
        return;
    uiLog("sound pads: stop all");
    if (soundPads->stopAll()) {
        ui->detail->setDetail(tr("แป้นเสียง"), tr("หยุดทั้งหมด"));
        ui->detail->show();
        detailTimer->start(1500);
    }
}

void MainWindow::showSoundPadsPanel(bool show)
{
    settings->setValue("SoundPadsPanel", show);
    if (!show) {
        if (padsPanel) {
            settings->setValue("SoundPadsPanelGeometry", padsPanel->saveGeometry());
            padsPanel->hide();
        }
        return;
    }
    ensureSoundPads();
    if (!padsPanel) {
        padsPanel = new SoundPadsPanel(this, soundPads);
        padsPanel->onPad = [this](int i) { triggerSoundPad(i); };
        padsPanel->onSettings = [this]() { openSoundPadsSettings(); };
        padsPanel->onStopAll = [this]() { stopAllSoundPads(); };
        padsPanel->onAddSounds = [this]() { openSoundFxManager(); };
        padsPanel->onClosed = [this]() {
            settings->setValue("SoundPadsPanel", false);
            settings->setValue("SoundPadsPanelGeometry", padsPanel->saveGeometry());
        };
        if (!padsPanel->restoreGeometry(settings->value("SoundPadsPanelGeometry").toByteArray()))
            padsPanel->move(mapToGlobal(QPoint(width() - 360, 80)));
    }
    padsPanel->refresh();
    padsPanel->show();
}

// "เพิ่มเสียงเอฟเฟกต์": the shop's own sounds (Dialogs/SoundFxManager.h). Afterwards the pads play the
// bank that was being edited.
// "สร้างไดรฟ์พกพา": the program, its settings and the song libraries onto a USB / external drive
void MainWindow::openPortableDrive()
{
    if (!requireShopUnlock(tr("สร้างไดรฟ์พกพา")))
        return;
    if (db && db->isRunning()) {
        QMessageBox::information(dialogParent(), tr("สร้างไดรฟ์พกพา"), tr("รอให้ปรับปรุงฐานข้อมูลเพลงเสร็จก่อน"));
        return;
    }
    settings->sync();               // the drive gets the settings as they are now
    PortableDriveDialog dlg(dialogParent());
    dlg.exec();
    setFocus();
}

// "เสียงพิธีการ / เอฟเฟกต์" (Dialogs/EventSoundsDialog.h)
void MainWindow::openEventSounds()
{
    ensureSoundPads();
    EventSoundsDialog dlg(dialogParent(), soundPads);
    dlg.selectCategory(settings->value("EventSounds/Last").toString());
    dlg.exec();
    settings->setValue("EventSounds/Last", dlg.currentCategoryName());
    setFocus();
}

void MainWindow::showEventSoundsQuickMenu(const QPoint &globalPos)
{
    ensureSoundPads();
    QMenu m(this);
    QAction *openWin = m.addAction(MenuIcons::make(MenuIcons::Drum),
                                   tr("เปิดหน้าต่าง เสียงพิธีการ / เอฟเฟกต์..."));
    connect(openWin, &QAction::triggered, this, [this]() {
        QTimer::singleShot(0, this, [this]() { openEventSounds(); });
    });
    if (soundPads && !soundPads->previewPlaying().isEmpty()) {
        QAction *st = m.addAction(tr("■  หยุดเสียงที่กำลังเล่น (%1)\tAlt+Backspace")
                                      .arg(SoundPads::fileLabel(soundPads->previewPlaying())));
        connect(st, &QAction::triggered, this, [this]() { stopAllSoundPads(); });
    }
    m.addSeparator();

    // Categories with sounds ready to play in 1 click directly from the popup menu
    const QVector<EventSoundsDialog::Cat> cats = EventSoundsDialog::allCategories(soundPads);
    for (const EventSoundsDialog::Cat &c : cats) {
        if (c.name.isEmpty() || c.files.isEmpty())
            continue;
        QMenu *sub = m.addMenu(QString("%1 (%2)").arg(c.name).arg(c.files.size()));
        QAction *openCat = sub->addAction(tr("เปิดหมวด \"%1\" ในหน้าต่างใหญ่...").arg(c.name));
        const QString catName = c.name;
        connect(openCat, &QAction::triggered, this, [this, catName]() {
            settings->setValue("EventSounds/Last", catName);
            QTimer::singleShot(0, this, [this]() { openEventSounds(); });
        });
        sub->addSeparator();
        for (const QString &file : c.files) {
            const QString label = SoundPads::fileLabel(file);
            QAction *a = sub->addAction(QString::fromUtf8("▶  ") + label);
            if (soundPads && soundPads->previewPlaying() == file)
                a->setText(QString::fromUtf8("■  ") + label + tr(" (กำลังเล่น - คลิกเพื่อหยุด)"));
            connect(a, &QAction::triggered, this, [this, file, label]() {
                if (!soundPads)
                    return;
                soundPads->preview(file);
                if (!soundPads->previewPlaying().isEmpty()) {
                    ui->detail->setDetail(tr("เอฟเฟกต์"), label);
                    ui->detail->show();
                    detailTimer->start(2500);
                }
                setFocus();
            });
        }
    }

    // Quick pads (Alt+1..9)
    if (soundPads) {
        m.addSeparator();
        QMenu *padsSub = m.addMenu(tr("แป้นเสียงเชียร์ด่วน (Alt+1..9): %1").arg(soundPads->bankName(soundPads->currentBank())));
        for (int i = 0; i < SoundPads::Count; ++i) {
            QAction *pa = padsSub->addAction(QString("Alt+%1  %2").arg(i + 1).arg(soundPads->label(i)));
            connect(pa, &QAction::triggered, this, [this, i]() { triggerSoundPad(i); setFocus(); });
        }
    }

    m.addSeparator();
    QAction *panelAct = m.addAction(tr("แสดงแป้นเสียงลอยบนจอ (กดได้ตลอดเวลา)"));
    panelAct->setCheckable(true);
    panelAct->setChecked(padsPanel && padsPanel->isVisible());
    connect(panelAct, &QAction::triggered, this, [this](bool on) { showSoundPadsPanel(on); setFocus(); });

    QAction *stopAllAct = m.addAction(tr("หยุดเสียงเอฟเฟกต์ทั้งหมด\tAlt+Backspace"));
    connect(stopAllAct, &QAction::triggered, this, [this]() { stopAllSoundPads(); setFocus(); });

    m.exec(globalPos - QPoint(0, m.sizeHint().height()));
    setFocus();
}

void MainWindow::openSoundFxManager()
{
    ensureSoundPads();
    QString show;
    const QString cur = soundPads->bankName(soundPads->currentBank());
    if (cur.startsWith(SoundPads::ownBankName(QString())))
        show = cur.mid(SoundPads::ownBankName(QString()).length());
    SoundFxManager dlg(dialogParent(), soundPads, show);
    dlg.onChanged = [this]() { if (padsPanel) padsPanel->refresh(); };
    dlg.exec();
    const int b = dlg.chosenBank().isEmpty() ? -1 : soundPads->findBank(SoundPads::ownBankName(dlg.chosenBank()));
    if (b > 0)
        soundPads->setBank(b);
    if (padsPanel)
        padsPanel->refresh();
    setFocus();
}

void MainWindow::openSoundPadsSettings()
{
    ensureSoundPads();
    SoundPadsDialog dlg(dialogParent(), soundPads);
    dlg.exec();
    if (padsPanel)
        padsPanel->refresh();
    setFocus();
}

void MainWindow::setSongLevelOn(bool on)
{
    songLevelOn = on;
    settings->setValue("SongLevelOn", on);
    applySongLevelGain(500);
}

void MainWindow::setSongLevelTarget(double lufs)
{
    songLevelTarget = lufs;
    settings->setValue("SongLevelTarget", lufs);
    if (levelKnown || levelAdjusted) {          // the playing song follows the new target
        double l = -100.0;
        if (levelKnown && db) {
            double stored = std::nan("");
            db->songLevel(levelType, levelPath, &stored, nullptr);
            l = stored;
        } else {
            SongLevel::get().reading(&l, nullptr);
        }
        if (!std::isnan(l) && l > -70.0)
            levelAutoDb = autoLevelDbFor(l);
    }
    applySongLevelGain(500);
}

// the song came to its end (by itself, or faded out after its last lyric / with End)
void MainWindow::songFinished()
{
    endingSong = false;
    playNext();
}

// End key / "จบเพลง": fade out, then as at the end of the song; pressed again it ends at once
void MainWindow::endSongSmoothly()
{
    if (!isSongPlaying()) {
        if (auto_playnext)
            playNext();
        else
            stop();
        return;
    }
    if (songInMedley() && player->isPlayerPlaying() && (player->midiSequencerTemp() != nullptr || playlist.size() > (playingIndex + 1))) {
        if (player->midiSequencerTemp() == nullptr && playlist.size() > (playingIndex + 1)) {
            loadNextMedley(playlist[playingIndex + 1]);
            if (medleyLoader) medleyLoader->wait();
        }
        if (player->midiSequencerTemp() != nullptr) {
            MidiSequencer *seq = player->midiSequencer();
            int nextBarTick = seq->midiFile()->tickFromBar(seq->currentBar() + 1);
            if (nextBarTick <= seq->positionTick() + seq->midiFile()->resorution() / 8)
                nextBarTick = seq->midiFile()->tickFromBar(seq->currentBar() + 2);
            seq->setEndTick(nextBarTick);
            ui->detail->setDetail(tr("เมดเลย์ "), tr("ส่งกลองเข้าเพลงถัดไปท้ายห้องดนตรี"));
            ui->detail->show();
            detailTimer->start(3000);
            return;
        }
    }
    // A MIDI song with no medley hand-off (medley off, or no next song in the queue): end like a
    // band ("ลูกจบ") - at the next bar line the band lands on beat 1, the drums play a fill bar,
    // then crash + kick. The song then ends as if by itself (next song, or stop when none).
    if (!endingSong && player->isPlayerPlaying() && player->midiSequencer() != nullptr) {
        MidiSequencer *seq = player->midiSequencer();
        if (!seq->isDrumHoldOn() && !seq->midiFile()->events().isEmpty()) {
            int nextBarTick = seq->midiFile()->tickFromBar(seq->currentBar() + 1);
            if (nextBarTick <= seq->positionTick() + seq->midiFile()->resorution() / 8)
                nextBarTick = seq->midiFile()->tickFromBar(seq->currentBar() + 2);
            if (nextBarTick < int(seq->midiFile()->events().last()->tick())) {
                seq->setEndingFill(true);
                seq->setEndTick(nextBarTick);
                endingSong = true;      // no lyric auto-cut on top; pressed again: ends at once
                ui->detail->setDetail(tr("จบเพลง"), tr("ส่งกลองจบเพลงท้ายห้องดนตรี"));
                ui->detail->show();
                detailTimer->start(3000);
                return;
            }
        }
    }
    auto finish = [this]() {
        endingSong = false;
        if (auto_playnext)
            songFinished();
        else
            stop();
    };
    if (endingSong) {                   // already fading out
        fadeTimer->stop();
        fadeDone = nullptr;
        finish();
        return;
    }
    endingSong = true;
    startFade(0.0, fadeOutMs, finish);
}

// pos: lyric position (MIDI ticks, or ms for MP3 + LRC); remainingMs: time to the end of the song
void MainWindow::checkEndAfterLyrics(qint64 pos, qint64 remainingMs)
{
    const bool medleyAutoCut = songInMedley() && player->medleyAutoCutOutro();
    if ((!endAfterLyrics && !medleyAutoCut) || endingSong)
        return;
    if (lastLyricPos == -2) {
        lastLyricPos = lyrWidget->lastActiveLyricTick();
        if (lastLyricPos <= 0) {
            const QVector<long> c = lyrWidget->curData();
            for (long v : c)
                if (v > lastLyricPos && v < 1000000) lastLyricPos = v;
        }
    }
    if (lastLyricPos < 0)
        return;

    long lyricCutTick = lastLyricPos;
    if (songInMedley() && player->midiSequencer() != nullptr) {
        lyricCutTick = player->midiSequencer()->findVocalReleaseTick(lastLyricPos);
    }

    if (pos < lyricCutTick) {           // still singing (or sustaining final vocal note)
        afterLyrics = false;
        return;
    }
    if (!afterLyrics) {
        afterLyrics = true;
        if (songInMedley()) {
            const int nextMedleyIdx = (remove_playlist ? 0 : playingIndex + 1);
            if (player->isPlayerPlaying() && (player->midiSequencerTemp() != nullptr || playlist.size() > nextMedleyIdx)) {
                if (player->midiSequencerTemp() == nullptr && playlist.size() > nextMedleyIdx) {
                    loadNextMedley(playlist[nextMedleyIdx]);
                    if (medleyLoader != nullptr && medleyLoader->isRunning())
                        medleyLoader->wait();
                }
                MidiSequencer *seq = player->midiSequencer();
                const int waitBars = player->medleyWaitBars();
                int currentBar = seq->barAtTick(uint32_t(seq->positionTick()));   // real musical bars
                const int targetBar = currentBar + 1 + waitBars;
                const int totalBars = seq->midiFile()->barCount();
                // Cut where the band's steady beat stops (ending hits / held final chord),
                // or `waitBars` after the last lyric, whichever comes first. The bridge then
                // carries the beat on with no "song stops, then drums start" gap.
                const int grooveEnd = seq->firstNonGrooveBar(currentBar + 1);
                int cutBar = qMin(targetBar, grooveEnd);
                QString reason = (cutBar == grooveEnd && grooveEnd < targetBar)
                                     ? QString("where the song's beat stops")
                                     : QString("%1 bar(s) after last lyric").arg(waitBars);

                // Finish the musical phrase: songs move in 4-bar phrases counted from the bar
                // where the singing starts. Cutting "N bars after the last word" in the middle
                // of a phrase sounded unfinished; the next phrase line is where the band lands.
                if (cutBar == targetBar) {
                    const int res = qMax(1, seq->midiFile()->resorution());
                    const long first = lyrWidget->firstSungLyricTick();
                    if (first > 0) {
                        int anchor = seq->barAtTick(uint32_t(first));
                        const double beatInBar = double(long(first) - long(seq->barStartTick(anchor))) / res;
                        if (beatInBar >= 2.5)       // a pick-up before the bar line: the phrase starts after it
                            ++anchor;
                        int phraseCut = cutBar;
                        while ((phraseCut - anchor) % 4 != 0)
                            ++phraseCut;
                        const int limit = (grooveEnd > currentBar) ? grooveEnd : phraseCut;
                        if (phraseCut > cutBar && phraseCut <= limit) {
                            reason = QString("end of the 4-bar phrase (phrases from bar %1; %2 bar(s) after last lyric at least)")
                                         .arg(anchor).arg(waitBars);
                            cutBar = phraseCut;
                        } else if (phraseCut > limit && limit > cutBar) {
                            reason = QString("where the song's beat stops (before the phrase end)");
                            cutBar = limit;
                        }
                    }
                }
                // A short ending (the band is about to land on its final chord): play the song's
                // own ending to the last note instead of cutting just before it.
                const int endBar = seq->barAtTick(uint32_t(seq->lastNoteOnTick())) + 1;
                const int kShortEndingBars = 8;
                if (endBar > cutBar && endBar - cutBar <= kShortEndingBars) {
                    reason = QString("short ending (%1 bar(s) left): played to the last note").arg(endBar - cutBar);
                    cutBar = endBar;
                } else if (totalBars > 0 && cutBar >= totalBars) {
                    cutBar = endBar;
                    reason = "after the last note";
                }
                int cutTick = int(seq->barStartTick(cutBar));
                if (cutTick <= seq->positionTick() + seq->midiFile()->resorution() / 2) {
                    ++cutBar;
                    cutTick = int(seq->barStartTick(cutBar));
                }
                if (seq->endTick() == 0 || seq->endTick() > cutTick)
                    seq->setEndTick(cutTick);
                MidiSequencer::timingLog(QString("outro cut: %1, at bar %2 (now bar %3, song has %4 bars)")
                                         .arg(reason).arg(cutBar).arg(currentBar).arg(totalBars));
            }
            return;
        }
        afterLyricsClock.start();
        return;
    }
    if (songInMedley())
        return;
    if (afterLyricsClock.elapsed() < qint64(endAfterLyricsSec) * 1000)
        return;
    if (remainingMs <= fadeOutMs + 1500)    // ends by itself in a moment anyway
        return;

    endingSong = true;
    const bool midi = !videoActive && !ytActive && mp3Stream == 0;
    startFade(0.0, fadeOutMs, [this, midi]() {
        if (midi) {                     // as at the natural end of a MIDI song
            scoreTimer->stop();
            showScore();
            stopRecording();
        }
        songFinished();
    });
}

void MainWindow::showTransitionDialog()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("การต่อเพลง"));
    auto spin = [&dlg](double v, double max, double step, const QString &suffix) {
        QDoubleSpinBox *s = new QDoubleSpinBox(&dlg);
        s->setRange(0, max);
        s->setSingleStep(step);
        s->setDecimals(step < 1 ? 1 : 0);
        s->setValue(v);
        s->setSuffix(suffix);
        s->setMinimumWidth(110);
        return s;
    };
    const QString sec = tr(" วินาที");
    QDoubleSpinBox *fadeOut = spin(fadeOutMs / 1000.0, 10, 0.5, sec);
    QDoubleSpinBox *fadeIn = spin(fadeInMs / 1000.0, 3, 0.5, sec);
    QCheckBox *trim = new QCheckBox(tr("จบเพลงหลังเนื้อร้องท่อนสุดท้าย (ตัดดนตรีท้ายเพลงที่ยาว)"), &dlg);
    trim->setChecked(endAfterLyrics);
    QDoubleSpinBox *trimSec = spin(endAfterLyricsSec, 60, 1, sec);
    trimSec->setEnabled(endAfterLyrics);
    connect(trim, &QCheckBox::toggled, trimSec, &QWidget::setEnabled);

    auto note = [&dlg](const QString &t) {
        QLabel *l = new QLabel(t, &dlg);
        l->setWordWrap(true);
        l->setStyleSheet("color: gray;");
        return l;
    };
    QFormLayout *form = new QFormLayout();
    form->addRow(tr("ค่อยๆ เบาเสียงตอนจบเพลง:"), fadeOut);
    form->addRow(QString(), note(tr("ใช้ตอนกด End / จบเพลง และตอนตัดดนตรีท้ายเพลง  (0 = ตัดทันที)")));
    form->addRow(tr("ค่อยๆ ดังขึ้นตอนเริ่มเพลง:"), fadeIn);
    form->addRow(trim);
    form->addRow(tr("จบหลังเนื้อร้องสุดท้าย:"), trimSec);
    form->addRow(QString(), note(tr("เฉพาะเพลง NCN / MIDI / KAR และ MP3 ที่มีเนื้อร้อง  (เพลงวิดีโอและ YouTube จะเล่นจนจบ)")));

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addLayout(form);
    v->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;

    settings->setValue("TransitionFadeOutMs", qRound(fadeOut->value() * 1000));
    settings->setValue("TransitionFadeInMs", qRound(fadeIn->value() * 1000));
    settings->setValue("TransitionEndAfterLyrics", trim->isChecked());
    settings->setValue("TransitionEndAfterLyricsSec", int(trimSec->value()));
    loadTransitionSettings();
    setFocus();
}

// ---- song requests from phones ----------------------------------------------------------------
