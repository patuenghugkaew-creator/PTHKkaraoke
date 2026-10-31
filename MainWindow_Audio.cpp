// MainWindow: Volume, YouTube audio / AI vocal cut, fades, song endings and transitions
#include "MainWindow_p.h"

int MainWindow::effectiveVolume() const
{
    if (!ui->sliderVolume->isEnabled())
        return 0;
    return qRound(ui->sliderVolume->value() * fadeGain);
}

// every player at once: whichever is playing follows the volume slider and the fade
void MainWindow::applyVolume()
{
    const int v = effectiveVolume();
    // MIDI: the slider volume only when it changes (it also goes to the MIDI out ports);
    // the fade itself only to the built-in synth, many times a second
    const int base = ui->sliderVolume->isEnabled() ? ui->sliderVolume->value() : 0;
    if (base != midiBaseVolume) {
        midiBaseVolume = base;
        player->setVolume(base);
    }
    player->setFadeGain(float(fadeGain));
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
            if (!ytAudio)
                ytAudio = new YtAudio(aiEngine);
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
    const bool cut = ui->chMix->isMuteVoice();
    const bool ai = settings->value("YtCutAi", false).toBool();
    AiVocalEngine::setDevicePreference(settings->value("YtAiDevice", 0).toInt());
    if (ai)
        aiEngine->selectModel(settings->value("YtAiModel", "light").toString());
    ytAudio->setLevels(settings->value("YtVocalLevel", 0).toInt() / 100.0f,
                       settings->value("YtMusicLevel", 100).toInt() / 100.0f);
    const bool wasLatched = ytAiLatched;
    if (!ai)
        ytAiLatched = false;
    else if (cut)
        ytAiLatched = true;
    if (ai && ytAiLatched && !wasLatched && aiEngine && aiEngine->ready()) {
        // First switch-on in this song: picture holds and sound is silent until the AI's
        // first result (one AI delay), then both continue from the same point.
        ui->detail->setDetail(tr("AI ตัดเสียงร้อง"),
                              tr("กำลังเตรียม รอประมาณ %1 วินาที").arg(aiEngine->delaySeconds(), 0, 'f', 0));
        ui->detail->show();
        detailTimer->start(int(aiEngine->delaySeconds() * 1000));
    }
    ytAudio->setVocalCut(cut);
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
    QLabel *vocL = new QLabel(&dlg), *musL = new QLabel(&dlg);
    auto showLevels = [=]() {
        vocL->setText(QString::number(voc->value()) + "%");
        musL->setText(QString::number(mus->value()) + "%");
    };
    showLevels();
    QCheckBox *delayPic = new QCheckBox(tr("หน่วงภาพให้ตรงกับเสียง (โหมด AI)"), &dlg);
    delayPic->setChecked(settings->value("YtDelayPicture", true).toBool());
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
        showLevels();
        applyYtVocalCut();
    };
    for (QRadioButton *r : { rAi, rFast })
        connect(r, &QRadioButton::toggled, &dlg, apply);
    connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, apply);
    connect(voc, &QSlider::valueChanged, &dlg, apply);
    connect(mus, &QSlider::valueChanged, &dlg, apply);
    connect(delayPic, &QCheckBox::toggled, &dlg, apply);

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
    aiDev->addItem(tr("อัตโนมัติ (เลือกตัวที่เร็วกว่า)"), 0);
    aiDev->addItem(tr("CPU (ภาพไม่กระตุก แต่เสียงช้ากว่าเดิมเล็กน้อย)"), 1);
    aiDev->addItem(tr("การ์ดจอ"), 2);
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
    if (player->isUseMedley() && player->isPlayerPlaying() && (player->midiSequencerTemp() != nullptr || playlist.size() > (playingIndex + 1))) {
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
    if (false && !isSongPlaying()) {
        if (auto_playnext)
            playNext();
        else
            stop();
        return;
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
    const bool medleyAutoCut = player->isUseMedley() && player->medleyAutoCutOutro();
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
    if (player->isUseMedley() && player->midiSequencer() != nullptr) {
        lyricCutTick = player->midiSequencer()->findVocalReleaseTick(lastLyricPos);
    }

    if (pos < lyricCutTick) {           // still singing (or sustaining final vocal note)
        afterLyrics = false;
        return;
    }
    if (!afterLyrics) {
        afterLyrics = true;
        if (player->isUseMedley()) {
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
                if (totalBars > 0 && cutBar >= totalBars) {
                    cutBar = seq->barAtTick(uint32_t(seq->lastNoteOnTick())) + 1;
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
    if (player->isUseMedley())
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
