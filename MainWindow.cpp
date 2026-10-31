// MainWindow: construction / destruction, play(), help.
// The rest of MainWindow is split by topic into MainWindow_*.cpp (see CLAUDE.md).
#include "MainWindow_p.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{

    // List audio device and Init BASS
    {
        QMap<int, QString> dvs;
        int a, count=0;
        BASS_DEVICEINFO info;
        for (a=0; BASS_GetDeviceInfo(a, &info); a++)
        {
            #ifdef __linux__
            if (info.flags&BASS_DEVICE_ENABLED && BASS_Init(a, 44100, BASS_DEVICE_SPEAKERS, NULL, NULL)) { // device is enabled
                dvs[a] =  QString(info.name);
                count++; // count it
            }
            #else
            if (info.flags&BASS_DEVICE_ENABLED) { // device is enabled
                BASS_Init(a, 44100, BASS_DEVICE_SPEAKERS, NULL, NULL);
                dvs[a] =  QString(info.name);
                count++; // count it
            }
            #endif
        }
        MidiSynthesizer::audioDevices(dvs);
    }

    BASS_SetDevice(1);
    BASS_FX_GetVersion();

    auto concurentThreadsSupported = Utils::concurentThreadsSupported();
    float nVoices = (concurentThreadsSupported > 1) ? 500 : 256;

    BASS_SetConfig(BASS_CONFIG_BUFFER, 100);
    BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 10);
    BASS_SetConfig(BASS_CONFIG_MIDI_VOICES, nVoices);
    BASS_SetConfig(BASS_CONFIG_MIDI_COMPACT, true);
    // End Init BASS


    bgWidget = new Background(this);
    videoWidget = new FrameVideoWidget(this);  // karaoke video songs: above the background, below the lyrics
    videoWidget->hide();
    videoWidget->onFrameShown = [this](const QImage &img) {     // mirror on the 2nd screen
        if (secondLyr != nullptr && videoActive && !mediaIsAudio) {
            QPixmap sharedPm = QPixmap::fromImage(img);
            videoWidget->setPixmap(sharedPm);
            secondMonitor->setVideoPixmap(sharedPm);
        }
    };
    lyrWidget = new LyricsWidget(this);
    updateDetail = new Detail(this);

    ui->setupUi(this);

    #ifdef _WIN32
    taskbarButton = new QWinTaskbarButton();
    #endif

    settings = new QSettings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);

    // lang
    QString lang = settings->value("Language", "th").toString();
    if (lang == "en")
        setEngLang();

    const QString appSongs = QDir::fromNativeSeparators(qApp->applicationDirPath()) + "/Songs";
    const bool useAppSongs = Config::isPortableMode() || !appSongs.contains("Program Files", Qt::CaseInsensitive);
    QString ncn = Config::relocatePath(settings->value("NCNPath", appSongs + "/NCN").toString());
    settings->setValue("NCNPath", ncn);
    QString hnk = Config::relocatePath(settings->value("HNKPath", appSongs + "/HNK").toString());
    settings->setValue("HNKPath", hnk);
    // (default under Music: the program folder in Program Files is not writable for "Add songs")
    QString kar = settings->value("KARPath", QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
                                            + "/PTHK Karaoke Songs/KAR").toString();

    db = new SongDatabase();
    kar = Config::relocatePath(useAppSongs && !settings->contains("KARPath") ? (appSongs + "/KAR") : kar);
    settings->setValue("KARPath", kar);
    QString sib = Config::relocatePath(settings->value("SIBPath", "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB").toString());
    settings->setValue("SIBPath", sib);
    db->setNcnPath(ncn);
    db->setHNKPath(hnk);
    db->setKarPath(kar);
    db->setSibPath(sib);
    if (db->songPaths("SIB").isEmpty() && QFile::exists("D:/REA/SIBSIB KARAOKE/DATA/sibsib_songs.dat")) {
        db->importSibDatabase("D:/REA/SIBSIB KARAOKE/DATA/sibsib_songs.dat", sib);
    }


    timer1 = new QTimer();
    timer2 = new QTimer();
    timer2->setSingleShot(true);

    positionTimer = new QTimer(this);
    positionTimer->setInterval(80); // 12.5 FPS for slider and background, saving GUI cycles

    lyricsTimer = new QTimer(0);
    lyricsTimer->setTimerType(Qt::PreciseTimer);
    lyricsTimer->setInterval(25); // 40 FPS for smooth lyrics sweep

    detailTimer = new QTimer();
    detailTimer->setSingleShot(true);

    player = new MidiPlayer();

    locale = QLocale(QLocale::English, QLocale::UnitedStates);

    { // Channel Mixer
        bool lck = false; settings->setValue("ChMixLock", false);

        ui->chMix->setLock(lck);
        ui->chMix->setPlayer(player);

        if (lck) {
            ui->chMix->setLock(true);
            ui->chMix->hide();
            ui->expandChMix->hide();
        }
        else {
            ui->chMix->setLock(false);
            ui->chMix->hide();
            ui->expandChMix->hide();
        }

        connect(ui->chMix, SIGNAL(lockChanged(bool)), this, SLOT(onChMixLockChanged(bool)));
        connect(ui->chMix, SIGNAL(mouseLeaved()), this, SLOT(onChMixMouseLeaved()));
    }


    { // UI window size
        int sTime = settings->value("SearchTimeout", 5).toInt();
        int pTime = settings->value("PlaylistTimeout", 5).toInt();
        setSearchTimeout(sTime);
        setPlaylistTimeout(pTime);

        bool removeList  = settings->value("RemoveFromPlaylist", true).toBool();
        bool aPlayNext   = settings->value("AutoPlayNext", true).toBool();
        remove_playlist = removeList;
        auto_playnext = aPlayNext;

        // default: the PTHK Karaoke stage picture (built in), also when the chosen picture
        // is missing (e.g. another computer, or a flash drive without that file)
        int bg = settings->value("BackgroundType", int(Background::Image)).toInt();
        QString bgColor = settings->value("BackgroundColor", "#515151").toString();
        QString bgImg = Config::relocatePath(settings->value("BackgroundImage", "").toString());
        if (bgImg.isEmpty() || !QFile::exists(bgImg))
            bgImg = QStringLiteral(":/Images/default-background.png");
        bgWidget->setBackgroundType((Background::BackgroundType)bg);
        bgWidget->setBackgroundColor(bgColor);
        bgWidget->setBackgroundImage(bgImg);
        if (bg == Background::Video)
            bgWidget->setBackgroundVideoDir(Config::relocatePath(settings->value("BackgroundVideoDir", "").toString()));

        int w       = settings->value("WindowWidth", this->minimumWidth()).toInt();
        int h       = settings->value("WindowHeight", this->minimumHeight()).toInt();
        bool maximum= settings->value("WindowMaximized", true).toBool();
        bool full   = settings->value("WindowFullScreen", false).toBool();

        this->resize(w, h);
        // applied by showSavedWindowState() when main() shows the player window
        startMaximized = maximum;
        startFullScreen = full;
    }


    { // Player
        // MIDI-out is remembered by device name; no name, or a device that is
        // not present now, means the SoundFont synthesizer (-1).
        int oPort   = -1;
        QString outName = settings->value("MidiOutName", "").toString();
        if (!outName.isEmpty())
            oPort = MidiPlayer::midiDevices().indexOf(outName);
        settings->setValue("MidiOut", oPort);
        int iPort   = settings->value("MidiIn", -1).toInt();
        // MIDI-in is remembered by device name, because port numbers shift
        // depending on which program/virtual cable was started first.
        QString inName = settings->value("MidiInName", "").toString();
        if (!inName.isEmpty()) {
            int idx = MidiPlayer::midiInDevices().indexOf(inName);
            if (idx >= 0)
                iPort = idx;
        }
        int vl      = settings->value("MidiVolume", 50).toInt();
        bool lDrum  = settings->value("MidiLockDrum", false).toBool();
        bool lSnare = settings->value("MidiLockSnare", false).toBool();
        bool lKick  = settings->value("MidiLockKick", false).toBool();
        bool lBass  = settings->value("MidiLockBass", false).toBool();
        int synBuf  = settings->value("SynthBuffer", 100).toInt();

        BASS_SetConfig(BASS_CONFIG_BUFFER, synBuf);
        player->setMidiOut(oPort);
        player->setMidiIn(iPort);
        player->setVolume(vl);
        player->setMasterMute(settings->value("MasterMute", false).toBool());
        player->midiSynthesizer()->setIgnoreDrumPitch(settings->value("IgnoreDrumPitch", true).toBool());

        // Auto (re)connect the MIDI-in device by name, so the mixer works no
        // matter whether the karaoke program / virtual MIDI cable starts first.
        QTimer *midiInWatcher = new QTimer(this);
        midiInWatcher->setInterval(2000);
        connect(midiInWatcher, &QTimer::timeout, this, [this]() {
            QString name = settings->value("MidiInName", "").toString();
            if (name.isEmpty())
                return;
            int idx = MidiPlayer::midiInDevices().indexOf(name);
            if (idx < 0) {
                if (player->midiInPortNumber() != -1)
                    player->setMidiIn(-1);
            } else if (idx != player->midiInPortNumber()) {
                player->setMidiIn(idx);
            }
        });
        midiInWatcher->start();

        if (lDrum) {
            int ldNum = settings->value("MidiLockDrumNumber", 0).toInt();
            player->setLockDrum(true, ldNum);
        }
        if (lSnare) {
            int lsNum = settings->value("MidiLockSnareNumber", 38).toInt();
            player->setLockSnare(true, lsNum);
        }
        if (lKick) {
            int lkNum = settings->value("MidiLockKickNumber", 36).toInt();
            player->setLockKick(true, lkNum);
        }
        if (lBass) {
            int lbNum = settings->value("MidiLockBassNumber", 32).toInt();
            player->setLockBass(true, lbNum);
        }

        // Midi Channel Mapper
        QList<int> ports = settings->value("MidiChannelMapper").value<QList<int>>();
        if (ports.count() == 16) {
            for (int i=0; i<16; i++) {
                // saved port numbers may point to another device after a restart,
                // so every channel follows the selected MIDI out (oPort)
                Q_UNUSED(ports)
                player->setMapChannelOutput(i, player->midiOutPortNumber());
            }
        }

        connect(player, SIGNAL(finished()), this, SLOT(onPlayerThreadFinished()));
        connect(player, SIGNAL(bpmChanged(int)), ui->rhmWidget, SLOT(setBpm(int)));

        connect(player, SIGNAL(nextMedleyStarted()), lyricsTimer, SLOT(stop()));
        connect(player, SIGNAL(nextMedleyStarted()), this, SLOT(onNextMedleyStarted()));
        connect(player, SIGNAL(nextMedleyStarted()), this, SLOT(switchMedleyLyrics()));
        connect(player, SIGNAL(nextMedleyStarted()), this, SLOT(switchMedleyLyrics2()));
        connect(player, SIGNAL(nextMedleyAfterStarted()), lyricsTimer, SLOT(start()));
        connect(player, SIGNAL(nextMedleyAfterStarted()), this, SLOT(onNextMedleyAfterStarted()));
        connect(player, SIGNAL(medleyCountIn(int,int,int,int)), this, SLOT(onMedleyCountIn(int,int,int,int)));
    }


    { // Synth
        MidiSynthesizer *synth = player->midiSynthesizer();

        // Audio out
        int aout = settings->value("SynthDefaultDevice", 1).toInt();
        synth->setDefaultDevice(aout);

        // ASIO output (optional, -1 = normal Windows device)
        int asio = settings->value("AsioDevice", -1).toInt();
        if (asio >= AsioOutput::devices().count())
            asio = -1;
        if (AsioOutput::crashedLastStart()) {          // the driver hung / crashed the last time: start on the normal device
            asio = -1;
            settings->setValue("AsioDevice", -1);
        }
        AsioOutput::setPairMask(settings->value("AsioPairMask", 1).toUInt());
        AsioOutput::setSpeakerSplit(settings->value("AsioSpeakerSplit", false).toBool());
        AsioOutput::setBufferSize(settings->value("AsioBufferSamples", 0).toInt());
        synth->setAsioDevice(asio);

        // audio quality
        {
            synth->setVoices(settings->value("SynthVoices", 500).toInt());
            synth->setSampleRate(44100);
            synth->setSincInterpolation(false);
            synth->setReverbStrong(settings->value("ReverbStrong", false).toBool());
        }

        // floating and fx
        bool useFloat = true;    // fixed: 32-bit floating point
        bool useFX = false;      // fixed: the old global reverb / chorus switch stays off
        synth->setUsetFloattingPoint(useFloat);
        synth->setUseFXRC(useFX);


        // Soundfonts and Soundfonts map
        // move to setup in main function (main.cpp)

        // Synth EQ -> move to SynthMixer
        // Synth chorus -> move to SynthMixer
        // Synth reverb -> move to SynthMixer

        // Create synth mixer
        synthMix = new SynthMixerDialog(nullptr, this);
    }


    { // Lyrics
        QString family  = settings->value("LyricsFamily", font().family()).toString();
        int size        = settings->value("LyricsSize", 40).toInt();
        int weight      = settings->value("LyricsWeight", 75).toInt();
        bool italic     = settings->value("LyricsItalic", false).toBool();
        bool strikeOut  = settings->value("LyricsStrikeOut", false).toBool();
        bool underline  = settings->value("LyricsUnderline", false).toBool();

        QString tColor  = settings->value("LyricsTextColor", "#f3f378").toString();
        QString tbColor = settings->value("LyricsTextBorderColor", "#000000").toString();
        QString toColor = settings->value("LyricsTextBorderOutColor", "#000000").toString();
        int     tbWidth = settings->value("LyricsTextBorderWidth", 3).toInt();
        int     toWidth = settings->value("LyricsTextBorderOutWidth", 2).toInt();

        QString cColor  = settings->value("LyricsCurColor", "#0000ff").toString();
        QString cbColor = settings->value("LyricsCurBorderColor", "#ffffff").toString();
        QString coColor = settings->value("LyricsCurBorderOutColor", "#000000").toString();
        int     cbWidth = settings->value("LyricsCurBorderWidth", 4).toInt();
        int     coWidth = settings->value("LyricsCurBorderOutWidth", 2).toInt();

        int     line1X  = settings->value("LyricsLine1X", 0).toInt();
        int     line2X  = settings->value("LyricsLine2X", 0).toInt();
        int     line1Y  = settings->value("LyricsLine1Y", 320).toInt();
        int     line2Y  = settings->value("LyricsLine2Y", 170).toInt();
        int     aTime   = settings->value("LyricsAnimationTime", 250).toInt();
        bool   autosize = settings->value("LyricsAutoFontSize", true).toBool();

        QFont f;
        f.setFamily(family);
        f.setPointSize(size);
        f.setWeight(weight);
        f.setItalic(italic);
        f.setStrikeOut(strikeOut);
        f.setUnderline(underline);

        lyrWidget->setTextFont(f);
        lyrWidget->setTextColor(QColor(tColor));
        lyrWidget->setTextBorderColor(QColor(tbColor));
        lyrWidget->setTextBorderOutColor(QColor(toColor));
        lyrWidget->setTextBorderWidth(tbWidth);
        lyrWidget->setTextBorderOutWidth(toWidth);

        lyrWidget->setCurColor(QColor(cColor));
        lyrWidget->setCurBorderColor(QColor(cbColor));
        lyrWidget->setCurBorderOutColor(QColor(coColor));
        lyrWidget->setCurBorderWidth(cbWidth);
        lyrWidget->setCurBorderOutWidth(coWidth);

        lyrWidget->setLine1Position(static_cast<LinePosition>(line1X));
        lyrWidget->setLine2Position(static_cast<LinePosition>(line2X));
        lyrWidget->setLine1Y(line1Y);
        lyrWidget->setLine2Y(line2Y);
        lyrWidget->setAnimationTime(aTime);
        lyrWidget->setAutoFontSize(autosize);
    }


    { // Video and MP3 songs (both played by videoPlayer); default folders under Music (writable,
      // unlike the program folder in Program Files) so "Add songs" can copy files there
        QString songs = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/PTHK Karaoke Songs";
        const QString defVideo = (Config::isPortableMode() || !qApp->applicationDirPath().contains("Program Files", Qt::CaseInsensitive)) ? (QDir::fromNativeSeparators(qApp->applicationDirPath()) + "/Songs/VIDEO") : (songs + "/VIDEO");
        const QString vPath = Config::relocatePath(settings->value("VideoPath", defVideo).toString());
        settings->setValue("VideoPath", vPath);
        db->setVideoPath(vPath);
        const QString defMp3 = (Config::isPortableMode() || !qApp->applicationDirPath().contains("Program Files", Qt::CaseInsensitive)) ? (QDir::fromNativeSeparators(qApp->applicationDirPath()) + "/Songs/MP3") : (songs + "/MP3");
        const QString mPath = Config::relocatePath(settings->value("MP3Path", defMp3).toString());
        settings->setValue("MP3Path", mPath);
        db->setMp3Path(mPath);
        setAcceptDrops(true);

        videoPlayer = new QMediaPlayer(this);
        videoPlayer->setVideoOutput(videoWidget->videoSurface());

        connect(videoPlayer, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
            if (!videoActive)
                return;
            ui->sliderPosition->setMaximum(int(d));
            onPlayerDurationMSChanged(d);
        });
        connect(videoPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
            if (!videoActive)
                return;
            if (videoAudioTempo) {
                DWORD flags = BASS_Mixer_ChannelFlags(videoAudioTempo, 0, 0);
                if (flags & BASS_MIXER_CHAN_PAUSE) {
                    if (videoPlayer->state() == QMediaPlayer::PlayingState) {
                        BASS_Mixer_ChannelSetPosition(videoAudioTempo,
                            BASS_ChannelSeconds2Bytes(videoAudioTempo, p / 1000.0), BASS_POS_BYTE);
                        BASS_Mixer_ChannelFlags(videoAudioTempo, 0, BASS_MIXER_CHAN_PAUSE);
                    }
                } else if (p > 500) {
                    const qint64 aPos = videoAudioPositionMs();
                    if (std::abs(p - aPos) > 250) {
                        BASS_Mixer_ChannelSetPosition(videoAudioTempo,
                            BASS_ChannelSeconds2Bytes(videoAudioTempo, p / 1000.0), BASS_POS_BYTE);
                    }
                }
            }
            if (!ui->sliderPosition->isSliderDown())
                ui->sliderPosition->setValue(int(p));
            onPlayerPositionMSChanged(p);
            if (mediaIsAudio) {     // MP3 + LRC: the lyric cursors are in milliseconds
                lyrWidget->setPositionCursor(int(p));
                checkEndAfterLyrics(p, videoPlayer->duration() - p);
            }
        });
        videoPlayer->setNotifyInterval(40);

        // MP3 through the main mixer (BASS): position, lyrics and end of song
        mp3Timer = new QTimer(this);
        mp3Timer->setInterval(40);
        connect(mp3Timer, &QTimer::timeout, this, [this]() {
            if (!mp3Stream)
                return;
            const qint64 p = mp3PositionMs();
            if (!ui->sliderPosition->isSliderDown())
                ui->sliderPosition->setValue(int(p));
            onPlayerPositionMSChanged(p);
            lyrWidget->setPositionCursor(int(p));
            if (BASS_ChannelIsActive(mp3Stream) == BASS_ACTIVE_STOPPED) {      // decoded to the end
                mp3Timer->stop();
                QTimer::singleShot(0, this, [this]() { songFinished(); });
            } else {
                QWORD len = BASS_ChannelGetLength(mp3Stream, BASS_POS_BYTE);
                checkEndAfterLyrics(p, qint64(BASS_ChannelBytes2Seconds(mp3Stream, len) * 1000) - p);
            }
        });
        connect(videoPlayer, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (videoActive && s == QMediaPlayer::EndOfMedia)
                songFinished();
        });
        connect(videoPlayer, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this](QMediaPlayer::Error) {
            if (!videoActive)
                return;
            QString err = videoPlayer->errorString();
            if (err.isEmpty())
                err = tr("ไฟล์เสียหาย หรือ Windows ไม่มีตัวถอดรหัสสำหรับไฟล์นี้");
            stop();
            QMessageBox::warning(this, tr("ไม่สามารถเล่นวิดีโอได้"), err);
        });
    }


    { // song transitions: fade timer
        loadTransitionSettings();
        fadeTimer = new QTimer(this);
        fadeTimer->setInterval(40);
        connect(fadeTimer, &QTimer::timeout, this, [this]() {
            const double t = fadeMs > 0 ? qMin(1.0, fadeClock.elapsed() / double(fadeMs)) : 1.0;
            fadeGain = fadeFrom + (fadeTo - fadeFrom) * t;
            applyVolume();
            if (t >= 1.0) {
                fadeTimer->stop();
                std::function<void()> done = fadeDone;
                fadeDone = nullptr;
                if (done)
                    done();
            }
        });
    }


    { // DMX stage lights
        dmxLights = new DmxLights(this);
        dmxLights->configure(DmxLights::Mode(settings->value("DmxMode", 0).toInt()),
                             settings->value("DmxPort", "").toString(),
                             settings->value("DmxStartChannel", 1).toInt(),
                             settings->value("DmxFixtures", 4).toInt());
    }


    { // Vocal score: compare the microphone pitch with the vocal melody (MIDI channel 9) every 50 ms
        scoreLabel = new QLabel(this);
        scoreLabel->setAlignment(Qt::AlignCenter);
        scoreLabel->setStyleSheet("QLabel { color: #ffe066; background-color: rgba(0, 0, 0, 170);"
                                  " border-radius: 24px; padding: 24px 48px; }");
        QFont sf = scoreLabel->font();
        sf.setPointSize(48);
        sf.setBold(true);
        scoreLabel->setFont(sf);
        scoreLabel->hide();

        scoreTimer = new QTimer(this);
        scoreTimer->setInterval(50);
        connect(scoreTimer, &QTimer::timeout, this, [this]() {
            if (melodyNote < 0)
                return;
            scoreFrames++;
            const float hz = MicInput::get().pitchHz();
            if (hz <= 0.0f)
                return;
            const double sung = 69.0 + 12.0 * std::log2(hz / 440.0);
            double d = std::fmod(std::fabs(sung - melodyNote), 12.0);     // octave does not matter
            d = qMin(d, 12.0 - d);
            if (d <= 1.0)
                scoreHits++;
        });

        connect(player, &MidiPlayer::sendedEvent, this, [this](MidiEvent e) {
            if (e.channel() == 9 && e.eventType() == MidiEventType::NoteOn && e.data2() > 0) {
                if (MidiPlayer::isKickNumber(e.data1()))
                    dmxLights->kick();
                else if (MidiPlayer::isSnareNumber(e.data1()))
                    dmxLights->snare();
                return;
            }
            if (e.channel() != 8)
                return;
            const int note = e.data1() + player->transpose();
            if (e.eventType() == MidiEventType::NoteOn && e.data2() > 0)
                melodyNote = note;
            else if ((e.eventType() == MidiEventType::NoteOff || e.eventType() == MidiEventType::NoteOn)
                     && note == melodyNote)
                melodyNote = -1;
        });

        setVocalScore(settings->value("VocalScore", false).toBool());
    }


    { // Init UI
        this->setWindowTitle("PTHK Karaoke  " + QApplication::applicationVersion());

        Utils::LAST_OPEN_DIR = settings->value("LastOpenDir", QDir::homePath()).toString();

        updateDetail->hide();
        updateDetail->resize(250, 60);
        updateDetail->setText("กำลังปรับปรุงฐานข้อมูล");

        ui->detail->hide();
        ui->frameSearch->hide();
        ui->playlistWidget->hide();
        ui->songDetail->hide();
        ui->songMedley->hide();
        ui->sliderVolume->setValue(player->volume());

        // 249
        ui->playlistWidget->setMinimumHeight(305);
        ui->playlistWidget->setMaximumHeight(305);

        { // Playback control
            ui->btnPlay->setIconFiles(":/Icons/play-white", ":/Icons/play-blue");
            ui->btnPause->setIconFiles(":/Icons/pause-white", ":/Icons/pause-blue");
            ui->btnStop->setIconFiles(":/Icons/stop-white", ":/Icons/stop-blue");
            ui->btnPrevious->setIconFiles(":/Icons/previous-white", ":/Icons/previous-blue");
            ui->btnNext->setIconFiles(":/Icons/next-white", ":/Icons/next-blue");

            ui->btnVolumeMute->setIconFiles(":/Icons/volume-white", ":/Icons/volume-blue");
        }

        #ifdef __linux__
        { // UI Font in linux
            QString fontName = "TH Sarabun New Bold";
            QFont f = ui->lbSearch->font();
            f.setBold(true);
            f.setFamily(fontName);
            f.setPointSize(f.pointSize() - 12);

            ui->lbSearch->setFont(f);
            ui->lbArtist->setFont(f);
            ui->lbBtw->setFont(f);
            ui->lbId->setFont(f);
            ui->lbName->setFont(f);
            ui->lbTempoKey->setFont(f);
            ui->lbType->setFont(f);

            f.setPointSize(f.pointSize() - 4);
            ui->lbLyrics->setFont(f);
        }
        #endif

        connect(db, SIGNAL(started()), updateDetail, SLOT(show()));
        connect(db, SIGNAL(finished()), updateDetail, SLOT(hide()));
        connect(db, SIGNAL(updatePositionChanged(int)), this, SLOT(onDbUpdateChanged(int)));

        connect(timer1, SIGNAL(timeout()), this, SLOT(showCurrentTime()));
        connect(timer2, SIGNAL(timeout()), this, SLOT(hideUIFrame()));

        // SIB songs imported from the collection index have no lyrics preview / real tempo:
        // fill them in from the .sib files in the background (see SibBackfill.h).
        QTimer::singleShot(15000, this, [this]() { startSibBackfill(); });
        {   // automatic update check (settings: Update/Auto, Update/IntervalHours)
            QTimer *upd = new QTimer(this);
            upd->setInterval(30 * 60 * 1000);
            auto due = [this]() {
                if (!settings->value("Update/Auto", true).toBool())
                    return;
                const int hours = qBound(1, settings->value("Update/IntervalHours", 24).toInt(), 24 * 30);
                const QDateTime last = settings->value("Update/LastCheck").toDateTime();
                if (!last.isValid() || last.secsTo(QDateTime::currentDateTime()) >= qint64(hours) * 3600
                        || last > QDateTime::currentDateTime())
                    checkForUpdate(false);
            };
            connect(upd, &QTimer::timeout, this, due);
            upd->start();
            QTimer::singleShot(20000, this, due);
        }
        connect(db, &SongDatabase::finished, this, [this]() { startSibBackfill(); db->startFuzzyIndex(); });
        QTimer::singleShot(3000, this, [this]() { db->startFuzzyIndex(); });   // approximate search

        // Audio songs (YouTube / video / MP3) on the synth mixer: band levels (see AudioBandMeter.h)
        {
            audioMeter = new AudioBandMeter();
            QTimer *meterTimer = new QTimer(this);
            meterTimer->setInterval(40);
            connect(meterTimer, &QTimer::timeout, this, [this]() {
                DWORD h = 0;
#ifdef KM_HAVE_PROCLOOP
                if (ytActive && ytAudio && ytAudio->running())
                    h = ytAudio->outputHandle();
#endif
                if (!h && videoActive && videoAudioTempo)
                    h = videoAudioTempo;
                if (!h && mp3Stream)
                    h = mp3Stream;
                audioMeter->attach(h);
                if (!h || !synthMix || !synthMix->isVisible())
                    return;
                int bands[AudioBandMeter::kBands];
                for (int b = 0; b < AudioBandMeter::kBands; ++b)
                    bands[b] = audioMeter->bandLevel(b);
                synthMix->showAudioBands(bands, AudioBandMeter::kBands, audioMeter->totalLevel());
            });
            meterTimer->start();
        }

        // Drum hold messages (see MidiSequencer::toggleDrumHold)
        connect(player, &MidiPlayer::drumHoldChanged, this, [this](int st) {
            switch (st) {
            case 1:
                ui->detail->setDetail(tr("ค้างจังหวะกลอง"), tr("ดนตรีจะหยุดที่ต้นห้องถัดไป เหลือแต่กลอง"));
                detailTimer->start(4000);
                break;
            case 2:
                ui->detail->setDetail(tr("ค้างจังหวะกลองอยู่"), tr("กด Ctrl+D (หรือ Pause) อีกครั้ง = ลูกส่งแล้วเข้าเพลงต่อ"));
                detailTimer->stop();        // stays until released
                break;
            case 3:
                ui->detail->setDetail(tr("ลูกส่ง"), tr("เข้าเพลงต่อในห้องถัดไป"));
                detailTimer->start(4000);
                break;
            default:
                detailTimer->start(800);
                return;
            }
            ui->detail->show();
        });

        // UI watchdog: logs when the window stops responding for > 120 ms (ui-timing.log)
        {
            QTimer *watch = new QTimer(this);
            watch->setInterval(50);
            QElapsedTimer *last = new QElapsedTimer();
            last->start();
            connect(watch, &QTimer::timeout, this, [this, last]() {
                const qint64 gap = last->restart();
                if (gap > 120) {
                    QStringList what;
                    if (ui->frameSearch->isVisible()) what << "search open";
                    if (ui->playlistWidget->isVisible()) what << "queue open";
                    if (player->isPlayerPlaying()) what << "MIDI playing";
                    if (ytActive) what << "YouTube";
                    if (videoActive) what << "video";
                    if (synthMix && synthMix->isVisible()) what << "mixer open";
                    uiLog(QString("UI stall %1 ms [%2]").arg(gap).arg(what.join(", ")));
                }
            });
            watch->start();
        }

        // Mouse at the top edge of the screen: show the playing song's details (code, name,
        // artist, key, tempo, file type). Polled, because the player doesn't track the mouse.
        {
            QTimer *hoverTimer = new QTimer(this);
            hoverTimer->setInterval(200);
            connect(hoverTimer, &QTimer::timeout, this, [this]() {
                if (!isActiveWindow() || playingSong.id().isEmpty())
                    return;
                const QPoint p = mapFromGlobal(QCursor::pos());
                const bool atTop = p.x() >= 0 && p.x() < width() && p.y() >= 0 && p.y() < qMax(40, height() / 12);
                if (!atTop)
                    return;
                if (ui->frameSearch->isVisible() || ui->playlistWidget->isVisible() || ui->chMix->isVisible())
                    return;     // don't cover what the user opened
                if (!ui->songDetail->isVisible()) {
                    ui->songDetail->setDetail(&playingSong);
                    ui->songDetail->adjustSize();
                    showSongDetail();
                }
                timer2->start(songDetail_timeout);   // stays while the mouse is up there
            });
            hoverTimer->start();
        }

        connect(positionTimer, SIGNAL(timeout()), this, SLOT(onPositiomTimerTimeOut()));
        connect(lyricsTimer, SIGNAL(timeout()), this, SLOT(onLyricsTimerTimeOut()));

        connect(detailTimer, SIGNAL(timeout()), this, SLOT(onDetailTimerTimeout()));

        connect(ui->btnPause, SIGNAL(clicked()), this, SLOT(pause()));
        connect(ui->btnStop, SIGNAL(clicked()), this, SLOT(stop()));
        connect(ui->btnNext, SIGNAL(clicked()), this, SLOT(playNext()));
        connect(ui->btnPrevious, SIGNAL(clicked()), this, SLOT(playPrevious()));
        connect(ui->sliderPosition, SIGNAL(sliderPressed()), this, SLOT(onSliderPositionPressed()));
        connect(ui->sliderPosition, SIGNAL(sliderReleased()), this, SLOT(onSliderPositionReleased()));
        ui->sliderPosition->installEventFilter(this);
        connect(ui->sliderPosition, &QSlider::sliderMoved, this, [this](int val) {
            static QElapsedTimer dragTimer;
            const bool throttle = dragTimer.isValid() && dragTimer.elapsed() < 90;
            if (mp3Stream) {
                onPlayerPositionMSChanged(val);
                lyrWidget->setSeekPositionCursor(val);
                if (!throttle) {
                    dragTimer.start();
                    BASS_Mixer_ChannelSetPosition(mp3Stream, BASS_ChannelSeconds2Bytes(mp3Stream, val / 1000.0), BASS_POS_BYTE);
                }
                return;
            }
            if (videoActive) {
                onPlayerPositionMSChanged(val);
                if (mediaIsAudio)
                    lyrWidget->setSeekPositionCursor(val);
                if (!throttle) {
                    dragTimer.start();
                    videoPlayer->setPosition(val);
                    if (videoAudioTempo)
                        BASS_Mixer_ChannelSetPosition(videoAudioTempo, BASS_ChannelSeconds2Bytes(videoAudioTempo, val / 1000.0), BASS_POS_BYTE);
                }
                return;
            }
#ifdef PTHK_WEBENGINE
            if (ytActive) {
                onPlayerPositionMSChanged(val);
                if (!throttle) {
                    dragTimer.start();
                    ytPlayer->seek(val, true);
#ifdef KM_HAVE_PROCLOOP
                    delayedFrames.clear();
#endif
                }
                return;
            }
#endif
            if (player->midiSequencer() && player->midiSequencer()->midiFile() && !player->midiSequencer()->midiFile()->events().isEmpty()) {
                lyrWidget->setSeekPositionCursor(val);
                if (secondLyr != nullptr)
                    secondLyr->setSeekPositionCursor(val);
                const qint64 ms = qint64(player->midiSequencer()->midiFile()->timeFromTick(val, player->midiSequencer()->bpmSpeed()) * 1000);
                onPlayerPositionMSChanged(ms);
                ui->rhmWidget->setCurrentBeat(player->midiSequencer()->midiFile()->beatFromTick(val));
            }
        });
        connect(ui->sliderVolume, SIGNAL(valueChanged(int)), this, SLOT(onSliderVolumeValueChanged(int)));

        timer1->start(1000);

        { // Extra buttons on the playback bar (same as the Home / End / * keys and the menu)
            auto addBarButton = [this](const QString &text, const QString &tip) {
                QPushButton *b = new QPushButton(text, this);
                b->setFocusPolicy(Qt::NoFocus);     // typing still goes to the song search
                b->setToolTip(tip);
                b->setCursor(Qt::PointingHandCursor);
                b->setStyleSheet("QPushButton { color: white; background-color: #2b2f36; border: 1px solid #4a5060;"
                                 " border-radius: 4px; padding: 2px 8px; }"
                                 "QPushButton:hover { background-color: #3a7bd5; }"
                                 "QPushButton:checked { background-color: #d9534f; border-color: #ff8a80; }"
                                 "QPushButton::menu-indicator { width: 0px; }");
                b->setMinimumWidth(b->sizeHint().width());   // never cut the label; the position slider gives way
                ui->horizontalLayout_8->addWidget(b);
                return b;
            };

            QPushButton *addSongs = addBarButton(tr("+ เพิ่มเพลง"), tr("เพิ่มเพลง NCN / วิดีโอ / MP3 / KAR เข้าคลังเพลงทันที เลือกทั้งโฟลเดอร์ได้ (ลากไฟล์หรือโฟลเดอร์มาวางบนจอก็ได้)"));
            connect(addSongs, &QPushButton::clicked, this, [this]() { showImportDialog(); });

            QPushButton *yt = addBarButton(tr("YouTube"), tr("ค้นหาเพลงจาก YouTube แล้วเล่นสด หรือเพิ่มเข้าคิว (F10)"));
            connect(yt, &QPushButton::clicked, this, [this]() { showYouTubeSearch(); });

            QPushButton *home = addBarButton(tr("เริ่มใหม่"), tr("เล่นเพลงนี้ใหม่ตั้งแต่ต้น (Home)"));
            connect(home, &QPushButton::clicked, this, [this]() {
                if (playingSong.id() != "")
                    play(-1);
            });

            QPushButton *end = addBarButton(tr("จบเพลง"), tr("จบเพลงนี้ ไปเพลงถัดไป (End)"));
            connect(end, &QPushButton::clicked, this, [this]() { endSongSmoothly(); });

            btnMedley = addBarButton(tr("เมดเลย์"), tr("เปิด / ปิด โหมดเมดเลย์ ต่อเพลงเนียนตามจังหวะห้องดนตรี (F4)"));
            btnMedley->setCheckable(true);
            connect(btnMedley, &QPushButton::clicked, this, [this]() {
                QKeyEvent ev(QEvent::KeyPress, Qt::Key_F4, Qt::NoModifier);
                keyPressEvent(&ev);
                QSignalBlocker b(btnMedley);
                btnMedley->setChecked(player->isUseMedley());
            });

            QPushButton *btnMedleySettings = addBarButton(tr("ตั้งค่าเมดเลย์"), tr("ตั้งค่าเมดเลย์: BPM, จำนวนห้องลูกส่งรอ, ลายกลองส่ง"));
            connect(btnMedleySettings, &QPushButton::clicked, this, [this]() { openMedleySettings(); });

            // Drum hold: click once = the band stops at the next bar and the drums keep the beat;
            // click again = drum fill, then the song carries on (same as Ctrl+D / Pause)
            QPushButton *btnDrumHold = addBarButton(tr("ค้างกลอง"),
                tr("ค้างจังหวะกลอง: ดนตรีหยุดที่ต้นห้องถัดไป เหลือกลองตีรอ (เนื้อร้องรอด้วย)\n"
                   "กดอีกครั้ง = ลูกส่ง แล้วเข้าเพลงต่อพอดีจังหวะ (Ctrl+D) - เพลง MIDI"));
            btnDrumHold->setCheckable(true);
            connect(btnDrumHold, &QPushButton::clicked, this, [this, btnDrumHold]() {
                toggleDrumHold();
                QSignalBlocker b(btnDrumHold);
                btnDrumHold->setChecked(player->isDrumHoldOn());
            });
            connect(player, &MidiPlayer::drumHoldChanged, btnDrumHold, [btnDrumHold](int st) {
                static const char *labels[] = { "ค้างกลอง", "ค้างกลอง…", "▶ ส่งเข้าเพลง", "ลูกส่ง…" };
                QSignalBlocker b(btnDrumHold);
                btnDrumHold->setText(QObject::tr(labels[qBound(0, st, 3)]));
                btnDrumHold->setChecked(st != 0);
                btnDrumHold->setMinimumWidth(btnDrumHold->sizeHint().width());
            });

            btnMuteVocal = addBarButton(tr("ตัดเสียงร้อง"), tr("ปิด / เปิด เสียงร้องนำ (*)"));
            btnMuteVocal->setCheckable(true);
            connect(btnMuteVocal, &QPushButton::toggled, this, [this](bool on) {
                if (ui->chMix->isMuteVoice() != on)
                    ui->chMix->setMuteVoice(on);
#ifdef KM_HAVE_PROCLOOP
                applyYtVocalCut();
#endif
                applyVideoVocalCut();
                ui->detail->setDetail(tr("เสียงร้อง "), on ? tr("ปิด") : tr("เปิด"));
                ui->detail->show();
                detailTimer->start(3000);
            });
            connect(ui->chMix, &ChannelMixer::muteVoiceChanged, this, [this](bool on) {
                if (btnMuteVocal && btnMuteVocal->isChecked() != on) {
                    QSignalBlocker block(btnMuteVocal);
                    btnMuteVocal->setChecked(on);
                }
#ifdef KM_HAVE_PROCLOOP
                applyYtVocalCut();
#endif
                applyVideoVocalCut();
            });

            QPushButton *btnMixer = addBarButton(tr("มิกเซอร์"), tr("เปิด / ปิด มิกเซอร์ปรับแต่งเสียงหลัก"));
            connect(btnMixer, &QPushButton::clicked, this, [this]() {
                if (synthMix->isVisible() && !synthMix->isMinimized()) {
                    synthMix->hide();
                } else {
                    synthMix->showNormal();
                    synthMix->raise();
                    synthMix->activateWindow();
                }
            });

            btnSecondScreen = addBarButton(tr("จอที่ 2"), tr("เปิด / ปิด หน้าจอเนื้อร้องบนจอที่สอง (คลิกขวาบนจอที่ 2 เพื่อปรับ)"));
            btnSecondScreen->setCheckable(true);
            connect(btnSecondScreen, &QPushButton::clicked, this, [this]() { showSecondMonitor(); });

            btnSubtitles = addBarButton(tr("ซับไตเติล"), tr("เปิด / ปิด ซับไตเติล YouTube (CC)"));
            btnSubtitles->setCheckable(true);
            const bool showCc = settings->value("YtShowSubtitles", false).toBool();
            btnSubtitles->setChecked(showCc);
            connect(btnSubtitles, &QPushButton::toggled, this, [this](bool on) {
                settings->setValue("YtShowSubtitles", on);
#ifdef PTHK_WEBENGINE
                if (ytPlayer)
                    ytPlayer->setSubtitlesVisible(on);
#endif
                ui->detail->setDetail(tr("ซับไตเติล (CC) "), on ? tr("เปิด") : tr("ปิด"));
                ui->detail->show();
                detailTimer->start(3000);
            });

            QPushButton *full = addBarButton(tr("เต็มจอ"), tr("เต็มจอ / หน้าต่างปกติ"));
            connect(full, &QPushButton::clicked, this, [this]() { showFullScreenOrNormal(); });
        }

        // Every shortcut (Home, End, PageUp/Down, arrows, numbers...) is handled by this window.
        // A clicked slider or button used to keep the keyboard: End then moved the volume slider
        // to maximum instead of ending the song. Nothing in the player takes the focus any more.
        for (QWidget *child : findChildren<QWidget *>())
            child->setFocusPolicy(Qt::NoFocus);
        setFocusPolicy(Qt::StrongFocus);

        // ... and while the mixer window is the active one (even with a fader focused)
        qApp->installEventFilter(this);
    }


    // Menu
    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
}

MainWindow::~MainWindow()
{
    stop();
    delete videoVocalRemover;
    videoVocalRemover = nullptr;


    delete synthMix;

    if (!skipSaveOnExit) {
        settings->setValue("LastOpenDir", Utils::LAST_OPEN_DIR);
        settings->setValue("MidiVolume", player->volume());
    }
    // position + size + monitor, restored by showSavedWindowState()
    settings->setValue("WindowGeometry", saveGeometry());
    if (this->isFullScreen()) {
        settings->setValue("WindowFullScreen", true);
        settings->setValue("WindowMaximized", false);
    } else if (this->isMaximized()) {
        settings->setValue("WindowFullScreen", false);
        settings->setValue("WindowMaximized", true);
    } else {
        settings->setValue("WindowFullScreen", false);
        settings->setValue("WindowMaximized", false);
        settings->setValue("WindowWidth", this->width());
        settings->setValue("WindowHeight", this->height());
    }

    foreach (Song *s, playlist) {
        delete s;
    }

    if (medleyLoader != nullptr) {
        medleyLoader->wait();
    }

    delete player;

    delete detailTimer;

    delete lyricsTimer;
    delete positionTimer;
    delete timer2;
    delete timer1;

    delete db;
    delete settings;

    #ifdef _WIN32
    delete taskbarButton;
    #endif

    delete ui;

    delete updateDetail;
    delete lyrWidget;
    delete bgWidget;

    delete aiStems;          // stops a running separation
    aiStems = nullptr;
    delete sibFill;          // stops the SIB fill (resumes next start)
    sibFill = nullptr;
    delete audioMeter;
    audioMeter = nullptr;
#ifdef KM_HAVE_PROCLOOP
    delete ytAudio;
    ytAudio = nullptr;
    PTHKCore::destroyAiVocalEngine(aiEngine);
    aiEngine = nullptr;
#endif
    BASS_Free();
}

// A song that cannot be played: say why, and offer to take it out of the song list
// (database entry only; the file stays on disk), so it doesn't come up again.
void MainWindow::songUnplayable(const QString &reason)
{
    QMessageBox box(QMessageBox::Warning, tr("ไม่สามารถเล่นเพลงได้"),
                    QString("%1  %2\n\n%3").arg(playingSong.id(), playingSong.name(), reason),
                    QMessageBox::NoButton, dialogParent());
    QPushButton *btnRemove = box.addButton(tr("ลบเพลงนี้ออกจากรายการเพลง"), QMessageBox::DestructiveRole);
    box.addButton(tr("ปิด"), QMessageBox::RejectRole);
    box.setInformativeText(tr("ลบออกจากรายการเพลงเท่านั้น ไฟล์เพลงยังอยู่ในเครื่อง"));
    box.exec();
    if (box.clickedButton() != btnRemove)
        return;

    Song gone(playingSong);
    db->removeSong(gone);
    for (int i = playlist.size() - 1; i >= 0; --i) {     // also out of the queue
        if (playlist[i]->id() == gone.id() && playlist[i]->songType() == gone.songType()
            && playlist[i]->path() == gone.path()) {
            removeFromPlaylist(i);
            if (i < playingIndex)
                playingIndex--;
        }
    }
    ui->detail->setDetail(tr("ลบเพลงแล้ว"), QString("%1  %2").arg(gone.id(), gone.name()));
    ui->detail->show();
    detailTimer->start(3000);
}

void MainWindow::play(int index, int position)
{
    const bool targetIsMidi = (index >= 0 && index < playlist.size() &&
        (playlist[index]->songType() == "NCN" || playlist[index]->songType() == "EMK" ||
         playlist[index]->songType() == "SIB" ||
         playlist[index]->songType() == "HNK" || playlist[index]->songType() == "KAR"));
    if (player->isUseMedley() && player->isPlayerPlaying() && targetIsMidi) {
        loadNextMedley(playlist[index]);
        connect(medleyLoader, &MedleyLoader::finished, [this, index](){
            MidiSequencer *seq = player->midiSequencer();
            seq->setEndTick(seq->midiFile()->tickFromBar(seq->currentBar() + 1));
            playingIndex = index - 1;
        });
        return;
    }

    stop();

    // a new song: no fade left over; it fades in from silence
    fadeTimer->stop();
    fadeDone = nullptr;
    endingSong = false;
    lastLyricPos = -2;
    afterLyrics = false;
    const int effFadeIn = player->isUseMedley() ? 0 : fadeInMs;
    fadeGain = effFadeIn > 0 ? 0.0 : 1.0;
    applyVolume();
    if (effFadeIn > 0)
        startFade(1.0, effFadeIn);
    if (false) fadeGain = fadeInMs > 0 ? 0.0 : 1.0;
    applyVolume();
    if (fadeInMs > 0)
        startFade(1.0, fadeInMs);

    if (index == -1 && playingSong.songType() == "VIDEO")
    {
        playVideo(db->videoPath() + playingSong.path());
        return;
    }
    if (index == -1 && playingSong.songType() == "MP3")
    {
        playVideo(db->mp3Path() + playingSong.path(), false);
        return;
    }
    if (index == -1 && playingSong.songType() == "YOUTUBE")
    {
        playYouTube();
        return;
    }

    if (index == -1 && playingSong.id() != "")
    {
        lyrWidget->reset();
        lyrWidget->show();
        if (secondLyr != nullptr)
        {
            secondLyr->reset();
            secondLyr->show();
        }
        if (position > 0)
        {
            lyrWidget->setSeekPositionCursor(position);
            if (secondLyr != nullptr)
                secondLyr->setSeekPositionCursor(position);
        }

        #ifdef _WIN32
        taskbarButton->progress()->resume();
        taskbarButton->progress()->show();
        #endif

        if (position > 0)
            player->setPositionTick(position);

        player->play();
        positionTimer->start();
        lyricsTimer->start();
        resetScore();
        scoreTimer->start();
        return;
    }

    Song *s = playlist[index];
    playingSong = *s;
    playingIndex = index;

    if (remove_playlist) {
        delete s; // delete playlist in "index"
        playlist.removeAt(index);
        ui->playlistWidget->removeRow(index);
        playingIndex = -1;
    }


    // NCN File
    if (playingSong.songType() == "NCN")
    {
        QString p = db->ncnPath() + playingSong.path();
        if (!player->load(p, true)) {
            songUnplayable(tr("ไม่มีไฟล์ ") + p +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            return;
        }

        QString curPath = db->getCurFilePath(p);
        if (curPath == "" || !QFile::exists(curPath)) {
            songUnplayable(tr("ไม่มีไฟล์ Cursor รหัส ") + playingSong.id() +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            return;
        }

        QString lyrPath = db->getLyrFilePath(p);
        if (lyrPath == "" || !QFile::exists(lyrPath)) {
            songUnplayable(tr("ไม่มีไฟล์ Lyrics รหัส ") + playingSong.id() +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            return;
        }

        lyrWidget->setLyrics(Utils::readLyrics(lyrPath),
            Utils::readCurFile(curPath, player->midiFile()->resorution()));

    }
    else if (playingSong.songType() == "EMK")
    {
        // EMK File (eXtreme Music Karaoke)
        QString p = playingSong.path();
        if (!QFile::exists(p) && !db->hnkPath().isEmpty())
            p = db->hnkPath() + playingSong.path();
        if (!QFile::exists(p) && !db->hnkPath().isEmpty())
            p = db->hnkPath() + "/" + playingSong.path();
        if (!QFile::exists(p) && !db->ncnPath().isEmpty())
            p = QFileInfo(db->ncnPath()).dir().absoluteFilePath("EMK") + playingSong.path();
        if (!QFile::exists(p))
            p = db->ncnPath() + playingSong.path();
        if (!QFile::exists(p))
            p = db->ncnPath() + "/" + playingSong.path();
        if (!QFile::exists(p)) {
            songUnplayable(tr("ไม่มีไฟล์ ") + p);
            return;
        }

        PTHKCore::EmkSong emk = PTHKCore::readEmk(p);
        if (!emk.valid || emk.midData.isEmpty()) {
            songUnplayable(tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            return;
        }

        QFile mid(TEMP_MIDI_DIR_PATH);
        if (mid.exists())
            mid.remove();

        mid.open(QFile::ReadWrite);
        mid.write(emk.midData);
        mid.close();

        if (!player->load(TEMP_MIDI_DIR_PATH, true)) {
            songUnplayable(tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            mid.remove();
            return;
        }

        mid.remove();

        lyrWidget->setLyrics(Utils::readLyrics(emk.lyrData),
            Utils::readCurFile(emk.curData, player->midiFile()->resorution()));
    }
    else if (playingSong.songType() == "SIB")
    {
        QString p = playingSong.path();
        QStringList candidates;
        candidates << p;
        if (!db->sibPath().isEmpty()) {
            candidates << db->sibPath() + "/" + playingSong.path();
            candidates << db->sibPath() + playingSong.path();
            QDir pDir(db->sibPath());
            if (pDir.cdUp()) {
                candidates << pDir.absoluteFilePath(playingSong.path().startsWith('/') ? playingSong.path().mid(1) : playingSong.path());
                candidates << pDir.absolutePath() + playingSong.path();
            }
        }
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI" + playingSong.path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/" + playingSong.path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB" + playingSong.path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB/" + playingSong.path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIBKLDEMO" + playingSong.path();
        const QChar fc = !playingSong.id().isEmpty() ? playingSong.id().at(0).toUpper() : QChar('S');
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIBKLDEMO/" + QString(fc) + "/" + playingSong.id() + ".sibkldemo";
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIBKLDEMO/S/" + playingSong.id() + ".sibkldemo";
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIBKLDEMO/" + playingSong.id() + ".sibkldemo";
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB/" + QString(fc) + "/" + playingSong.id() + ".sib";
        if (!db->sibPath().isEmpty()) {
            QDir sDir(db->sibPath());
            candidates << sDir.absoluteFilePath(playingSong.id() + ".sib");
            candidates << sDir.absoluteFilePath(QString(fc) + "/" + playingSong.id() + ".sib");
            if (sDir.cdUp()) {
                candidates << sDir.absoluteFilePath("SIBKLDEMO/" + QString(fc) + "/" + playingSong.id() + ".sibkldemo");
                candidates << sDir.absoluteFilePath("SIBKLDEMO/" + playingSong.id() + ".sibkldemo");
                candidates << sDir.absoluteFilePath("SIB/" + QString(fc) + "/" + playingSong.id() + ".sib");
                candidates << sDir.absoluteFilePath("SIB/" + playingSong.id() + ".sib");
            }
        }

        bool found = false;
        for (const QString &cand : candidates) {
            if (QFile::exists(cand)) {
                p = cand;
                found = true;
                break;
            }
        }
        if (!found) {
            songUnplayable(tr("ไม่มีไฟล์ ") + p);
            return;
        }

        PTHKCore::SibSong sib = PTHKCore::readSib(p);
        if (!sib.valid || sib.midData.isEmpty()) {
            songUnplayable(tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            return;
        }

        QFile mid(TEMP_MIDI_DIR_PATH);
        if (mid.exists())
            mid.remove();

        mid.open(QFile::ReadWrite);
        mid.write(sib.midData);
        mid.close();

        if (!player->load(TEMP_MIDI_DIR_PATH, true)) {
            songUnplayable(tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            mid.remove();
            return;
        }

        mid.remove();

        lyrWidget->setLyrics(Utils::readLyrics(sib.lyrData),
            Utils::readCurFile(sib.curData, player->midiFile()->resorution()));
        if (secondLyr != nullptr)
            secondLyr->setLyrics(Utils::readLyrics(sib.lyrData),
                Utils::readCurFile(sib.curData, player->midiFile()->resorution()));
    }
    else if (playingSong.songType() == "HNK")
    {
        // HNK File
        QString p = db->hnkPath() + playingSong.path();
        if (!QFile::exists(p)) {
            songUnplayable(tr("ไม่มีไฟล์ ") + p);
            return;
        }

        QFile mid(TEMP_MIDI_DIR_PATH);
        if (mid.exists())
            mid.remove();

        mid.open(QFile::ReadWrite);
        mid.write(HNKFile::midData(p));
        mid.close();

        if (!player->load(TEMP_MIDI_DIR_PATH, true)) {
            songUnplayable(tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            mid.remove();
            return;
        }

        mid.remove();

        lyrWidget->setLyrics(Utils::readLyrics(HNKFile::lyrData(p)),
            Utils::readCurFile(HNKFile::curData(p), player->midiFile()->resorution()));

    }
    else if (playingSong.songType() == "KAR")
    {
        // KAR file
        QString p = db->karPath() + playingSong.path();
        if (!QFile::exists(p)) {
            songUnplayable(tr("ไม่มีไฟล์ ") + p);
            return;
        }

        if (!player->load(p, false)) {
            songUnplayable(tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"));
            return;
        }

        lyrWidget->setLyrics(player->midiFile()->lyrics(), player->midiFile()->lyricsCursor());
    }
    else if (playingSong.songType() == "VIDEO")
    {
        QString p = db->videoPath() + playingSong.path();
        if (!QFile::exists(p)) {
            songUnplayable(tr("ไม่มีไฟล์ ") + p);
            return;
        }
        playVideo(p);
        return;
    }
    else if (playingSong.songType() == "MP3")
    {
        QString p = db->mp3Path() + playingSong.path();
        if (!QFile::exists(p)) {
            songUnplayable(tr("ไม่มีไฟล์ ") + p);
            return;
        }
        playVideo(p, false);
        return;
    }
    else if (playingSong.songType() == "YOUTUBE")
    {
        playYouTube();
        return;
    }
    else
    {
        return;
    }

    if (secondLyr != nullptr)
        secondLyr->setLyrics(lyrWidget->lyrData(), lyrWidget->curData());

    onPlayerDurationTickChanged(player->durationTick());
    onPlayerDurationMSChanged(player->durationMs());

    #ifdef _WIN32
    taskbarButton->progress()->setValue(0);
    taskbarButton->progress()->setMaximum(player->durationTick());
    taskbarButton->progress()->resume();
    taskbarButton->progress()->show();
    #endif

    // RHM
    ui->rhmWidget->setBeat(MidiHelper::calculateBeats(player->midiFile()), player->beatCount());

    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->chMix->hide();
    ui->expandChMix->hide();

    // SongDetail
    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();
    ui->songDetail->show();
    timer2->start(songDetail_timeout);

    player->setBpmSpeed(playingSong.bpmSpeed());
    player->setTranspose(playingSong.transpose());

    if (player->isUseMedley() && player->midiFile() && !player->midiFile()->events().isEmpty()) {
        player->midiFile()->setSingleTempo(true);
        const int baseBpm = player->currentBpm() - player->bpmSpeed();
        const int targetBpm = player->medleyBPM() > 0 ? MidiPlayer::fitLockedBpm(player->medleyBPM(), baseBpm, player->medleyKeepFarSongs()) : player->currentBpm();
        player->setBpmSpeed(targetBpm - baseBpm);
        playingSong.setBpmSpeed(player->bpmSpeed());
        player->midiSequencer()->applyAutoMedleyBounds(lyrWidget->curData(),
                                                       playingSong.cutStartBar(),
                                                       playingSong.cutEndBar(),
                                                       lyrWidget->lastActiveLyricTick(),
                                                       player->medleySkipIntro(),
                                                       player->medleyAutoCutOutro(),
                                                       player->medleyWaitBars(),
                                                       player->medleyQuantizeBar(),
                                                       player->medleyIntroLeadInBars());
    }
    if (player->isUseMedley()) {
        if (false) player->midiSequencer()->setCutStartBar(playingSong.cutStartBar());
        if (false) player->midiSequencer()->setCutEndBar(playingSong.cutEndBar());

        lyrWidget->setSeekPositionCursor(player->positionTick());
        if (secondLyr != nullptr)
            secondLyr->setSeekPositionCursor(player->positionTick());
    }

    player->play();
    db->addHistory(&playingSong);
    resetScore();
    scoreTimer->start();
    dmxLights->setPlaying(true);

    lyrWidget->show();

    if (secondLyr != nullptr)
        secondLyr->show();

    positionTimer->start();
    lyricsTimer->start();

    const int nextMedleyIdx = (remove_playlist ? 0 : playingIndex + 1);
    if (player->isUseMedley() && (nextMedleyIdx >= 0 && nextMedleyIdx < playlist.size())) {
        loadNextMedley(playlist[nextMedleyIdx]);
    }
}

// "Add songs": video / MP3 (+ .lrc) / KAR / MID files go straight into the song database, without
// re-scanning every folder. A file outside the configured folder of its kind is copied into it first,
// so a later full "ปรับปรุง" (update) still finds it.
void MainWindow::showHelpDialog()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("คู่มือการใช้งาน PTHK Karaoke"));
    dlg.resize(760, 640);

    QTextBrowser *text = new QTextBrowser(&dlg);
    text->setOpenExternalLinks(false);
    text->setHtml(tr(
        "<style>h3{color:#3a7bd5;margin:14px 0 4px 0} td{padding:3px 10px} td.k{font-weight:bold;white-space:nowrap}</style>"
        "<h3>เริ่มร้องเพลง</h3>"
        "<table>"
        "<tr><td class=k>พิมพ์รหัส / ชื่อเพลง / ศิลปิน</td><td>ค้นหาเพลง (พิมพ์ผิดแป้นภาษาอังกฤษ ระบบแปลงเป็นไทยให้)</td></tr>"
        "<tr><td class=k>← →</td><td>ดูผลการค้นหาเพลงก่อนหน้า / ถัดไป</td></tr>"
        "<tr><td class=k>Tab</td><td>เปลี่ยนการค้นหา: ทั้งหมด / รหัส / ชื่อเพลง / ศิลปิน</td></tr>"
        "<tr><td class=k>Enter</td><td>เพิ่มเพลงเข้าคิว (ถ้ายังไม่มีเพลงเล่นอยู่ จะเล่นทันที)</td></tr>"
        "<tr><td class=k>↑ ↓</td><td>เปิดคิวเพลง / เลือกเพลงในคิว แล้วกด Enter เพื่อเล่น</td></tr>"
        "<tr><td class=k>Esc</td><td>ปิดช่องค้นหา / คิวเพลง</td></tr>"
        "</table>"

        "<h3>ระหว่างเล่นเพลง</h3>"
        "<table>"
        "<tr><td class=k>Home</td><td>เริ่มเพลงนี้ใหม่ตั้งแต่ต้น (ปุ่ม <i>เริ่มใหม่</i>)</td></tr>"
        "<tr><td class=k>End</td><td>จบเพลงนี้ ไปเพลงถัดไปในคิว (ปุ่ม <i>จบเพลง</i>)</td></tr>"
        "<tr><td class=k>F5 / F6 / F7</td><td>เล่น / หยุดชั่วคราว / หยุด</td></tr>"
        "<tr><td class=k>F8 / F9</td><td>เพลงก่อนหน้า / เพลงถัดไป</td></tr>"
        "<tr><td class=k>*</td><td>ตัด / คืนเสียงร้องนำ (ปุ่ม <i>ตัดเสียงร้อง</i>)</td></tr>"
        "<tr><td class=k>Insert / Delete</td><td>ขึ้นคีย์ / ลงคีย์</td></tr>"
        "<tr><td class=k>Page Up / Page Down</td><td>เร็วขึ้น / ช้าลง</td></tr>"
        "<tr><td class=k>Ctrl + ↑ / ↓</td><td>เพิ่ม / ลดเสียง</td></tr>"
        "<tr><td class=k>Space</td><td>แสดงรายละเอียดเพลงที่เล่นอยู่</td></tr>"
        "<tr><td class=k>F4</td><td>เปิด / ปิด โหมดเมดเลย์ (เล่นต่อเนื่อง)</td></tr>"
        "<tr><td class=k>Ctrl + D / Pause</td><td>ค้างจังหวะกลอง: ดนตรีหยุดที่ต้นห้องถัดไป เหลือกลองตีรอ (เนื้อร้องรอด้วย) กดอีกครั้ง = ลูกส่งแล้วเข้าเพลงต่อพอดีจังหวะ (เพลง MIDI)</td></tr>"
        "<tr><td class=k>PageUp / PageDown</td><td>เพิ่ม / ลดความเร็วเพลงที่เล่นอยู่ (กด Shift ด้วย = ทีละ 5) — ในเมดเลย์ เพลงถัดไปจะใช้ความเร็วใหม่นี้ด้วย</td></tr>"
        "</table>"

        "<h3>คิวเพลง (กด ↑ ↓ เพื่อเปิด)</h3>"
        "<table>"
        "<tr><td class=k>Delete</td><td>เอาเพลงที่เลือกออกจากคิว</td></tr>"
        "<tr><td class=k>+ / -</td><td>เลื่อนเพลงที่เลือกขึ้น (ได้เล่นเร็วขึ้น) / ลง ในคิว</td></tr>"
        "<tr><td class=k>Ctrl + ← → / ↑ ↓</td><td>ตั้งความเร็ว / คีย์ ของเพลงในคิว (หรือในช่องค้นหา) ก่อนเล่น</td></tr>"
        "<tr><td class=k>Ctrl + S / Ctrl + O</td><td>บันทึก / เปิด รายการคิวเพลง</td></tr>"
        "</table>"

        "<h3>ฟีเจอร์อื่น</h3>"
        "<table>"
        "<tr><td class=k>F2</td><td>เพิ่ม / เอาออก จากเพลงโปรด</td></tr>"
        "<tr><td class=k>F3</td><td>เพลงโปรด / เล่นล่าสุด / เพลงฮิต</td></tr>"
        "<tr><td class=k>F10</td><td>ค้นหาเพลงจาก YouTube แล้วเล่นสด หรือเพิ่มเข้าคิว (ต้องต่ออินเทอร์เน็ต)</td></tr>"
        "<tr><td class=k>F12</td><td>บันทึกเสียงร้อง (ดนตรี + ไมค์) เป็นไฟล์ WAV ใน Music\\PTHK Karaoke Recordings</td></tr>"
        "<tr><td class=k>F1</td><td>คู่มือนี้</td></tr>"
        "<tr><td class=k>คลิกขวา</td><td>เมนูทั้งหมด (หน้าจอ, คลังเพลง, เสียงและเอฟเฟกต์, อุปกรณ์เสริม, ตั้งค่า)</td></tr>"
        "</table>"

        "<h3>เพิ่มเพลง</h3>"
        "<p>กดปุ่ม <b>+ เพิ่มเพลง</b> หรือลากไฟล์ / โฟลเดอร์มาวางบนจอ: วิดีโอ (mp4, mkv, avi, mpg, wmv, vob, dat), "
        "MP3 (+ ไฟล์เนื้อร้อง .lrc ชื่อเดียวกัน) และ KAR / MID เข้าฐานข้อมูลทันที<br>"
        "ตั้งชื่อไฟล์แบบ <b>รหัส ชื่อเพลง - ศิลปิน</b> เช่น <i>1001 ใจบางบาง - ศิลปิน.mp4</i> "
        "แล้วค้นด้วยรหัส <b>V1001</b> (วิดีโอ) หรือ <b>M1001</b> (MP3)</p>"

        "<h3>จอที่ 2 (ทีวี / โปรเจกเตอร์)</h3>"
        "<p>กดปุ่ม <b>จอที่ 2</b> เพื่อแสดงเนื้อร้องเต็มจอบนอีกจอ "
        "<b>คลิกขวาบนจอที่ 2</b> เพื่อสลับเต็มจอ / หน้าต่าง, ย้ายไปจออื่น หรือปิด "
        "ปุ่มลัดทั้งหมดใช้ได้ทั้งบนจอหลักและจอที่ 2</p>"));

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    QVBoxLayout *l = new QVBoxLayout(&dlg);
    l->addWidget(text);
    l->addWidget(bb);
    dlg.exec();
    setFocus();
}
