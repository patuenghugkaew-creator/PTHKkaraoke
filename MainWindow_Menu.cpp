// MainWindow: Right-click menu, config file save/load, settings & effect dialogs, language
#include "MainWindow_p.h"

void MainWindow::showContextMenu(const QPoint &pos)
{
    showContextMenuAt(mapToGlobal(pos));
}

static const char *CFG_APP_PREFIX   = "App/";
static const char *CFG_SYNTH_PREFIX = "Synth/";

// New .kmcfg format: the two settings files stored byte for byte. Copying key by key through QVariant changed some
// values (lists written as text, "a,b,c" strings turned into string lists ...) and the sound came back wrong.
static const quint32 CFG_RAW_MAGIC = 0x4B4D4352;   // "KMCR"

static QByteArray readWholeFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

static bool writeWholeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

void MainWindow::saveConfigFile()
{
    // make sure the file on disk holds the current mixer state first
    synthMix->settingValues();
    settings->sync();

    QDir().mkpath(Config::CONFIG_DIR_PATH);   // dialog opens in the config folder itself
    QString file = QFileDialog::getSaveFileName(synthMix, tr("บันทึกคอนฟิก"),
                        QDir(Config::CONFIG_DIR_PATH).absoluteFilePath("PTHKKaraoke.pthkcfg"),
                        tr("PTHK Karaoke config (*.pthkcfg)"), nullptr,
                        QFileDialog::DontConfirmOverwrite);   // no overwrite prompt (it plays a warning sound over the music)
    if (file.isEmpty())
        return;
    if (!file.endsWith(".pthkcfg", Qt::CaseInsensitive) && !file.endsWith(".kmcfg", Qt::CaseInsensitive))
        file += ".pthkcfg";

    if (QFileInfo::exists(file)) {   // ask before overwriting, with NoIcon so Windows plays no sound over the music
        QMessageBox box(QMessageBox::NoIcon, tr("บันทึกคอนฟิก"),
                        tr("มีไฟล์นี้อยู่แล้ว ต้องการเขียนทับหรือไม่?\n") + file,
                        QMessageBox::Yes | QMessageBox::No, synthMix);
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes)
            return;
    }

    {
        QSettings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat).sync();
        QSettings(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat).sync();
        QByteArray blob;
        QDataStream ds(&blob, QIODevice::WriteOnly);
        ds.setVersion(QDataStream::Qt_5_12);
        ds << CFG_RAW_MAGIC << quint32(1)
           << readWholeFile(Config::CONFIG_APP_FILE_PATH)
           << readWholeFile(Config::CONFIG_SYNTH_FILE_PATH);
        if (!writeWholeFile(file, blob)) {
            QMessageBox::warning(synthMix, tr("บันทึกคอนฟิก"), tr("บันทึกไฟล์ไม่สำเร็จ\n") + file);
            return;
        }
    }

    // silent confirmation: a message box would play the system sound in the middle of the music
    QToolTip::showText(QCursor::pos(), tr("บันทึกคอนฟิกเรียบร้อยแล้ว\n") + file, synthMix, QRect(), 4000);
}

void MainWindow::openConfigFile()
{
    QString file = QFileDialog::getOpenFileName(synthMix, tr("เปิดคอนฟิก"),
                        Config::CONFIG_DIR_PATH, tr("PTHK Karaoke config (*.pthkcfg *.kmcfg)"));
    if (file.isEmpty())
        return;

    // new format (raw copy of both settings files)?
    QByteArray rawApp, rawSyn;
    bool isRaw = false;
    {
        const QByteArray all = readWholeFile(file);
        QDataStream ds(all);
        ds.setVersion(QDataStream::Qt_5_12);
        quint32 magic = 0, ver = 0;
        ds >> magic >> ver;
        if (ds.status() == QDataStream::Ok && magic == CFG_RAW_MAGIC) {
            ds >> rawApp >> rawSyn;
            if (ds.status() != QDataStream::Ok) {
                QMessageBox::warning(synthMix, tr("เปิดคอนฟิก"), tr("ไฟล์คอนฟิกเสียหาย"));
                return;
            }
            isRaw = true;
        }
    }

    QSettings in(file, QSettings::IniFormat);
    QStringList keys = isRaw ? QStringList() : in.allKeys();

    bool valid = isRaw;
    for (const QString &k : keys) {
        if (k.startsWith(CFG_APP_PREFIX) || k.startsWith(CFG_SYNTH_PREFIX)) {
            valid = true;
            break;
        }
    }
    if (!valid) {
        QMessageBox::warning(synthMix, tr("เปิดคอนฟิก"), tr("ไฟล์นี้ไม่ใช่ไฟล์คอนฟิกของ PTHK Karaoke"));
        return;
    }

    if (QMessageBox::question(synthMix, tr("เปิดคอนฟิก"),
            tr("โปรแกรมจะโหลดคอนฟิกนี้แทนค่าปัจจุบัน แล้วเปิดโปรแกรมใหม่อัตโนมัติ\nต้องการดำเนินการต่อ?"),
            QMessageBox::Yes|QMessageBox::No) != QMessageBox::Yes)
        return;

    // Do not let the exit routine overwrite what we are about to import.
    skipSaveOnExit = true;
    synthMix->setSkipSave(true);

    if (isRaw) {
        if (!writeWholeFile(Config::CONFIG_APP_FILE_PATH, rawApp) ||
            !writeWholeFile(Config::CONFIG_SYNTH_FILE_PATH, rawSyn)) {
            skipSaveOnExit = false;
            synthMix->setSkipSave(false);
            QMessageBox::warning(synthMix, tr("เปิดคอนฟิก"), tr("เขียนไฟล์คอนฟิกไม่สำเร็จ"));
            return;
        }
    } else {
        QSettings app(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        QSettings syn(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        app.clear();
        syn.clear();

        for (const QString &k : keys) {
            if (k.startsWith(CFG_APP_PREFIX))
                app.setValue(k.mid(QString(CFG_APP_PREFIX).length()), in.value(k));
            else if (k.startsWith(CFG_SYNTH_PREFIX))
                syn.setValue(k.mid(QString(CFG_SYNTH_PREFIX).length()), in.value(k));
        }
        app.sync();
        syn.sync();
    }

    // the files on disk are the new config: apply it to the running program (the music keeps playing)
    skipSaveOnExit = false;
    synthMix->setSkipSave(false);
    applyConfigLive();
    QToolTip::showText(QCursor::pos(), tr("โหลดคอนฟิกเรียบร้อยแล้ว (ไม่ต้องรีสตาร์ท)"), synthMix, QRect(), 4000);
}

void MainWindow::applyConfigLive()
{
    MidiSynthesizer *synth = player->midiSynthesizer();
    synthMix->setSkipSave(true);          // no auto-save of the old state over the new files while this runs

    settings->sync();                     // MainWindow's own QSettings re-reads the replaced file
    QSettings app(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    app.sync();

    // ---- 1. audio engine options (the setters rebuild the audio only when the value really changes)
    {
        int synBuf = app.value("SynthBuffer", 100).toInt();
        BASS_SetConfig(BASS_CONFIG_BUFFER, synBuf);

        synth->setVoices(app.value("SynthVoices", 500).toInt());
        synth->setSincInterpolation(false);
        synth->setReverbStrong(app.value("ReverbStrong", false).toBool());

        int aout = app.value("SynthDefaultDevice", 1).toInt();
        int asio = app.value("AsioDevice", -1).toInt();
        if (asio >= AsioOutput::devices().count())
            asio = -1;
        if (asio != synth->asioDevice())
            synth->setAsioDeviceKeepState(asio);
        if (asio < 0 && aout != synth->defaultDevice())
            synth->setDefaultDevice(aout);

        synth->setSampleRate(44100);
        synth->setUsetFloattingPoint(true);
        synth->setUseFXRC(false);
    }

    // ---- 2. soundfonts (list, volumes, preset maps)
    {
        synth->setLoadAllSoundfont(app.value("SynthSoundfontsLoadAll", false).toBool());

        const QStringList want = app.value("SynthSoundfonts", QStringList()).toStringList();
        if (synth->soundfontFiles() != want) {
            int guard = 512;
            while (guard-- > 0 && !synth->soundfontFiles().isEmpty())
                synth->removeSoundfont(0);
            for (const QString &f : want)
                synth->addSoundfont(f);
        }
        app.beginReadArray("SynthSoundfontsVolume");
        for (int i = 0; i < want.count(); i++) {
            app.setArrayIndex(i);
            synth->setSoundfontVolume(i, app.value("SoundfontVolume", 100).toInt() / 100.0f);
        }
        app.endArray();

        for (int i = 0; i < SF_PRESET_COUNT; i++) {
            QString sfKey = "SynthSoundfontsMap";
            QString drKey = "SynthSoundfontsDrumMap";
            if (i > 0) {
                sfKey += QString::number(i);
                drKey += QString::number(i);
            }
            QList<int> sfMap     = app.value(sfKey).value<QList<int>>();
            QList<int> sfDrumMap = app.value(drKey).value<QList<int>>();
            if (sfMap.count() == 0)
                sfMap = synth->getMapSoundfontIndex(i);
            if (sfDrumMap.count() == 0)
                sfDrumMap = synth->getDrumMapSfIndex(i);
            synth->setMapSoundfontIndex(i, sfMap, sfDrumMap);
        }
    }

    #ifndef __linux__
    // ---- 3. VSTi instruments
    {
        QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        st.sync();
        st.beginReadArray("VSTiGroup");
        for (int i = 0; i < synth->HANDLE_VSTI_COUNT; i++) {
            st.setArrayIndex(i);
            const QString filePath = st.value("VstiFilePath", "").toString();
            const int program = st.value("VstiPrograms", 0).toInt();
            const QList<float> params = st.value("VstiParams").value<QList<float>>();
            const QByteArray chunk = st.value("VstiChunk", QByteArray()).toByteArray();

            if (filePath.isEmpty()) {
                if (!synth->vstiFile(i).isEmpty())
                    synth->removeVSTiFile(i);
                continue;
            }
            const DWORD vsti = synth->setVSTiFile(i, filePath);   // same file = nothing is reloaded
            if (vsti == 0)
                continue;
            if (chunk.length() > 0) {
                bv::SetChunk(vsti, false, chunk.constData(), chunk.length());
            } else {
                bv::SetProgram(vsti, program);
                FX::setVSTParams(vsti, params);
            }
        }
        st.endArray();
    }
    #endif

    // ---- 4. player options (drum locks)
    {
        auto lock = [&](const char *key, const char *numKey, int def, void (MidiPlayer::*fn)(bool, int)) {
            const bool on = app.value(key, false).toBool();
            (player->*fn)(on, on ? app.value(numKey, def).toInt() : 0);
        };
        lock("MidiLockDrum",  "MidiLockDrumNumber",  0,  &MidiPlayer::setLockDrum);
        lock("MidiLockSnare", "MidiLockSnareNumber", 38, &MidiPlayer::setLockSnare);
        lock("MidiLockKick",  "MidiLockKickNumber",  36, &MidiPlayer::setLockKick);
        lock("MidiLockBass",  "MidiLockBassNumber",  32, &MidiPlayer::setLockBass);
    }

    // ---- 5. the mixer: strips, effects, routing, master volume ...
    synthMix->reloadFromSettings();

    ui->sliderVolume->setValue(player->volume());
    synthMix->setSkipSave(false);
}

void MainWindow::setMixerAutoHide(bool on, double delaySec, const QString &program)
{
    if (!autoHide)
        autoHide = new MixerAutoHide(synthMix, this);
    autoHide->configure(on, delaySec, program);
}

QWidget* MainWindow::dialogParent()
{
    // Dialogs open over the window being used: the mixer when it is the active window
    // (its Menu button), otherwise the karaoke player, which may be on another monitor.
    if (synthMix->isVisible() && synthMix->isActiveWindow())
        return synthMix;
    return this;
}

void MainWindow::showContextMenuAt(const QPoint &globalPos)
{
    QMenu menu(tr("Context menu"), synthMix);
    menu.setMinimumWidth(230);

    QAction actionSettings(tr("ตั้งค่า..."), this);
    QAction actionMappChanel(tr("แยกช่องสัญญาณ..."), this);
    QAction actionShowEqDlg(tr("อีควอไลเซอร์"), this);
    QAction actionShowChorusDlg(tr("เอฟเฟ็กต์เสียงประสาน"), this);
    QAction actionShowReverbDlg(tr("เอฟเฟ็กต์เสียงก้อง"), this);
    QAction actionMapSF(tr("ตารางเลือกใช้ซาวด์ฟ้อนท์..."), this);
    QAction actionAbout(tr("เกี่ยวกับ..."), this);
    QAction actionAboutQt(tr("เกี่ยวกับ Qt..."), this);
    QAction actionExit(tr("ออกจากโปรแกรม"), this);

    connect(&actionSettings, SIGNAL(triggered()), this, SLOT(showSettingsDialog()));
    connect(&actionMappChanel, SIGNAL(triggered()), this, SLOT(showMapMidiChannelDialog()));
    connect(&actionShowEqDlg, SIGNAL(triggered()), this, SLOT(showEqDialog()));
    connect(&actionShowReverbDlg, SIGNAL(triggered()), this, SLOT(showReverbDialog()));
    connect(&actionShowChorusDlg, SIGNAL(triggered()), this, SLOT(showChorusDialog()));
    connect(&actionMapSF, SIGNAL(triggered()), this, SLOT(showMapSFDialog()));
    connect(&actionAbout, SIGNAL(triggered()), this, SLOT(showAboutDialog()));
    connect(&actionAboutQt, SIGNAL(triggered()), this, SLOT(showAboutQtDialog()));
    connect(&actionExit, SIGNAL(triggered()), this, SLOT(close()));

    QAction actionShowMixer(tr("แสดงมิกเซอร์"), this);
    connect(&actionShowMixer, &QAction::triggered, this, [this]() {
        synthMix->show();
        synthMix->raise();
        synthMix->activateWindow();
    });

    QAction actionFullScreen(tr("เต็มจอ / หน้าต่างปกติ"), this);
    connect(&actionFullScreen, SIGNAL(triggered()), this, SLOT(showFullScreenOrNormal()));

    QAction actionVideoDir(tr("โฟลเดอร์วิดีโอคาราโอเกะ..."), this);
    connect(&actionVideoDir, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getExistingDirectory(dialogParent(), tr("เลือกที่เก็บวิดีโอคาราโอเกะ"),
                                                         db->videoPath(), QFileDialog::ShowDirsOnly);
        if (path.isEmpty())
            return;
        db->setVideoPath(path);
        settings->setValue("VideoPath", path);
        QMessageBox::information(dialogParent(), tr("โฟลเดอร์วิดีโอคาราโอเกะ"),
                                 tr("ตั้งที่เก็บวิดีโอเป็น ") + path +
                                 tr("\nกด \"ปรับปรุง\" ในหน้าตั้งค่า > ฐานข้อมูลเพลง เพื่อเพิ่มวิดีโอเข้าฐานข้อมูล"));
    });

    QAction actionMp3Dir(tr("โฟลเดอร์เพลง MP3 (+ เนื้อร้อง .lrc)..."), this);
    connect(&actionMp3Dir, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getExistingDirectory(dialogParent(), tr("เลือกที่เก็บเพลง MP3"),
                                                         db->mp3Path(), QFileDialog::ShowDirsOnly);
        if (path.isEmpty())
            return;
        db->setMp3Path(path);
        settings->setValue("MP3Path", path);
        QMessageBox::information(dialogParent(), tr("โฟลเดอร์เพลง MP3"),
                                 tr("ตั้งที่เก็บเพลง MP3 เป็น ") + path +
                                 tr("\nกด \"ปรับปรุง\" ในหน้าตั้งค่า > ฐานข้อมูลเพลง เพื่อเพิ่มเพลงเข้าฐานข้อมูล"));
    });

    QAction actionDmx(tr("ไฟเวที DMX..."), this);
    connect(&actionDmx, &QAction::triggered, this, [this]() { showDmxDialog(); });

    QAction actionBgVideo(tr("พื้นหลังเป็นวิดีโอ..."), this);
    connect(&actionBgVideo, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getExistingDirectory(dialogParent(), tr("เลือกโฟลเดอร์วิดีโอพื้นหลัง"),
                                                         bgWidget->backgroundVideoDir(), QFileDialog::ShowDirsOnly);
        if (path.isEmpty())
            return;
        int n = bgWidget->setBackgroundVideoDir(path);
        if (n == 0) {
            QMessageBox::information(dialogParent(), tr("พื้นหลังเป็นวิดีโอ"), tr("ไม่พบไฟล์วิดีโอในโฟลเดอร์นี้"));
            return;
        }
        bgWidget->setBackgroundType(Background::Video);
        settings->setValue("BackgroundType", Background::Video);
        settings->setValue("BackgroundVideoDir", path);
        if (secondMonitor != nullptr && secondLyr != nullptr) {
            secondMonitor->backgroundWidget()->setBackgroundVideoDir(path);
            secondMonitor->backgroundWidget()->setBackgroundType(Background::Video);
        }
    });

    QAction actionRecord(OutputRecorder::instance().isRecording() ? tr("หยุดบันทึกเสียงร้อง (F12)")
                                                                  : tr("บันทึกเสียงร้อง (F12)"), this);
    connect(&actionRecord, &QAction::triggered, this, [this]() { toggleRecording(); });

    QAction actionScore(tr("ให้คะแนนการร้อง (ใช้ไมโครโฟน)"), this);
    actionScore.setCheckable(true);
    actionScore.setChecked(vocalScore);
    connect(&actionScore, &QAction::triggered, this, [this](bool on) { setVocalScore(on); });

    QAction actionLists(tr("เพลงโปรด / เล่นล่าสุด / เพลงฮิต (F3)"), this);
    connect(&actionLists, &QAction::triggered, this, [this]() {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_F3, Qt::NoModifier);
        keyPressEvent(&e);
    });

    QAction actionSecond(tr("จอที่ 2 (เนื้อร้องบนจอที่สอง)"), this);
    actionSecond.setCheckable(true);
    actionSecond.setChecked(secondLyr != nullptr);
    connect(&actionSecond, SIGNAL(triggered()), this, SLOT(showSecondMonitor()));

    QAction actionNewSongs(tr("ตรวจหาเพลงใหม่ในโฟลเดอร์คลัง NCN (เร็ว)"), this);
    connect(&actionNewSongs, &QAction::triggered, this, [this]() { updateNewSongs(); });

    QAction actionQuickUpdate(tr("ปรับปรุงเฉพาะวิดีโอ / MP3 / KAR (เร็ว)"), this);
    connect(&actionQuickUpdate, &QAction::triggered, this, [this]() {
        if (db->isRunning())
            return;
        db->setUpdateType(UpdateType::UpdateMedia);
        db->start();
    });

    QAction actionMixerAtStart(tr("เปิดมิกเซอร์พร้อมโปรแกรม"), this);
    actionMixerAtStart.setCheckable(true);
    actionMixerAtStart.setChecked(settings->value("ShowMixerAtStart", false).toBool());
    connect(&actionMixerAtStart, &QAction::triggered, this, [this](bool on) {
        settings->setValue("ShowMixerAtStart", on);
    });

    QAction actionImport(tr("เพิ่มเพลง (NCN / วิดีโอ / MP3 / KAR / zip / rar)..."), this);
    connect(&actionImport, &QAction::triggered, this, [this]() { showImportDialog(); });

    // ---- Main level: only what is used while singing; everything else is grouped below ----
    MidiSynthesizer *syn = player->midiSynthesizer();
    actionSettings.setIcon(MenuIcons::make(MenuIcons::Settings));
    actionMappChanel.setIcon(MenuIcons::make(MenuIcons::Channel));
    actionMapSF.setIcon(MenuIcons::make(MenuIcons::Soundfont));
    actionAbout.setIcon(MenuIcons::make(MenuIcons::About));
    actionExit.setIcon(MenuIcons::make(MenuIcons::Exit));
    actionImport.setIcon(MenuIcons::make(MenuIcons::AddSong));
    actionLists.setIcon(MenuIcons::make(MenuIcons::Star));
    actionRecord.setIcon(MenuIcons::make(MenuIcons::MicRecord));
    actionFullScreen.setIcon(MenuIcons::make(MenuIcons::Fullscreen));
    actionSecond.setIcon(MenuIcons::make(MenuIcons::Screen));
    actionBgVideo.setIcon(MenuIcons::make(MenuIcons::Video));
    actionShowMixer.setIcon(MenuIcons::make(MenuIcons::Eq));
    actionNewSongs.setIcon(MenuIcons::make(MenuIcons::Search));
    actionQuickUpdate.setIcon(MenuIcons::make(MenuIcons::Reset));
    actionVideoDir.setIcon(MenuIcons::make(MenuIcons::Video));
    actionMp3Dir.setIcon(MenuIcons::make(MenuIcons::Open));
    actionScore.setIcon(MenuIcons::make(MenuIcons::Trophy));
    actionDmx.setIcon(MenuIcons::make(MenuIcons::DmxLight));

    menu.addAction(&actionImport);
    {
        const bool on = player->isDrumHoldOn();
        QAction *hold = menu.addAction(MenuIcons::make(MenuIcons::Drum), on ? tr("ส่งเข้าเพลงต่อ (ลูกส่ง)\tCtrl+D") : tr("ค้างจังหวะกลอง (ดนตรีหยุด เหลือกลอง)\tCtrl+D"));
        hold->setEnabled(!videoActive && !ytActive && !mp3Stream && player->isPlayerPlaying());
        connect(hold, &QAction::triggered, this, [this]() { toggleDrumHold(); });
    }
    menu.addAction(MenuIcons::make(MenuIcons::AddSong), tr("เพิ่มเพลงจากไฟล์ .zip / .rar / .7z..."), this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(
                    dialogParent(), tr("เลือกไฟล์บีบอัดที่มีเพลง"), Utils::LAST_OPEN_DIR,
                    tr("ไฟล์บีบอัด (*.zip *.rar *.7z);;ทุกไฟล์ (*)"));
        if (files.isEmpty())
            return;
        Utils::LAST_OPEN_DIR = QFileInfo(files.first()).absolutePath();
        importSongs(files);
    });
    menu.addAction(MenuIcons::make(MenuIcons::Search), tr("ค้นหาเพลงซ้ำ (ชื่อ นักร้อง ความเร็วเดียวกัน)..."), this, [this]() {
        DuplicateSongsDialog dlg(db, dialogParent());
        dlg.exec();
    });
    {
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::YouTube), tr("YouTube"));
        m->addAction(MenuIcons::make(MenuIcons::Search), tr("ค้นหาเพลง YouTube (เล่นสด)\tF10"), this, [this]() { showYouTubeSearch(); });
        m->addAction(MenuIcons::make(MenuIcons::AiSparkle), tr("ตัดเสียงร้อง YouTube (AI / เร็ว, ระดับเสียง)..."), this, [this]() { showYtVocalDialog(); });
        m->addAction(MenuIcons::make(MenuIcons::Download), tr("ดาวน์โหลดจากลิงก์ YouTube..."), this, [this]() { showYouTubeDownload(); });
    }
    QAction actionAiStems(tr("AI แยกเสียงร้องเพลงวิดีโอนี้ (ทำครั้งเดียว)"), this);
    actionAiStems.setIcon(MenuIcons::make(MenuIcons::AiSparkle));
    const QString curVideo = (videoActive && !playingSong.path().isEmpty())
                                 ? db->videoPath() + playingSong.path() : QString();
    const bool curHasStems = !curVideo.isEmpty() && AiStemMaker::hasStems(curVideo);
    actionAiStems.setEnabled(!curVideo.isEmpty() && !curHasStems);
    if (curHasStems)
        actionAiStems.setText(tr("AI แยกเสียงร้องเพลงนี้แล้ว"));
    connect(&actionAiStems, &QAction::triggered, this, [this, curVideo]() { enqueueAiStems(curVideo); });

    menu.addAction(&actionLists);
    menu.addAction(&actionRecord);
    if (videoActive)
        menu.addAction(&actionAiStems);
    menu.addAction(&actionShowMixer);
    menu.addSeparator();

    { // Screen
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Screen), tr("หน้าจอ"));
        m->addAction(&actionFullScreen);
        m->addAction(&actionSecond);
        m->addAction(&actionBgVideo);
        m->addSeparator();
        m->addAction(&actionShowMixer);
        m->addAction(&actionMixerAtStart);
        QAction *topAct = m->addAction(tr("มิกเซอร์อยู่บนสุดตลอดเวลา (On Top)"));
        topAct->setCheckable(true);
        topAct->setChecked(synthMix->isStaysOnTop());
        connect(topAct, &QAction::triggered, this, [this](bool on) {
            QMetaObject::invokeMethod(synthMix, "setStaysOnTop", Q_ARG(bool, on));
        });
        m->addSeparator();

        QMenu *theme = m->addMenu(MenuIcons::make(MenuIcons::Theme), tr("ธีม / Theme"));
        QActionGroup *grp = new QActionGroup(theme);
        QAction *aSys    = theme->addAction(tr("ตามธีม Windows"));
        QAction *aDark   = theme->addAction(tr("มืด (Dark)"));
        QAction *aLight  = theme->addAction(tr("สว่าง (Light)"));
        QAction *aOrange = theme->addAction(tr("ดาร์กส้ม (Dark Orange)"));
        const QList<QAction*> acts = { aSys, aDark, aLight, aOrange };
        for (QAction *a : acts) {
            a->setCheckable(true);
            grp->addAction(a);
        }
        aSys->setChecked(Theme::mode() == Theme::System);
        aDark->setChecked(Theme::mode() == Theme::Dark);
        aLight->setChecked(Theme::mode() == Theme::Light);
        aOrange->setChecked(Theme::mode() == Theme::DarkOrange);
        connect(aSys,    &QAction::triggered, this, []() { Theme::setMode(Theme::System); });
        connect(aDark,   &QAction::triggered, this, []() { Theme::setMode(Theme::Dark); });
        connect(aLight,  &QAction::triggered, this, []() { Theme::setMode(Theme::Light); });
        connect(aOrange, &QAction::triggered, this, []() { Theme::setMode(Theme::DarkOrange); });

        QMenu *lang = m->addMenu(MenuIcons::make(MenuIcons::Language), tr("Language"));
        QAction *th = lang->addAction("ไทย");
        th->setCheckable(true);
        th->setChecked(currentLang == "th");
        connect(th, SIGNAL(triggered()), this, SLOT(setThaiLang()));
        QAction *en = lang->addAction("English");
        en->setCheckable(true);
        en->setChecked(currentLang == "en");
        connect(en, SIGNAL(triggered()), this, SLOT(setEngLang()));
    }

    { // Song library
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Open), tr("คลังเพลง"));
        m->addAction(&actionNewSongs);
        m->addAction(MenuIcons::make(MenuIcons::Book), tr("จัดรหัสเพลงตามศิลปิน / สมุดเพลง..."), this, [this]() { showArtistCodeDialog(); });
        m->addAction(&actionQuickUpdate);
        m->addSeparator();
        m->addAction(&actionVideoDir);
        m->addAction(&actionMp3Dir);
    }

    { // Sound & effects (the mixer tools)
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Eq), tr("เสียงและเอฟเฟกต์"));
        // effect pictograms: neon green when the effect is on, dimmed when off
        auto fxIcon = [](MenuIcons::Kind k, bool on) {
            return MenuIcons::make(k, on ? MenuIcons::onColor() : MenuIcons::dimColor());
        };
        actionShowEqDlg.setIcon(fxIcon(MenuIcons::Eq, syn->equalizer31BandFXs()[0]->isOn()));
        actionShowChorusDlg.setIcon(fxIcon(MenuIcons::Chorus, syn->chorusFXs()[0]->isOn()));
        actionShowReverbDlg.setIcon(fxIcon(MenuIcons::Reverb, syn->reverbFXs()[0]->isOn()));
        m->addAction(&actionShowEqDlg);
        m->addAction(&actionShowChorusDlg);
        m->addAction(&actionShowReverbDlg);
        m->addAction(&actionMapSF);
        QAction *actLockSf = m->addAction(MenuIcons::make(MenuIcons::Lock), tr("ล็อก Soundfont (.kmsf)..."));
        connect(actLockSf, &QAction::triggered, this, [this]() {
            LockSoundfontDialog dlg(dialogParent(), player->midiSynthesizer());
            dlg.exec();
        });
        QAction *actMaster = m->addAction(MenuIcons::make(MenuIcons::AiSparkle), tr("มาสเตอริ่งเสียง (Mastering Suite)..."));
        connect(actMaster, &QAction::triggered, this, [this]() {
            MasteringDialog dlg(dialogParent(), player->midiSynthesizer());
            dlg.exec();
        });
        m->addSeparator();

        m->addAction(&actionMappChanel);
        QAction *act = m->addAction(MenuIcons::make(MenuIcons::Bus), tr("บัสกรุ๊ป..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showBusGroupDialog()));
        act = m->addAction(MenuIcons::make(MenuIcons::Speaker), tr("แยกอุปกรณ์เสียง/ลำโพง..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showSpeakerDialog()));
        #ifndef __linux__
        act = m->addAction(MenuIcons::make(MenuIcons::Vst), tr("จัดการ VST && VSTi..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showVSTDirDialog()));
        #endif
        act = m->addAction(MenuIcons::make(MenuIcons::Meter), tr("ตั้งค่า LED Meter..."));
        connect(act, &QAction::triggered, this, [this]() {
            QMetaObject::invokeMethod(synthMix, "showVuDlg");
        });
        m->addSeparator();

        QAction *drumAct = m->addAction(tr("ตัดคำสั่งจูนเสียงกลอง/สแนร์ (ช่อง 10)"));
        drumAct->setCheckable(true);
        drumAct->setChecked(syn->isIgnoreDrumPitch());
        connect(drumAct, &QAction::triggered, this, [this](bool on) {
            player->midiSynthesizer()->setIgnoreDrumPitch(on);
            settings->setValue("IgnoreDrumPitch", on);
        });
        act = m->addAction(MenuIcons::make(MenuIcons::Reset), tr("รีเซ็ตมิกเซอร์"));
        connect(act, &QAction::triggered, this, [this]() {
            QMetaObject::invokeMethod(synthMix, "resetChannel");
        });
        m->addSeparator();

        QAction *saveCfg = m->addAction(MenuIcons::make(MenuIcons::Save), tr("บันทึกคอนฟิก (Save config)..."));
        connect(saveCfg, &QAction::triggered, this, [this]() { saveConfigFile(); });
        QAction *openCfg = m->addAction(MenuIcons::make(MenuIcons::Open), tr("เปิดคอนฟิก (Open config)..."));
        connect(openCfg, &QAction::triggered, this, [this]() { openConfigFile(); });
    }

    { // Extras
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Phone), tr("อุปกรณ์เสริม"));
        m->addAction(&actionScore);
        m->addAction(&actionDmx);
        QAction *act = m->addAction(MenuIcons::make(MenuIcons::QrCode), tr("ควบคุมผ่านมือถือ (QR Code)..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showRemoteDialog()));
        m->addAction(MenuIcons::make(MenuIcons::QrCode), tr("ขอเพลงจากมือถือ (QR ลูกค้า / เจ้าของ)..."),
                     this, [this]() { showSongRequestDialog(); });
    }

    menu.addSeparator();
    menu.addAction(&actionSettings);
    menu.addAction(MenuIcons::make(MenuIcons::Transition), tr("การต่อเพลง (เฟดเสียง / ตัดท้ายเพลง)..."), this, [this]() { showTransitionDialog(); });
    QAction *helpAct = menu.addAction(MenuIcons::make(MenuIcons::Help), tr("คู่มือการใช้งาน / ปุ่มลัด (F1)"));
    connect(helpAct, &QAction::triggered, this, [this]() { showHelpDialog(); });

    { // About
        QMenu *m = nullptr; if (false) m = menu.addMenu(MenuIcons::make(MenuIcons::About), tr("เกี่ยวกับ"));
        QAction *actPro = nullptr; if (false) actPro = m->addAction(tr("สิทธิ์การใช้งาน / USB Dongle (PRO)..."));
        if (actPro) connect(actPro, &QAction::triggered, this, [this]() {
            PTHKCore::showLicenseDialog(dialogParent());
        });
        menu.addAction(MenuIcons::make(MenuIcons::Download), tr("ตรวจหาอัปเดต..."), this, [this]() { showCheckUpdateDialog(); });
        menu.addAction(&actionAbout);

    }
    menu.addAction(&actionExit);

    // The menu closes by itself when the mouse leaves it (sub-menus count as part of it)
    QTimer leaveTimer;
    leaveTimer.setInterval(120);
    bool entered = false;
    int outsideMs = 0;
    connect(&leaveTimer, &QTimer::timeout, &menu, [&]() {
        QRect area;
        const QWidgetList tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            if (w->isVisible() && qobject_cast<QMenu *>(w))
                area = area.united(w->frameGeometry());
        }
        if (area.isNull())
            return;
        const QPoint pos = QCursor::pos();
        if (area.contains(pos)) {
            entered = true;
            outsideMs = 0;
            return;
        }
        // not entered yet: the menu stays until it is used or clicked away (a right-click menu
        // used to vanish after 1.5 s while the mouse was still on its way)
        if (!entered) {
            outsideMs = 0;
            return;
        }
        outsideMs += leaveTimer.interval();
        if (outsideMs >= 800) {
            if (QWidget *p = QApplication::activePopupWidget())
                p->close();     // innermost sub-menu first, then the menu itself on the next tick
        }
    });
    leaveTimer.start();

    menu.exec(globalPos);
    QMetaObject::invokeMethod(synthMix, "syncQuickChecks");
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog d(dialogParent(), this);
    d.setModal(true);
    d.adjustSize();
    {   // never taller / wider than the screen it opens on (the device tab scrolls inside)
        QScreen *sc = dialogParent() ? dialogParent()->screen() : QGuiApplication::primaryScreen();
        if (sc) {
            const QRect av = sc->availableGeometry();
            const int maxH = qMax(300, av.height() - 100);   // 36 px title bar + window border + taskbar margin
            const int maxW = qMax(400, av.width() - 20);
            d.setMaximumHeight(maxH);
            d.resize(qMin(d.width(), maxW), qMin(d.height(), maxH));
        }
    }
    d.setMinimumSize(d.size());
    d.exec();
}

void MainWindow::showRemoteDialog()
{
    if (remoteDialog)
    {
        remoteDialog->raise();
        remoteDialog->activateWindow();
        return;
    }

    if (!remoteServer)
        remoteServer = new RemoteServer(synthMix, this, this);

    RemoteDialog *dlg = new RemoteDialog(remoteServer, dialogParent());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    remoteDialog = dlg;
    dlg->show();
}

void MainWindow::showEqDialog()
{
    if (Equalizer31BandDialog::isOpenned())
        return;

    Equalizer31BandDialog *dlg = new Equalizer31BandDialog(
                dialogParent(), player->midiSynthesizer()->equalizer31BandFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::showChorusDialog()
{
    if (Chorus2Dialog::isOpenned())
        return;

    Chorus2Dialog *dlg = new Chorus2Dialog(dialogParent(), player->midiSynthesizer()->chorusFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::showReverbDialog()
{
    if (Reverb2Dialog::isOpenned())
        return;

    Reverb2Dialog *dlg = new Reverb2Dialog(dialogParent(), player->midiSynthesizer()->reverbFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::showMapMidiChannelDialog()
{
    MapChannelDialog dlg(dialogParent(), player);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
}

void MainWindow::showBusGroupDialog()
{
    BusDialog dlg(dialogParent(), synthMix->mixChannelMapPtr(), player->midiSynthesizer());
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
}

void MainWindow::showSpeakerDialog()
{
    SpeakerDialog dlg(dialogParent(), synthMix->mixChannelMapPtr(), this);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
}

void MainWindow::showVSTDirDialog()
{
    #ifndef __linux__
    VSTDirsDialog dlg(dialogParent(), this);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
#endif
}

void MainWindow::minimizeWindow()
{
    setWindowState(Qt::WindowMinimized);
}

void MainWindow::showMapSFDialog()
{
    MapSoundfontDialog msfDlg(dialogParent(), player->midiSynthesizer());
    msfDlg.setModal(true);
    msfDlg.setMinimumSize(msfDlg.size());
    msfDlg.exec();
}

void MainWindow::showSecondMonitor()
{
    if (secondLyr == nullptr) {
        this->stop();
        secondMonitor = new SecondMonitorDialog(this, lyrWidget);
        secondMonitor->setWindowTitle(tr("PTHK Karaoke - จอที่ 2"));
        secondMonitor->setProperty("pthkNoSkin", true);    // no skinned title bar on the TV / projector
        // its right-click "ปิดจอที่ 2" / window X: close it here (queued: not inside its own handler)
        connect(secondMonitor, &SecondMonitorDialog::closeRequested, this, [this]() {
            if (secondLyr != nullptr)
                showSecondMonitor();
        }, Qt::QueuedConnection);

        Background *bg = secondMonitor->backgroundWidget();
        bg->setBackgroundType(bgWidget->backgroundType());
        bg->setBackgroundColor(bgWidget->backgroundColor());
        bg->setBackgroundImage(bgWidget->backgroundImage());
        if (bgWidget->backgroundType() == Background::Video)
            bg->setBackgroundVideoDir(bgWidget->backgroundVideoDir());

        secondLyr = secondMonitor->lyrWidget();

        if (qApp->screens().count() > 1) {
            // full screen on the monitor this player window is not on (TV / projector);
            // right-click on it to make it a window or move it to another monitor
            QScreen *mine = windowHandle() ? windowHandle()->screen() : qApp->primaryScreen();
            QScreen *other = qApp->screens()[0] == mine ? qApp->screens()[1] : qApp->screens()[0];
            secondMonitor->showOnScreen(other);
        } else {
            secondMonitor->showAsWindow();      // one monitor only: a window to test with
        }
        activateWindow();       // keep typing song numbers here, not on the 2nd screen
    } else {
        secondLyr = nullptr;
        secondMonitor->setClosingByPlayer();
        secondMonitor->close();
        delete secondMonitor;
        secondMonitor = nullptr;
    }
    if (btnSecondScreen != nullptr)
        btnSecondScreen->setChecked(secondLyr != nullptr);
}

void MainWindow::showFullScreenOrNormal()
{
    if (this->isFullScreen()) {
        this->showNormal();
    } else {
        this->showFullScreen();
    }
}

void MainWindow::setThaiLang()
{
    if (currentLang == "th")
        return;

    if (QApplication::removeTranslator(&translator)) {
        QApplication::processEvents();
        currentLang = "th";
        settings->setValue("Language", currentLang);
    }
}

void MainWindow::setEngLang()
{
    if (currentLang == "en")
        return;

    QString path = QApplication::applicationDirPath() + "/languages/en.qm";

    if (translator.load(path)) {
        QApplication::installTranslator(&translator);
        QApplication::processEvents();
        currentLang = "en";
        settings->setValue("Language", currentLang);
    }
}
