// MainWindow: Right-click menu, config file save/load, settings & effect dialogs, language
#include "MainWindow_p.h"
#include <QTabWidget>

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
            const QString n = i > 0 ? QString::number(i) : QString();
            synth->setMapSoundExtras(i, app.value("SynthSoundfontsSrc" + n).value<QList<int>>(),
                                     app.value("SynthSoundfontsVol" + n).value<QList<int>>());
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
    if (isShopLocked()) {
        QMenu lockedMenu(tr("Context menu"), this);
        lockedMenu.setMinimumWidth(240);
        lockedMenu.addAction(MenuIcons::make(MenuIcons::Lock),
                             tr("ปลดล็อกหน้าจอสำหรับร้าน\tCtrl+L"),
                             this, [this]() { QTimer::singleShot(0, this, [this]() { toggleShopLock(); }); });
        lockedMenu.addAction(MenuIcons::make(MenuIcons::Settings),
                             tr("ตั้งค่าล็อกหน้าจอ / เพลงคลอเมื่อคิวว่าง..."),
                             this, [this]() { QTimer::singleShot(0, this, [this]() { openShopModeSettings(); }); });
        lockedMenu.addSeparator();
        lockedMenu.addAction(MenuIcons::make(MenuIcons::Star),
                             tr("เพลงโปรดและประวัติ (ล่าสุด / เพลงฮิต)\tF3"),
                             this, [this]() {
            QKeyEvent e(QEvent::KeyPress, Qt::Key_F3, Qt::NoModifier);
            keyPressEvent(&e);
        });
        lockedMenu.addAction(MenuIcons::make(MenuIcons::YouTube),
                             tr("ค้นหาเพลง YouTube\tF10"),
                             this, [this]() { showYouTubeSearch(); });
        lockedMenu.addAction(MenuIcons::make(MenuIcons::Fullscreen),
                             tr("เต็มจอ / หน้าต่างปกติ"),
                             this, [this]() { showFullScreenOrNormal(); });
        lockedMenu.addAction(MenuIcons::make(MenuIcons::Help),
                             tr("คู่มือการใช้งาน / ปุ่มลัด\tF1"),
                             this, [this]() { showHelpDialog(); });
        lockedMenu.exec(globalPos);
        return;
    }

    QMenu menu(tr("Context menu"), synthMix);
    menu.setMinimumWidth(230);

    QAction actionSettings(tr("ตั้งค่า..."), this);
    QAction actionMappChanel(tr("แยกเสียงมิดี้ (MIDI Mapper / loopMIDI)..."), this);
    QAction actionShowEqDlg(tr("อีควอไลเซอร์"), this);
    QAction actionShowChorusDlg(tr("เอฟเฟ็กต์เสียงประสาน"), this);
    QAction actionShowReverbDlg(tr("เอฟเฟ็กต์เสียงก้อง"), this);
    QAction actionMapSF(tr("มัลติซาวด์ฟอนต์ (เลือกเสียงรายเครื่องดนตรี / ส่งออก-นำเข้าชุดเสียง)..."), this);
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

    QAction actionNewSongs(tr("สแกนหาเพลง NCN ที่เพิ่มใหม่"), this);
    connect(&actionNewSongs, &QAction::triggered, this, [this]() { updateNewSongs(); });

    QAction actionQuickUpdate(tr("สแกนเพลงวิดีโอ / เพลงเสียง / KAR"), this);
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

    // ---- Layout: what is used while singing, then one sub-menu per topic, then settings / help.
    // Things with a button on the control bar (drum hold, mixer, add song...) are not repeated here.
    MidiSynthesizer *syn = player->midiSynthesizer();
    // simple menu (the default): the everyday things; the sound engineer's tools (effects, sound
    // fonts, routing, VST, config files, DMX) come back with "เมนูแบบง่าย" unticked
    const bool simple = simpleMenu();
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
    actionShowMixer.setIcon(MenuIcons::make(MenuIcons::Eq));
    actionNewSongs.setIcon(MenuIcons::make(MenuIcons::Search));
    actionQuickUpdate.setIcon(MenuIcons::make(MenuIcons::Reset));
    actionVideoDir.setIcon(MenuIcons::make(MenuIcons::Video));
    actionMp3Dir.setIcon(MenuIcons::make(MenuIcons::Open));
    actionScore.setIcon(MenuIcons::make(MenuIcons::Trophy));
    actionDmx.setIcon(MenuIcons::make(MenuIcons::DmxLight));
    actionImport.setText(tr("นำเพลงเข้าคลัง (ไฟล์ / โฟลเดอร์ / zip)..."));
    actionLists.setText(tr("เพลงโปรดและประวัติ (ล่าสุด / เพลงฮิต)\tF3"));
    actionRecord.setText(OutputRecorder::instance().isRecording() ? tr("หยุดบันทึกเสียงร้อง\tF12")
                                                                  : tr("บันทึกเสียงร้อง\tF12"));

    // Always at the very top of the menu: Event sounds / Sound effects (1 click to open, or quick-play submenu)
    {
        QAction *evTop = menu.addAction(MenuIcons::make(MenuIcons::Drum),
            tr("เสียงพิธีการ / เอฟเฟกต์ (เชิญประธาน เพลงชาติ ปรบมือ โห่ แคน พิณ ...)..."));
        connect(evTop, &QAction::triggered, this, [this]() {
            QTimer::singleShot(0, this, [this]() { openEventSounds(); });
        });
        ensureSoundPads();
        const QVector<EventSoundsDialog::Cat> cats = EventSoundsDialog::allCategories(soundPads);
        QMenu *quickFx = menu.addMenu(MenuIcons::make(MenuIcons::Drum), tr("เล่นเสียงด่วน / แป้นเสียงเชียร์"));
        if (soundPads && !soundPads->previewPlaying().isEmpty()) {
            QAction *st = quickFx->addAction(tr("■  หยุดเสียงที่กำลังเล่น (%1)\tAlt+Backspace")
                                                 .arg(SoundPads::fileLabel(soundPads->previewPlaying())));
            connect(st, &QAction::triggered, this, [this]() { stopAllSoundPads(); });
            quickFx->addSeparator();
        }
        for (const EventSoundsDialog::Cat &c : cats) {
            if (c.name.isEmpty() || c.files.isEmpty())
                continue;
            QMenu *sub = quickFx->addMenu(QString("%1 (%2)").arg(c.name).arg(c.files.size()));
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
        quickFx->addSeparator();
        quickFx->addAction(tr("หยุดเสียงเอฟเฟกต์ทั้งหมด\tAlt+Backspace"), this, [this]() { stopAllSoundPads(); });
        // the sound pads (Alt + 1..9): the on-screen panel, settings, the shop's own sound files
        quickFx->addSeparator();
        QAction *show = quickFx->addAction(tr("แสดงแถบปุ่มแป้นเสียงบนจอ"));
        show->setCheckable(true);
        show->setChecked(padsPanel && padsPanel->isVisible());
        connect(show, &QAction::triggered, this, [this](bool v) { showSoundPadsPanel(v); });
        quickFx->addAction(tr("ตั้งค่าแป้นเสียง..."), this, [this]() {
            QTimer::singleShot(0, this, [this]() { openSoundPadsSettings(); });
        });
        quickFx->addAction(tr("เพิ่มเสียงเอฟเฟกต์ (ใส่ไฟล์เสียงของร้าน)..."), this, [this]() {
            QTimer::singleShot(0, this, [this]() { openSoundFxManager(); });
        });
        QAction *keys = quickFx->addAction(tr("Alt + 1..9 = เล่น/หยุด  •  Alt + 0 = ชุดถัดไป  •  Alt + Backspace = หยุดทั้งหมด"));
        keys->setEnabled(false);
        menu.addSeparator();
    }

    // ---- used while singing: straight on the first level
    menu.addAction(MenuIcons::make(MenuIcons::YouTube), tr("ค้นหาเพลง YouTube\tF10"), this, [this]() { showYouTubeSearch(); });
    menu.addAction(&actionLists);
    menu.addAction(&actionImport);
    menu.addAction(&actionRecord);
    QMenu *medleyTop = menu.addMenu(MenuIcons::make(MenuIcons::Transition), tr("เมดเลย์ / การต่อเพลง"));

    QMenu *controls = menu.addMenu(MenuIcons::make(MenuIcons::Transition), tr("เครื่องมือร้องเพลง"));
    {
        for (QAbstractButton *b : barButtons) {
            if (!b->property("songTool").toBool())
                continue;
            QAction *a = controls->addAction(b->text());
            a->setCheckable(b->isCheckable());
            a->setChecked(b->isChecked());
            a->setEnabled(b->isEnabled());
            connect(a, &QAction::triggered, b, [b]() { b->click(); });
        }
    }
    // ---- while singing
    controls->addSeparator();
    controls->addAction(&actionScore);
    QAction actionAiStems(tr("AI แยกเสียงร้องเพลงวิดีโอนี้ (ทำครั้งเดียว)"), this);
    actionAiStems.setIcon(MenuIcons::make(MenuIcons::AiSparkle));
    const QString curVideo = (videoActive && !playingSong.path().isEmpty())
                                 ? db->videoPath() + playingSong.path() : QString();
    const bool curHasStems = !curVideo.isEmpty() && AiStemMaker::hasStems(curVideo);
    actionAiStems.setEnabled(!curVideo.isEmpty() && !curHasStems);
    if (curHasStems)
        actionAiStems.setText(tr("AI แยกเสียงร้องเพลงนี้แล้ว"));
    connect(&actionAiStems, &QAction::triggered, this, [this, curVideo]() { enqueueAiStems(curVideo); });
    if (videoActive)
        controls->addAction(&actionAiStems);
    menu.addSeparator();

    { // Songs: adding, lists, keeping the library up to date, folders
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Open), tr("คลังเพลง"));
        m->addAction(tr("เลือกโฟลเดอร์คลังเพลงและสแกน..."), this, [this]() {
            settingsStartTab = 2;
            showSettingsDialog();
        });
        m->addSeparator();
        m->addAction(&actionNewSongs);
        m->addAction(&actionQuickUpdate);
        m->addSeparator();
    {   // a wrong copy among same-named songs: gone from the list (the file to the Recycle Bin if asked)
        QAction *del = m->addAction(MenuIcons::make(MenuIcons::Trash), tr("ลบเพลงนี้ออกจากคลัง...	Ctrl+Del"));
        del->setEnabled(songLoaded());
        connect(del, &QAction::triggered, this, [this]() { QTimer::singleShot(0, this, [this]() { deletePlayingSong(); }); });
    }
        if (!simple) {
        m->addAction(MenuIcons::make(MenuIcons::Search), tr("ค้นหาเพลงซ้ำ (ชื่อ นักร้อง ความเร็วเดียวกัน)..."), this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                DuplicateSongsDialog dlg(db, dialogParent());
                dlg.exec();
            });
        });
        m->addAction(MenuIcons::make(MenuIcons::Book), tr("จัดรหัสเพลงตามศิลปิน / สมุดเพลง..."), this, [this]() { showArtistCodeDialog(); });
        m->addSeparator();
        m->addAction(&actionVideoDir);
        m->addAction(&actionMp3Dir);
        }
    }

    { // YouTube
        QMenu *m = controls->addMenu(MenuIcons::make(MenuIcons::YouTube), tr("YouTube"));
        m->addAction(MenuIcons::make(MenuIcons::Search), tr("ค้นหาเพลง YouTube\tF10"), this, [this]() { showYouTubeSearch(); });
        m->addAction(MenuIcons::make(MenuIcons::AiSparkle), tr("ตัดเสียงร้อง YouTube (AI / เร็ว, ระดับเสียง)..."), this, [this]() { showYtVocalDialog(); });
        m->addAction(MenuIcons::make(MenuIcons::Download), tr("ดาวน์โหลดจากลิงก์ YouTube..."), this, [this]() { showYouTubeDownload(); });
    }

    { // Medley sets (one click on stage) and how songs follow each other
        QMenu *m = medleyTop;
        const QStringList names = medleySetNames();
        for (const QString &n : names)
            m->addAction(n, this, [this, n]() { QTimer::singleShot(0, this, [this, n]() { loadMedleySet(n); }); });
        if (names.isEmpty()) {
            QAction *none = m->addAction(tr("(ยังไม่มีชุดเมดเลย์)"));
            none->setEnabled(false);
        }
        m->addSeparator();
        m->addAction(tr("+ สร้างชุดเมดเลย์ใหม่..."), this, [this]() { QTimer::singleShot(0, this, [this]() { editMedleySet(QString()); }); });
        m->addAction(tr("แก้ไข / ลบชุดเมดเลย์..."), this, [this]() { QTimer::singleShot(0, this, [this]() { manageMedleySets(); }); });
        m->addAction(tr("บันทึกคิวเพลงตอนนี้เป็นชุด..."), this, [this]() { QTimer::singleShot(0, this, [this]() { saveMedleySet(); }); });
        m->addSeparator();
        m->addAction(MenuIcons::make(MenuIcons::Transition), tr("การต่อเพลง (เฟดเสียง / ตัดท้ายเพลง)..."), this, [this]() { showTransitionDialog(); });
    }

    // ---- sound and screen: the everyday switches first, the details in their sub-menus
    menu.addAction(&actionShowMixer);
    { // Sound and mixer
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Eq), tr("เสียง"));
        m->addAction(tr("เลือกลำโพงและทดสอบเสียง..."), this, [this]() {
            settingsStartTab = 3;
            showSettingsDialog();
        });
        if (!simple) {
        m->addAction(&actionMixerAtStart);
        QAction *topAct = m->addAction(tr("มิกเซอร์อยู่บนสุดตลอดเวลา (On Top)"));
        topAct->setCheckable(true);
        topAct->setChecked(synthMix->isStaysOnTop());
        connect(topAct, &QAction::triggered, this, [this](bool on) {
            QMetaObject::invokeMethod(synthMix, "setStaysOnTop", Q_ARG(bool, on));
        });
        }
        m->addSeparator();

        // effect pictograms: neon green when the effect is on, dimmed when off
        auto fxIcon = [](MenuIcons::Kind k, bool on) {
            return MenuIcons::make(k, on ? MenuIcons::onColor() : MenuIcons::dimColor());
        };
        actionShowEqDlg.setIcon(fxIcon(MenuIcons::Eq, syn->equalizer31BandFXs()[0]->isOn()));
        actionShowChorusDlg.setIcon(fxIcon(MenuIcons::Chorus, syn->chorusFXs()[0]->isOn()));
        actionShowReverbDlg.setIcon(fxIcon(MenuIcons::Reverb, syn->reverbFXs()[0]->isOn()));
        m->addAction(&actionShowEqDlg);
        if (!simple) {
            m->addAction(&actionShowChorusDlg);
            m->addAction(&actionShowReverbDlg);
            QAction *actMaster = m->addAction(MenuIcons::make(MenuIcons::AiSparkle), tr("มาสเตอริ่งเสียง (Mastering Suite)..."));
            connect(actMaster, &QAction::triggered, this, [this]() {
                MasteringDialog dlg(dialogParent(), player->midiSynthesizer());
                dlg.exec();
            });
        }
        if (audioMeter) {   // "เสียงใส" for YouTube / video / MP3 (Midi/AudioClarity.h)
            QAction *clear = m->addAction(tr("เสียงใส (เพลง YouTube / วิดีโอ / MP3)"));
            clear->setCheckable(true);
            clear->setChecked(audioMeter->isClear());
            clear->setToolTip(tr("เติมเสียงแหลมที่ไฟล์คุณภาพต่ำตัดหายไป เพิ่มความใส ลดเสียงอู้ ไม่มีผลกับเพลง MIDI"));
            connect(clear, &QAction::triggered, this, [this](bool v) {
                audioMeter->setClear(v);
                settings->setValue("AudioClear", v);
            });
        }
        {   // same loudness for every song (MIDI / MP3 / video / YouTube); Alt + Up / Down per song
            QMenu *lv = m->addMenu(tr("ความดังทุกเพลงเท่ากัน"));
            QAction *on = lv->addAction(tr("เปิดใช้ (Alt + ↑ / ↓ ปรับเฉพาะเพลงที่เล่นอยู่)"));
            on->setCheckable(true);
            on->setChecked(songLevelOn);
            connect(on, &QAction::triggered, this, [this](bool v) { setSongLevelOn(v); });
            lv->addSeparator();
            QActionGroup *grp = new QActionGroup(lv);
            const struct { double lufs; const char *name; } levels[] = {
                { -20.0, QT_TR_NOOP("เบา (-20 LUFS)") },
                { -18.0, QT_TR_NOOP("ค่อนข้างเบา (-18 LUFS)") },
                { -16.0, QT_TR_NOOP("ปกติ (-16 LUFS)") },
                { -14.0, QT_TR_NOOP("ดัง (-14 LUFS)") },
            };
            for (const auto &l : levels) {
                QAction *a = lv->addAction(tr(l.name));
                a->setCheckable(true);
                a->setChecked(qAbs(songLevelTarget - l.lufs) < 0.05);
                grp->addAction(a);
                const double target = l.lufs;
                connect(a, &QAction::triggered, this, [this, target]() { setSongLevelTarget(target); });
            }
        }
        if (!simple) {
            QMenu *sf = m->addMenu(MenuIcons::make(MenuIcons::Soundfont), tr("ซาวด์ฟอนต์"));
            sf->addAction(&actionMapSF);
            QAction *actLockSf = sf->addAction(MenuIcons::make(MenuIcons::Lock), tr("ล็อก Soundfont (.kmsf)..."));
            connect(actLockSf, &QAction::triggered, this, [this]() {
                LockSoundfontDialog dlg(dialogParent(), player->midiSynthesizer());
                dlg.exec();
            });
        }
        if (!simple) {
            QAction *drumAct = m->addAction(tr("ตัดคำสั่งจูนเสียงกลอง/สแนร์ (ช่อง 10)"));
            drumAct->setCheckable(true);
            drumAct->setChecked(syn->isIgnoreDrumPitch());
            connect(drumAct, &QAction::triggered, this, [this](bool on) {
                player->midiSynthesizer()->setIgnoreDrumPitch(on);
                settings->setValue("IgnoreDrumPitch", on);
            });
        }
        if (!simple || isMidiRoutedOut()) {   // the way out when MIDI routing to loopMIDI etc. left the songs silent
            QAction *reset = m->addAction(MenuIcons::make(MenuIcons::Channel),
                                          tr("เสียงมิดี้ไม่ออก? คืนทุกช่องเป็นเสียงในโปรแกรม (SoundFont)"));
            reset->setEnabled(isMidiRoutedOut());
            connect(reset, &QAction::triggered, this, [this]() {
                if (QMessageBox::question(dialogParent(), tr("คืนเป็นเสียงในโปรแกรม"),
                        tr("ให้ทุกเครื่องดนตรี ทุกช่อง และมิดี้ขาออกหลัก กลับมาเล่นจากซาวด์ฟอนต์ในโปรแกรม?\n"
                           "(การส่งออก loopMIDI / โปรแกรมอื่นจะถูกยกเลิกทั้งหมด มิดี้ขาเข้าไม่เปลี่ยน)"))
                        != QMessageBox::Yes)
                    return;
                resetMidiRoutingToSoundfont();
                ui->detail->setDetail(tr("คืนเป็นเสียงในโปรแกรมแล้ว"), tr("ทุกเครื่องดนตรีเล่นจากซาวด์ฟอนต์"));
                ui->detail->show();
                detailTimer->start(4000);
            });
        }
        {
        m->addSeparator();

        {   // routing and hardware: set up once, rarely touched
            QMenu *adv = m->addMenu(MenuIcons::make(MenuIcons::Channel), tr("ตั้งค่าเสียงขั้นสูง"));
            adv->addAction(&actionMappChanel);
            adv->addAction(&actionMapSF);
            QAction *act = adv->addAction(MenuIcons::make(MenuIcons::Bus), tr("บัสกรุ๊ป..."));
            connect(act, SIGNAL(triggered()), this, SLOT(showBusGroupDialog()));
            act = adv->addAction(MenuIcons::make(MenuIcons::Speaker), tr("แยกอุปกรณ์เสียง/ลำโพง..."));
            connect(act, SIGNAL(triggered()), this, SLOT(showSpeakerDialog()));
            #ifndef __linux__
            act = adv->addAction(MenuIcons::make(MenuIcons::Vst), tr("จัดการ VST && VSTi..."));
            connect(act, SIGNAL(triggered()), this, SLOT(showVSTDirDialog()));
            #endif
            act = adv->addAction(MenuIcons::make(MenuIcons::Meter), tr("ตั้งค่า LED Meter..."));
            connect(act, &QAction::triggered, this, [this]() {
                QMetaObject::invokeMethod(synthMix, "showVuDlg");
            });
            adv->addSeparator();
            act = adv->addAction(MenuIcons::make(MenuIcons::Reset), tr("รีเซ็ตมิกเซอร์"));
            connect(act, &QAction::triggered, this, [this]() {
                QMetaObject::invokeMethod(synthMix, "resetChannel");
            });
        }
        QAction *saveCfg = m->addAction(MenuIcons::make(MenuIcons::Save), tr("บันทึกคอนฟิก (Save config)..."));
        connect(saveCfg, &QAction::triggered, this, [this]() { saveConfigFile(); });
        QAction *openCfg = m->addAction(MenuIcons::make(MenuIcons::Open), tr("เปิดคอนฟิก (Open config)..."));
        connect(openCfg, &QAction::triggered, this, [this]() { openConfigFile(); });
        }
    }

    menu.addAction(&actionSecond);
    menu.addAction(&actionFullScreen);
    { // Screen
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Screen), tr("จอและเนื้อร้อง"));
        // after the menu has closed, so the menu does not stay on screen
        m->addAction(MenuIcons::make(MenuIcons::Screen), tr("ย่อหน้าต่าง (ซ่อนไว้ที่แถบงาน)"), this,
                     [this]() { QTimer::singleShot(0, this, [this]() { minimizeWindow(); }); });
        m->addAction(tr("ปรับตัวหนังสือและเนื้อร้อง..."), this, [this]() {
            settingsStartTab = 1;
            showSettingsDialog();
        });
        m->addSeparator();
        {   // clock and beat box (top right): shown or not
            QAction *clock = m->addAction(tr("แสดงนาฬิกา"));
            clock->setCheckable(true);
            clock->setChecked(settings->value("ShowClock", true).toBool());
            connect(clock, &QAction::triggered, this, [this](bool on) {
                settings->setValue("ShowClock", on);
                applyCornerDisplay();
            });
            QAction *beat = m->addAction(tr("แสดงจังหวะเพลง (ความเร็ว / ห้องดนตรี)"));
            beat->setCheckable(true);
            beat->setChecked(settings->value("ShowRhythm", true).toBool());
            connect(beat, &QAction::triggered, this, [this](bool on) {
                settings->setValue("ShowRhythm", on);
                applyCornerDisplay();
            });
        }
        {   // the karaoke maker's name at the start of songs ("เต่า คาราโอเกะ", "SIB KARAOKE")
            QAction *br = m->addAction(tr("ชื่อผู้ทำคาราโอเกะตอนต้นเพลง แสดงเป็น PTHK KARAOKE (ซ่อนเบอร์โทร)"));
            br->setCheckable(true);
            br->setChecked(settings->value("Lyrics/BrandOn", true).toBool());
            br->setToolTip(tr("เช่น \"......เต่า คาราโอเกะ......\" หรือ \"SIB KARAOKE\" ในเนื้อร้อง  มีผลตั้งแต่เพลงถัดไป"));
            connect(br, &QAction::triggered, this, [this](bool on) {
                settings->setValue("Lyrics/BrandOn", on);
                LyricsWidget::brandText() = on ? settings->value("Lyrics/BrandText", "PTHK KARAOKE").toString() : QString();
            });
        }
        m->addAction(MenuIcons::make(MenuIcons::Video), tr("พื้นหลัง (สี / รูปภาพ / สไลด์ภาพ / วิดีโอ)..."), this,
                     [this]() { QTimer::singleShot(0, this, [this]() { showSettingsDialog(); }); });
        {   // scrolling text: the shop's messages and the next songs (top edge of both screens)
            QAction *tk = m->addAction(tr("แสดงข้อความวิ่ง"));
            tk->setCheckable(true);
            tk->setChecked(settings->value("Ticker/On", false).toBool());
            connect(tk, &QAction::triggered, this, [this](bool on) {
                settings->setValue("Ticker/On", on);
                updateTicker();
            });
            m->addAction(tr("ตั้งค่าข้อความวิ่ง (ข้อความ / เพลงถัดไป / สี)..."), this,
                         [this]() { QTimer::singleShot(0, this, [this]() { openTickerSettings(); }); });
        }
        m->addSeparator();
        m->addSeparator();
        {
            QMenu *ms = m->addMenu(MenuIcons::make(MenuIcons::Fullscreen), tr("ขนาดเมนู"));
            const QStringList labels = { tr("ปกติ"), tr("ใหญ่"), tr("ใหญ่มาก") };
            QActionGroup *grp = new QActionGroup(ms);
            for (int i = 0; i < labels.size(); ++i) {
                QAction *a = ms->addAction(labels[i]);
                a->setCheckable(true);
                a->setChecked(Theme::menuSize() == i);
                grp->addAction(a);
                connect(a, &QAction::triggered, this, [this, i]() {
                    QTimer::singleShot(0, this, [this, i]() { Theme::setMenuSize(i); fitControlBar(); });
                });
            }
        }

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

    menu.addSeparator();
    {
        QAction *lk = menu.addAction(MenuIcons::make(MenuIcons::Lock), tr("ล็อกหน้าจอสำหรับร้าน\tCtrl+L"));
        lk->setCheckable(true);
        lk->setChecked(isShopLocked());
        connect(lk, &QAction::triggered, this, [this]() {
            QTimer::singleShot(0, this, [this]() { toggleShopLock(); });
        });
    }
    { // Phones and stage lights
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Phone), tr("โหมดร้านและมือถือ"));
        {   // idle background music
            QAction *idOn = m->addAction(tr("เปิดเพลงคลออัตโนมัติเมื่อคิวว่าง"));
            idOn->setCheckable(true);
            idOn->setChecked(settings->value("IdleMusic/On", false).toBool());
            connect(idOn, &QAction::triggered, this, [this](bool on) {
                settings->setValue("IdleMusic/On", on);
                if (!on && idlePlaying)
                    stop();
                idleClock.restart();
                updateBarStatus();
            });
            m->addAction(tr("ตั้งค่าล็อกหน้าจอ / เพลงคลอเมื่อคิวว่าง..."), this,
                         [this]() { QTimer::singleShot(0, this, [this]() { openShopModeSettings(); }); });
        }
        m->addSeparator();
        QAction *act = m->addAction(MenuIcons::make(MenuIcons::QrCode), tr("ควบคุมผ่านมือถือ (QR Code)..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showRemoteDialog()));
        m->addAction(MenuIcons::make(MenuIcons::QrCode), tr("ขอเพลงจากมือถือ (QR ลูกค้า / เจ้าของ)..."),
                     this, [this]() { showSongRequestDialog(); });
        if (!simple) {
            m->addSeparator();
            m->addAction(&actionDmx);
        }
    }

    menu.addSeparator();
    menu.addAction(&actionSettings);
    {
        QAction *helpTop = menu.addAction(MenuIcons::make(MenuIcons::Help), tr("คู่มือการใช้งาน / ปุ่มลัด\tF1"));
        connect(helpTop, &QAction::triggered, this, [this]() { showHelpDialog(); });
    }
    {
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Settings), tr("เครื่องมือและช่วยเหลือ"));
        m->addAction(MenuIcons::make(MenuIcons::Download), tr("สร้างไดรฟ์พกพา (USB / External HD) พร้อมเพลงและค่าตั้ง..."), this, [this]() {
            QTimer::singleShot(0, this, [this]() { openPortableDrive(); });
        });
    // ---- settings, help, about
    m->addAction(MenuIcons::make(MenuIcons::Download), tr("ตรวจหาอัปเดต..."), this, [this]() { showCheckUpdateDialog(); });
    m->addAction(&actionAbout);
    {
        QAction *sm = m->addAction(tr("เมนูแบบง่าย (ลดรายการตั้งค่าเพิ่มเติม)"));
        sm->setToolTip(tr("ซ่อน: เอฟเฟกต์ประสาน/ก้อง มาสเตอริ่ง คอนฟิก และไฟ DMX\nเอาติ๊กออกเพื่อแสดงทั้งหมด"));
        sm->setCheckable(true);
        sm->setChecked(simple);
        connect(sm, &QAction::triggered, this, [this](bool on) { settings->setValue("Menu/Simple", on); });
    }
    }
    menu.addSeparator();
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
    if (!requireShopUnlock(tr("ตั้งค่าระบบ")))
        return;
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
    if (settingsStartTab >= 0) {
        if (QTabWidget *tabs = d.findChild<QTabWidget *>("tabWidget"))
            tabs->setCurrentIndex(qBound(0, settingsStartTab, tabs->count() - 1));
        settingsStartTab = -1;
    }
    d.exec();
}

// A new install with no songs in the library: what to do first, in three buttons
// (shown a few seconds after the start; "ไม่ต้องแสดงอีก" keeps it away for good).
void MainWindow::showFirstRunGuide()
{
    if (settings->value("FirstRun/Hide", false).toBool() || !db || db->isRunning() || db->hasAnySong())
        return;
    if (isShopLocked() || QApplication::activeModalWidget())
        return;
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("ยินดีต้อนรับสู่ PTHK Karaoke"));
    QVBoxLayout *l = new QVBoxLayout(&dlg);
    QLabel *head = new QLabel(tr("<h2>ยังไม่มีเพลงในคลัง</h2>"
        "<p>เลือกวิธีเริ่มต้นได้เลย (ทำทีหลังได้จาก <b>เมนู › คลังเพลง › เลือกโฟลเดอร์คลังเพลงและสแกน</b> หรือปุ่ม <b>เมนู › คลังเพลง › นำเพลงเข้าคลัง</b>)</p>"), &dlg);
    head->setWordWrap(true);
    l->addWidget(head);
    auto big = [&dlg, l](const QString &text) {
        QPushButton *b = new QPushButton(text, &dlg);
        b->setMinimumHeight(52);
        QFont f = b->font();
        f.setPointSizeF(f.pointSizeF() * 1.15);
        b->setFont(f);
        b->setStyleSheet("text-align: left; padding: 6px 14px;");
        l->addWidget(b);
        return b;
    };
    QPushButton *lib = big(tr("1.  มีคลังเพลงคาราโอเกะในเครื่องอยู่แล้ว (NCN / EMK / SIB / วิดีโอ)\n"
                              "     → ชี้โฟลเดอร์คลังเพลง แล้วกด \"ปรับปรุง\""));
    QPushButton *add = big(tr("2.  เพิ่มเพลงทีละไฟล์ หรือทั้งโฟลเดอร์ (mp4 / mp3 / kar / zip ...)\n"
                              "     → ลากไฟล์มาวางบนจอก็ได้"));
    QPushButton *yt  = big(tr("3.  ร้องจาก YouTube ไปก่อน (ต้องต่ออินเทอร์เน็ต)  F10"));
    QCheckBox *never = new QCheckBox(tr("ไม่ต้องแสดงหน้านี้อีก"), &dlg);
    l->addSpacing(6);
    l->addWidget(never);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    bb->button(QDialogButtonBox::Close)->setText(tr("ปิด"));
    l->addWidget(bb);
    int choice = 0;
    connect(lib, &QPushButton::clicked, &dlg, [&]() { choice = 1; dlg.accept(); });
    connect(add, &QPushButton::clicked, &dlg, [&]() { choice = 2; dlg.accept(); });
    connect(yt,  &QPushButton::clicked, &dlg, [&]() { choice = 3; dlg.accept(); });
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
    if (never->isChecked())
        settings->setValue("FirstRun/Hide", true);
    setFocus();
    if (choice == 1) {
        settingsStartTab = 2;               // "ฐานข้อมูลเพลง"
        showSettingsDialog();
    } else if (choice == 2) {
        showImportDialog();
    } else if (choice == 3) {
        showYouTubeSearch();
    }
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
    showMidiMapper();       // the MIDI Mapper replaced the old channel table
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

void MainWindow::applySlideshowFromSettings(Background *bg)
{
    Background::SlideshowOptions o;
    for (const QString &f : settings->value("Slideshow/Files").toStringList())
        o.files << Config::relocatePath(f);
    o.dir = Config::relocatePath(settings->value("Slideshow/Dir", "").toString());
    o.intervalSec = settings->value("Slideshow/IntervalSec", 15).toInt();
    o.transition = settings->value("Slideshow/Effect", true).toBool() ? 3 : 4;   // random effect / none
    o.transitionMs = 1200;
    o.shuffle = settings->value("Slideshow/Shuffle", false).toBool();
    o.fill = settings->value("Slideshow/Fill", true).toBool();
    bg->setSlideshow(o);
}

// Background pictures: a list of pictures that change every few seconds (simple, like other
// karaoke players): add / remove / clear / move up / down, effect, random order, full screen, delay.
void MainWindow::showSlideshowDialog()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("ภาพพื้นหลัง (สไลด์ภาพ)"));
    dlg.resize(620, 480);
    QVBoxLayout *v = new QVBoxLayout(&dlg);

    // top row: three ticks + delay, then the list buttons
    QHBoxLayout *top = new QHBoxLayout();
    QVBoxLayout *ticks = new QVBoxLayout();
    QCheckBox *chkEffect = new QCheckBox(tr("เปลี่ยนภาพแบบมีเอฟเฟกต์"), &dlg);
    QCheckBox *chkRandom = new QCheckBox(tr("สุ่มภาพ"), &dlg);
    QCheckBox *chkFull = new QCheckBox(tr("เต็มจอ"), &dlg);
    chkEffect->setChecked(settings->value("Slideshow/Effect", true).toBool());
    chkRandom->setChecked(settings->value("Slideshow/Shuffle", false).toBool());
    chkFull->setChecked(settings->value("Slideshow/Fill", true).toBool());
    ticks->addWidget(chkEffect);
    ticks->addWidget(chkRandom);
    ticks->addWidget(chkFull);
    top->addLayout(ticks);

    QVBoxLayout *delayBox = new QVBoxLayout();
    delayBox->addWidget(new QLabel(tr("เปลี่ยนภาพทุก (วินาที)"), &dlg));
    QSpinBox *spinDelay = new QSpinBox(&dlg);
    spinDelay->setRange(2, 3600);
    spinDelay->setValue(settings->value("Slideshow/IntervalSec", 15).toInt());
    spinDelay->setMinimumWidth(80);
    delayBox->addWidget(spinDelay);
    delayBox->addStretch();
    top->addSpacing(16);
    top->addLayout(delayBox);
    top->addStretch();

    auto mkBtn = [&dlg](const QString &text, const QString &tip) {
        QPushButton *b = new QPushButton(text, &dlg);
        b->setToolTip(tip);
        b->setAutoDefault(false);
        b->setMinimumHeight(36);
        return b;
    };
    QPushButton *btnAdd = mkBtn(tr("+ เพิ่มภาพ"), tr("เลือกไฟล์ภาพ (เลือกได้ทีละหลายไฟล์)"));
    QPushButton *btnRemove = mkBtn(tr("- ลบ"), tr("ลบภาพที่เลือกออกจากรายการ (ไฟล์จริงไม่ถูกลบ)"));
    QPushButton *btnClear = mkBtn(tr("ล้างทั้งหมด"), tr("เอาภาพทั้งหมดออกจากรายการ"));
    QPushButton *btnDown = mkBtn(tr("ลง"), tr("เลื่อนภาพที่เลือกลง"));
    QPushButton *btnUp = mkBtn(tr("ขึ้น"), tr("เลื่อนภาพที่เลือกขึ้น"));
    btnAdd->setStyleSheet("QPushButton{font-weight:bold;color:#16a34a;}");
    btnRemove->setStyleSheet("QPushButton{font-weight:bold;color:#dc2626;}");
    for (QPushButton *b : { btnAdd, btnRemove, btnClear, btnDown, btnUp })
        top->addWidget(b);
    v->addLayout(top);

    // the list, with small pictures
    QListWidget *list = new QListWidget(&dlg);
    list->setIconSize(QSize(96, 54));
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setSpacing(2);
    v->addWidget(list, 1);
    QLabel *info = new QLabel(&dlg);
    info->setStyleSheet("color: gray;");
    v->addWidget(info);

    auto refreshInfo = [=]() {
        info->setText(list->count() == 0
                      ? tr("ยังไม่มีภาพ  -  กด \"+ เพิ่มภาพ\" (รองรับ JPG, PNG, BMP, GIF, WEBP, TIFF)")
                      : tr("มีภาพ %1 ภาพ  -  เลือกแล้วกด ขึ้น/ลง เพื่อเรียงลำดับ").arg(list->count()));
    };
    auto addFile = [=](const QString &f) {
        QListWidgetItem *it = new QListWidgetItem(QFileInfo(f).fileName(), list);
        it->setData(Qt::UserRole, f);
        it->setToolTip(QDir::toNativeSeparators(f));
        QImageReader r(f);          // small picture for the list (read already reduced)
        r.setAutoTransform(true);
        const QSize sz = r.size();
        if (sz.isValid())
            r.setScaledSize(sz.scaled(QSize(96, 54), Qt::KeepAspectRatio));
        const QImage thumb = r.read();
        if (!thumb.isNull())
            it->setIcon(QIcon(QPixmap::fromImage(thumb)));
    };
    for (const QString &f : settings->value("Slideshow/Files").toStringList()) {
        const QString path = Config::relocatePath(f);
        if (QFile::exists(path))
            addFile(path);
    }
    refreshInfo();

    connect(btnAdd, &QPushButton::clicked, &dlg, [&, addFile, refreshInfo]() {
        QStringList pats;
        for (const QString &e : Background::imageExtensions())
            pats << "*." + e;
        const QStringList files = QFileDialog::getOpenFileNames(&dlg, tr("เลือกภาพพื้นหลัง"), Utils::LAST_OPEN_DIR,
                                                                tr("ไฟล์ภาพ (%1)").arg(pats.join(' ')));
        if (files.isEmpty())
            return;
        Utils::LAST_OPEN_DIR = QFileInfo(files.first()).absolutePath();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        for (const QString &f : files)
            addFile(QDir::fromNativeSeparators(f));
        QApplication::restoreOverrideCursor();
        refreshInfo();
    });
    connect(btnRemove, &QPushButton::clicked, &dlg, [=]() {
        for (QListWidgetItem *it : list->selectedItems())
            delete it;
        refreshInfo();
    });
    connect(btnClear, &QPushButton::clicked, &dlg, [=]() {
        list->clear();
        refreshInfo();
    });
    auto move = [=](int dir) {
        const int row = list->currentRow();
        const int to = row + dir;
        if (row < 0 || to < 0 || to >= list->count())
            return;
        QListWidgetItem *it = list->takeItem(row);
        list->insertItem(to, it);
        list->setCurrentRow(to);
    };
    connect(btnUp, &QPushButton::clicked, &dlg, [=]() { move(-1); });
    connect(btnDown, &QPushButton::clicked, &dlg, [=]() { move(1); });

    QHBoxLayout *okRow = new QHBoxLayout();
    QPushButton *btnOk = new QPushButton(tr("ตกลง"), &dlg);
    btnOk->setMinimumSize(140, 40);
    btnOk->setDefault(true);
    okRow->addStretch();
    okRow->addWidget(btnOk);
    okRow->addStretch();
    v->addLayout(okRow);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return;

    QStringList files;
    for (int i = 0; i < list->count(); ++i)
        files << list->item(i)->data(Qt::UserRole).toString();
    settings->setValue("Slideshow/Files", files);
    settings->setValue("Slideshow/Effect", chkEffect->isChecked());
    settings->setValue("Slideshow/Shuffle", chkRandom->isChecked());
    settings->setValue("Slideshow/Fill", chkFull->isChecked());
    settings->setValue("Slideshow/IntervalSec", spinDelay->value());

    Background::BackgroundType type;
    if (!files.isEmpty()) {
        type = Background::Slideshow;
    } else {                    // no pictures: back to the single background picture
        type = Background::Image;
        settings->remove("Slideshow/Dir");
    }
    settings->setValue("BackgroundType", int(type));
    QList<Background *> bgs { bgWidget };
    if (secondMonitor != nullptr && secondLyr != nullptr)
        bgs << secondMonitor->backgroundWidget();
    for (Background *bg : bgs) {
        if (type == Background::Slideshow)
            applySlideshowFromSettings(bg);
        bg->setBackgroundType(type);
    }
}

// Control bar in a small window: rather than squeezing the labels on top of each other,
// the less used buttons move into "เพิ่มเติม..." (first the ones least needed while singing).
void MainWindow::setBackgroundVideoFolder(const QString &dir)
{
    bgWidget->setBackgroundVideoDir(dir);
    bgWidget->setBackgroundType(Background::Video);
    settings->setValue("BackgroundType", int(Background::Video));
    settings->setValue("BackgroundVideoDir", dir);
    if (secondMonitor != nullptr && secondLyr != nullptr) {
        secondMonitor->backgroundWidget()->setBackgroundVideoDir(dir);
        secondMonitor->backgroundWidget()->setBackgroundType(Background::Video);
    }
}

bool MainWindow::simpleMenu() const
{
    return settings->value("Menu/Simple", true).toBool();
}

void MainWindow::fitControlBar()
{
    QLayout *bar = ui->frameControl->layout();
    if (!btnBarMore || !bar)
        return;
    // sizes follow the menu size; buttons that do not fit go into "more" (in barButtons order)
    for (QWidget *w : ui->frameControl->findChildren<QWidget *>())
        w->updateGeometry();
    for (QAbstractButton *b : barButtons) {
        b->setProperty("barOverflow", false);
        b->show();
    }
    btnBarMore->show();                     // always there: opens the main menu
    auto need = [&]() {
        bar->invalidate();
        bar->activate();
        return bar->minimumSize().width();
    };
    const int avail = ui->frameControl->width();
    int hidden = 0;
    if (need() > avail) {
        for (QAbstractButton *b : barButtons) {
            if (need() <= avail)
                break;
            b->hide();
            b->setProperty("barOverflow", true);
            ++hidden;
        }
    }
    ui->frameControl->setFixedHeight(bar->sizeHint().height());
    uiLog(QString("control bar: width %1, needs %2, %3 button(s) in the more menu").arg(avail).arg(need()).arg(hidden));
}

void MainWindow::minimizeWindow()
{
    // keep full screen: clicking the taskbar button brings it back full screen
    setWindowState(windowState() | Qt::WindowMinimized);
}

void MainWindow::showMapSFDialog()
{
    MultiSoundfontDialog dlg(dialogParent(), player->midiSynthesizer());
    dlg.exec();
    setFocus();
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
        if (bgWidget->backgroundType() == Background::Slideshow)
            bg->setSlideshow(bgWidget->slideshow());

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
