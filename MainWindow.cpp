#include <QMouseEvent>
#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QTime>
#include <QTimer>
#include <QCursor>
#include <QMenu>
#include <QFrame>
#include <QJsonObject>
#include <QJsonArray>
#include <QRadioButton>
#include <QHash>
#include <QSet>
#include <QActionGroup>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QDataStream>
#include <QSaveFile>
#include <QProcess>
#include <QSettings>
#include <QWindow>
#include <QStandardPaths>
#include <QDateTime>
#include <QLocale>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSlider>
#include <QGroupBox>
#include <QGridLayout>
#include <QDialog>
#include <QClipboard>
#include <QListWidget>
#include <QDesktopServices>
#include <QUrl>
#include <QTextCodec>
#include <QProgressDialog>
#include <QTextBrowser>
#include <bassmix.h>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <algorithm>
#include <cmath>

#include "Config.h"
#include "Theme.h"
#include "Utils.h"
#include "MedleyLoader.h"
#include "DrumPadsKey.h"
#include "SettingsDialog.h"
#include "Widgets/MixerAutoHide.h"
#include "Midi/MidiFile.h"
#include "Midi/HNKFile.h"
#include "Midi/AsioOutput.h"
#include "Dialogs/AboutDialog.h"
#include "Dialogs/LockSoundfontDialog.h"
#include "Dialogs/MasteringDialog.h"
#include "PTHKCore.h"
#include "Dialogs/MapSoundfontDialog.h"
#include "Dialogs/MapChannelDialog.h"
#include "Dialogs/BusDialog.h"
#include "Dialogs/SpeakerDialog.h"
#include <QToolTip>
#include <QCursor>
#ifndef __linux__
#include "BASSFX/VSTFX.h"
#include "Vst3/BassVst3.h"
#include <QScreen>
#include <QGuiApplication>
#endif
#include "Dialogs/Equalizer31BandDialog.h"
#include "Dialogs/Chorus2Dialog.h"
#include "Widgets/MenuIcons.h"
#include "Dialogs/Reverb2Dialog.h"
#include "Dialogs/DeleteSongDialog.h"
#include "Dialogs/MedleyDialog.h"
#include "Dialogs/SongListsDialog.h"
#include "Dialogs/YouTube.h"
#include "Midi/YtAudio.h"
#include "Midi/AiVocal.h"
#include "Midi/MicInput.h"
#include "Midi/OutputRecorder.h"
#include "Dmx/DmxLights.h"
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include "Remote/RemoteServer.h"
#include "Remote/RemoteDialog.h"
#include "Remote/SongRequestServer.h"

#ifndef __linux__
#include "Dialogs/VSTDirsDialog.h"
#endif


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
        if (secondLyr != nullptr && videoActive && !mediaIsAudio)
            secondMonitor->setVideoFrame(img);
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
    positionTimer->setInterval(30);

    lyricsTimer = new QTimer(0);
    lyricsTimer->setInterval(30);

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

        int bg = settings->value("BackgroundType", 0).toInt();
        QString bgColor = settings->value("BackgroundColor", "#515151").toString();
        QString bgImg = Config::relocatePath(settings->value("BackgroundImage", "").toString());
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
            if (!player->isPlayerStopped()) {
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

            btnMuteVocal = addBarButton(tr("ตัดเสียงร้อง"), tr("ปิด / เปิด เสียงร้องนำ (*)"));
            btnMuteVocal->setCheckable(true);
            connect(btnMuteVocal, &QPushButton::toggled, this, [this](bool on) {
                if (ui->chMix->isMuteVoice() != on)
                    ui->chMix->setMuteVoice(on);
#ifdef KM_HAVE_PROCLOOP
                applyYtVocalCut();
#endif
                ui->detail->setDetail(tr("เสียงร้อง "), on ? tr("ปิด") : tr("เปิด"));
                ui->detail->show();
                detailTimer->start(3000);
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

#ifdef KM_HAVE_PROCLOOP
    delete ytAudio;
    ytAudio = nullptr;
    PTHKCore::destroyAiVocalEngine(aiEngine);
    aiEngine = nullptr;
#endif
    BASS_Free();
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
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        QString curPath = db->getCurFilePath(p);
        if (curPath == "" || !QFile::exists(curPath)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ Cursor รหัส ") + playingSong.id() +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        QString lyrPath = db->getLyrFilePath(p);
        if (lyrPath == "" || !QFile::exists(lyrPath)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ Lyrics รหัส ") + playingSong.id() +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
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
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p, QMessageBox::Ok);
            return;
        }

        PTHKCore::EmkSong emk = PTHKCore::readEmk(p);
        if (!emk.valid || emk.midData.isEmpty()) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        QFile mid(TEMP_MIDI_DIR_PATH);
        if (mid.exists())
            mid.remove();

        mid.open(QFile::ReadWrite);
        mid.write(emk.midData);
        mid.close();

        if (!player->load(TEMP_MIDI_DIR_PATH, true)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
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
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p, QMessageBox::Ok);
            return;
        }

        PTHKCore::SibSong sib = PTHKCore::readSib(p);
        if (!sib.valid || sib.midData.isEmpty()) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        QFile mid(TEMP_MIDI_DIR_PATH);
        if (mid.exists())
            mid.remove();

        mid.open(QFile::ReadWrite);
        mid.write(sib.midData);
        mid.close();

        if (!player->load(TEMP_MIDI_DIR_PATH, true)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
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
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p, QMessageBox::Ok);
            return;
        }

        QFile mid(TEMP_MIDI_DIR_PATH);
        if (mid.exists())
            mid.remove();

        mid.open(QFile::ReadWrite);
        mid.write(HNKFile::midData(p));
        mid.close();

        if (!player->load(TEMP_MIDI_DIR_PATH, true)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
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
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p, QMessageBox::Ok);
            return;
        }

        if (!player->load(p, false)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        lyrWidget->setLyrics(player->midiFile()->lyrics(), player->midiFile()->lyricsCursor());
    }
    else if (playingSong.songType() == "VIDEO")
    {
        QString p = db->videoPath() + playingSong.path();
        if (!QFile::exists(p)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p, QMessageBox::Ok);
            return;
        }
        playVideo(p);
        return;
    }
    else if (playingSong.songType() == "MP3")
    {
        QString p = db->mp3Path() + playingSong.path();
        if (!QFile::exists(p)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p, QMessageBox::Ok);
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
        const int targetBpm = player->medleyBPM() > 0 ? player->medleyBPM() : player->currentBpm();
        const int baseBpm = player->currentBpm() - player->bpmSpeed();
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

QStringList MainWindow::importSongs(const QStringList &files, bool quiet)
{
    QStringList ids;
    QStringList videoExts = { "mp4", "mkv", "avi", "mpg", "mpeg", "wmv", "vob", "dat" };
    QStringList added, problems;

    QProgressDialog progress(tr("กำลังเพิ่มเพลง..."), tr("ยกเลิก"), 0, files.count(), dialogParent());
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    // NCN songs: a .mid with its .lyr and .cur of the same name (among the chosen files, next to
    // the .mid, or in Lyrics / Cursor folders laid out like an NCN library) is an NCN song. Its three
    // files are copied into the NCN library (Song / Lyrics / Cursor, sub folder = first letter, like
    // the songs already there) and added to the database at once.
    QHash<QString, QString> chosenLyr, chosenCur;       // lower-case name -> file
    for (const QString &f : files) {
        QFileInfo fi(f);
        const QString ext = fi.suffix().toLower();
        if (ext == "lyr")
            chosenLyr.insert(fi.completeBaseName().toLower(), fi.absoluteFilePath());
        else if (ext == "cur")
            chosenCur.insert(fi.completeBaseName().toLower(), fi.absoluteFilePath());
    }
    auto findPart = [](const QFileInfo &mid, const QString &folder, const QString &ext,
                       const QHash<QString, QString> &chosen) -> QString {
        const QString base = mid.completeBaseName();
        if (chosen.contains(base.toLower()))
            return chosen.value(base.toLower());
        QDir d(mid.absolutePath());
        const QString sub = d.dirName();
        QStringList dirs = { d.absolutePath() };
        QDir up(d);
        if (up.cdUp()) {
            dirs << up.absoluteFilePath(folder) << up.absoluteFilePath(folder + "/" + sub);
            QDir up2(up);
            if (up2.cdUp())
                dirs << up2.absoluteFilePath(folder + "/" + sub);
        }
        for (const QString &dir : dirs) {
            const QString p = dir + "/" + base + "." + ext;     // NTFS: any upper / lower case
            if (QFile::exists(p))
                return QFileInfo(p).absoluteFilePath();
        }
        return QString();
    };

    // first find the NCN songs (a folder of songs as received can hold tens of thousands)
    struct NcnSong { QString mid, lyr, cur; };
    QVector<NcnSong> ncnSongs;
    QSet<QString> ncnDone;          // .mid files handled as NCN songs
    progress.setMinimumWidth(460);
    progress.setLabelText(tr("กำลังตรวจไฟล์เพลง..."));
    QApplication::processEvents();
    for (const QString &f : files) {
        QFileInfo mid(f);
        const QString ext = mid.suffix().toLower();
        if (ext != "mid" && ext != "midi")
            continue;
        const QString lyr = findPart(mid, "Lyrics", "lyr", chosenLyr);
        const QString cur = findPart(mid, "Cursor", "cur", chosenCur);
        if (lyr.isEmpty() || cur.isEmpty())
            continue;               // a plain MIDI / KAR song
        ncnDone.insert(f);
        ncnSongs.append({ mid.absoluteFilePath(), lyr, cur });
    }

    const QString ncnRoot = db->ncnPath();
    int ncnSkipped = 0;
    int duplicateSkipped = 0;
    QSet<QString> existingSongKeys = db->songNameArtistKeys();
    if (!ncnSongs.isEmpty()) {
        progress.setMaximum(ncnSongs.size() + files.count());
        const QSet<QString> inDb = db->songPaths("NCN");
        db->beginBatch();
        for (int n = 0; n < ncnSongs.size(); n++) {
            if (progress.wasCanceled())
                break;
            if (n % 20 == 0) {
                progress.setValue(n);
                progress.setLabelText(tr("กำลังเพิ่มเพลง NCN  %1 / %2\nเพิ่มแล้ว %3   มีอยู่แล้ว %4")
                                      .arg(n + 1).arg(ncnSongs.size()).arg(added.size()).arg(ncnSkipped));
                QApplication::processEvents();
            }
            const QFileInfo mid(ncnSongs[n].mid);
            const QString lyr = ncnSongs[n].lyr;
            const QString cur = ncnSongs[n].cur;

            const QString id = mid.completeBaseName();
            if (ncnRoot.isEmpty() || !QDir(ncnRoot + "/Song").exists()) {
                problems << id + tr("  (ยังไม่ได้ตั้งโฟลเดอร์ NCN ในหน้าตั้งค่า)");
                break;
            }

            const QString sub = id.left(1).toUpper();
            const QString dstMid = ncnRoot + "/Song/" + sub + "/" + mid.fileName();
            const QString dstLyr = ncnRoot + "/Lyrics/" + sub + "/" + QFileInfo(lyr).fileName();
            const QString dstCur = ncnRoot + "/Cursor/" + sub + "/" + QFileInfo(cur).fileName();

            QString rel = dstMid;
            rel.replace(ncnRoot, "");
            const bool inLibrary = QFileInfo(dstMid).absoluteFilePath().compare(mid.absoluteFilePath(), Qt::CaseInsensitive) == 0;
            const bool sameFile = QFile::exists(dstMid) && QFileInfo(dstMid).size() == mid.size();
            if ((inLibrary || sameFile) && inDb.contains(rel.toLower())) {
                ncnSkipped++;           // already in the library and in the database
                continue;
            }

            // Check if song name + artist is already in library (skip duplicate songs)
            QString ncnTitle, ncnArtist;
            QFile lyrF(lyr);
            if (lyrF.open(QFile::ReadOnly)) {
                QStringList lines = Utils::decodeThaiText(lyrF.read(4096)).split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
                lyrF.close();
                if (lines.size() > 0) ncnTitle = lines[0].trimmed();
                if (lines.size() > 1) ncnArtist = lines[1].trimmed();
            }
            const QString songKey = SongDatabase::makeSongKey(ncnTitle, ncnArtist);
            if (!songKey.isEmpty() && existingSongKeys.contains(songKey)) {
                duplicateSkipped++;
                continue;
            }
            if (!inLibrary) {
                if (QFile::exists(dstMid) && QFileInfo(dstMid).size() != mid.size()) {
                    problems << id + tr("  (มีเพลงรหัสนี้ในคลังแล้ว แต่เป็นไฟล์อื่น จึงไม่เขียนทับ)");
                    continue;
                }
                bool copied = true;
                const QList<QPair<QString, QString>> parts = { { mid.absoluteFilePath(), dstMid }, { lyr, dstLyr }, { cur, dstCur } };
                for (const auto &pr : parts) {
                    QDir().mkpath(QFileInfo(pr.second).absolutePath());
                    if (!QFile::exists(pr.second) && !QFile::copy(pr.first, pr.second))
                        copied = false;
                }
                if (!copied) {
                    problems << id + tr("  (คัดลอกไม่ได้)");
                    continue;
                }
            }

            db->removeSongFile("NCN", rel);
            if (db->insertNCN(ncnRoot, id, dstMid)) {
                if (!songKey.isEmpty())
                    existingSongKeys.insert(songKey);
                ids << id;
                added << id + "   (NCN)";
            } else {
                problems << id + tr("  (ไฟล์ NCN เสียหาย อ่านไม่ได้)");
            }
        }
        db->endBatch();
    }

    for (int i = 0; i < files.count(); i++) {
        if (i % 200 == 0)
            progress.setValue(ncnSongs.size() + i);
        if (progress.wasCanceled())
            break;

        QFileInfo fi(files[i]);
        QString ext = fi.suffix().toLower();
        if (ncnDone.contains(files[i]) || ext == "lyr" || ext == "cur")
            continue;               // NCN song parts, done above
        if ((ext == "mid" || ext == "midi") && !ncnSongs.isEmpty()) {
            problems << fi.fileName() + tr("  (เพลง NCN ไม่ครบ: ไม่มีไฟล์ .lyr หรือ .cur)");
            continue;
        }
        QString type, root;
        if (videoExts.contains(ext)) {
            type = "VIDEO";
            root = db->videoPath();
        } else if (ext == "mp3") {
            type = "MP3";
            root = db->mp3Path();
        } else if (ext == "emk") {
            type = "EMK";
            root = db->ncnPath();
        } else if (ext == "sib") {
            type = "SIB";
            root = db->sibPath().isEmpty() ? "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB" : db->sibPath();
        } else if (ext == "kar" || ext == "mid" || ext == "midi") {
            type = "KAR";
            root = db->karPath();
        } else {
            if (ext != "lrc")       // a .lrc comes along with its MP3
                problems << fi.fileName() + tr("  (ไม่รองรับชนิดไฟล์นี้)");
            continue;
        }

        QString songTitle, songArtist;
        if (type == "VIDEO" || type == "MP3") {
            QString base = fi.completeBaseName();
            QRegExp numPrefix("^(\\d+)[\\s_\\-.]+(.*)$");
            if (numPrefix.indexIn(base) == 0 && !numPrefix.cap(2).trimmed().isEmpty()) {
                base = numPrefix.cap(2);
            }
            songTitle = base.trimmed();
            int sep = base.indexOf(" - ");
            if (sep > 0) {
                songTitle = base.left(sep).trimmed();
                songArtist = base.mid(sep + 3).trimmed();
            }
        } else if (type == "EMK") {
            PTHKCore::EmkSong s = PTHKCore::readEmk(fi.absoluteFilePath());
            songTitle = s.title.trimmed();
            songArtist = s.artist.trimmed();
        } else if (type == "SIB") {
            PTHKCore::SibSong s = PTHKCore::readSib(fi.absoluteFilePath());
            songTitle = s.title.trimmed();
            songArtist = s.artist.trimmed();
        } else if (type == "KAR") {
            songTitle = fi.completeBaseName().trimmed();
        }

        const QString mediaKey = SongDatabase::makeSongKey(songTitle, songArtist);
        if (!mediaKey.isEmpty() && existingSongKeys.contains(mediaKey)) {
            duplicateSkipped++;
            continue;
        }

        root = QDir::fromNativeSeparators(root);
        while (root.endsWith('/'))
            root.chop(1);
        if (!QDir().mkpath(root)) {
            problems << fi.fileName() + tr("  (สร้างโฟลเดอร์ไม่ได้: ") + root + ")";
            continue;
        }

        QString src = QDir::fromNativeSeparators(fi.absoluteFilePath());
        QString dst = src;
        if (!src.startsWith(root + "/", Qt::CaseInsensitive)) {
            dst = root + "/" + fi.fileName();
            progress.setLabelText(tr("กำลังคัดลอก ") + fi.fileName());
            if (!QFile::exists(dst) && !QFile::copy(src, dst)) {
                problems << fi.fileName() + tr("  (คัดลอกไม่ได้)");
                continue;
            }
            if (type == "MP3") {
                QString lrc = fi.absolutePath() + "/" + fi.completeBaseName() + ".lrc";
                QString lrcDst = root + "/" + fi.completeBaseName() + ".lrc";
                if (QFile::exists(lrc) && !QFile::exists(lrcDst))
                    QFile::copy(lrc, lrcDst);
            }
        }

        QString rel = dst.mid(root.length());
        db->removeSongFile(type, rel);
        bool ok = (type == "EMK") ? db->insertEMK(root, fi.completeBaseName(), dst)
                                  : (type == "SIB") ? db->insertSIB(root, fi.completeBaseName(), dst)
                                  : (type == "KAR") ? db->insertKAR(root, "-------", dst, QFileInfo(dst).fileName())
                                  : db->insertVideo(root, dst, QFileInfo(dst).fileName(), type, type == "MP3" ? "M" : "V");
        if (ok) {
            if (!mediaKey.isEmpty())
                existingSongKeys.insert(mediaKey);
            ids << db->songIdFor(type, rel);
            added << ids.last() + "   " + QFileInfo(dst).completeBaseName();
        } else
            problems << fi.fileName() + tr("  (อ่านไฟล์ไม่ได้)");
    }
    progress.setValue(progress.maximum());

    QString msg;
    if (!added.isEmpty())
        msg += tr("เพิ่มแล้ว %1 เพลง (พิมพ์รหัสหรือชื่อเพลงเพื่อเล่น):\n").arg(added.count()) + added.mid(0, 15).join("\n");
    if (added.count() > 15)
        msg += "\n...";
    if (ncnSkipped > 0)
        msg += (msg.isEmpty() ? "" : "\n\n") + tr("มีอยู่ในคลังแล้ว %1 เพลง (ข้าม)").arg(ncnSkipped);
    if (duplicateSkipped > 0)
        msg += (msg.isEmpty() ? "" : "\n\n") + tr("ชื่อเพลงและศิลปินซ้ำกับในคลัง %1 เพลง (ข้าม)").arg(duplicateSkipped);
    if (!problems.isEmpty())
        msg += (msg.isEmpty() ? "" : "\n\n") + tr("เพิ่มไม่ได้ %1 เพลง:\n").arg(problems.count()) + problems.mid(0, 10).join("\n");
    if (problems.count() > 10)
        msg += "\n...";
    if (msg.isEmpty())
        msg = tr("ไม่พบไฟล์เพลงที่เพิ่มได้ในที่ที่เลือก");
    if (!added.isEmpty())
        refreshArtistCodes();
    if (!msg.isEmpty() && (!quiet || !problems.isEmpty()))
        QMessageBox::information(dialogParent(), tr("เพิ่มเพลง"), msg);
    setFocus();
    return ids;
}

void MainWindow::showYouTubeDownload(const QString &url)
{
    QString dir = QDir::fromNativeSeparators(db->videoPath());
    while (dir.endsWith('/'))
        dir.chop(1);
    YouTubeDownloadDialog *dlg = new YouTubeDownloadDialog(dir + "/YouTube", url, dialogParent());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &YouTubeDownloadDialog::downloaded, this, [this](const QString &file, bool enqueue) {
        const QStringList ids = importSongs({ file }, enqueue);
        if (!enqueue || ids.isEmpty())
            return;
        Song *s = db->search(ids.first());
        if (!s || s->id() != ids.first())
            return;
        addToPlaylist(s);
        hideUIFrame();
        if (auto_playnext && !isSongPlaying())
            play(playlist.count() - 1);
        else {
            showPlaylist();
            timer2->start(playlist_timeout);    // the queue shows briefly, then hides again
        }
    });
    dlg->show();
}

// new NCN / HNK files only (the full update reads all ~90,000 songs again)
void MainWindow::updateNewSongs()
{
    if (db->isRunning())
        return;
    db->setUpdateType(UpdateType::UpdateNewSongs);
    auto *conn = new QMetaObject::Connection;
    *conn = connect(db, &QThread::finished, this, [this, conn]() {
        disconnect(*conn);
        delete conn;
        if (db->lastAdded() > 0 || db->lastRemoved() > 0)
            refreshArtistCodes();
        QString infoText;
        if (db->lastAdded() == 0 && db->lastRemoved() == 0 && db->lastSkippedDuplicate() == 0) {
            infoText = tr("ไม่มีเพลงใหม่ ฐานข้อมูลตรงกับโฟลเดอร์ NCN แล้ว");
        } else {
            infoText = tr("เพิ่มเพลงใหม่ %1 เพลง\n").arg(db->lastAdded());
            if (db->lastSkippedDuplicate() > 0)
                infoText += tr("ชื่อเพลงและศิลปินซ้ำ (ข้าม) %1 เพลง\n").arg(db->lastSkippedDuplicate());
            infoText += tr("ลบเพลงที่ไม่มีไฟล์แล้ว %1 เพลง").arg(db->lastRemoved());
        }
        QMessageBox::information(dialogParent(), tr("เพิ่มเพลง NCN ใหม่"), infoText);
    });
    db->start();
}

// ---- song transitions ------------------------------------------------------------------------

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
    videoPlayer->setVolume(v);
#ifdef PTHK_WEBENGINE
    if (ytPlayer)
        ytPlayer->setVolume(ytAudioLive() ? 100 : v);
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
    if (ai)
        aiEngine->selectModel(settings->value("YtAiModel", "light").toString());
    ytAudio->setLevels(settings->value("YtVocalLevel", 0).toInt() / 100.0f,
                       settings->value("YtMusicLevel", 100).toInt() / 100.0f);
    if (!ai)
        ytAiLatched = false;
    else if (cut)
        ytAiLatched = true;
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
            if (nextBarTick <= seq->positionTick() + seq->midiFile()->resorution() / 2)
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
    if (!endAfterLyrics || endingSong)
        return;
    if (lastLyricPos == -2) {
        const QVector<long> c = lyrWidget->curData();
        long last = -1;
        for (long v : c)
            last = qMax(last, v);
        lastLyricPos = lyrWidget->lastActiveLyricTick();
    }
    if (lastLyricPos < 0)
        return;
    if (pos < lastLyricPos) {           // still singing (or sought back)
        afterLyrics = false;
        return;
    }
    if (!afterLyrics) {
        afterLyrics = true;
        if (player->isUseMedley()) {
            if (player->isPlayerPlaying() && (player->midiSequencerTemp() != nullptr || playlist.size() > (playingIndex + 1))) {
                MidiSequencer *seq = player->midiSequencer();
                const int waitBars = player->medleyWaitBars();
                int targetBar = seq->currentBar() + waitBars + 1;
                int nextBarTick = seq->midiFile()->tickFromBar(targetBar);
                if (nextBarTick <= seq->positionTick() + seq->midiFile()->resorution() / 2)
                    nextBarTick = seq->midiFile()->tickFromBar(targetBar + 1);
                if (seq->endTick() == 0 || seq->endTick() > nextBarTick)
                    seq->setEndTick(nextBarTick);
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

void MainWindow::ensureRequestServer()
{
    if (reqServer)
        return;
    reqServer = new SongRequestServer(this);
    reqServer->setAutoApprove(settings->value("RequestAutoApprove", false).toBool());

    reqServer->searchLibrary = [this](const QString &text) {
        QJsonArray items;
        for (Song s : db->findSongs(text, 30))
            items.append(QJsonObject{ { "key", "L:" + s.songType() + "|" + s.path() },
                                      { "title", s.name() },
                                      { "artist", s.artist() + "  (" + s.id() + ")" } });
        return items;
    };
    reqServer->queueState = [this]() {
        QJsonObject st;
        if (isSongPlaying())
            st["now"] = QJsonObject{ { "title", playingSong.name() }, { "artist", playingSong.artist() } };
        QJsonArray q;
        for (int i = playingIndex + 1; i < playlist.count() && q.size() < 15; i++)
            q.append(QJsonObject{ { "title", playlist[i]->name() }, { "artist", playlist[i]->artist() } });
        st["queue"] = q;
        return st;
    };
    reqServer->addToQueue = [this](const SongRequestServer::Request &r) {
        if (r.key.startsWith("Y:")) {
            YtItem it;
            it.id = r.key.mid(2);
            it.title = r.title;
            it.channel = r.artist;
            it.seconds = r.seconds;
            addYouTubeToQueue(it, false);
            return true;
        }
        const QString rest = r.key.mid(2);
        const int bar = rest.indexOf('|');
        Song s;
        if (bar < 0 || !db->songByPath(rest.left(bar), rest.mid(bar + 1), &s))
            return false;
        addToPlaylist(&s);
        if (auto_playnext && !isSongPlaying())
            play(playlist.count() - 1);
        else {
            showPlaylist();
            timer2->start(playlist_timeout);
        }
        return true;
    };
    connect(reqServer, &SongRequestServer::requestsChanged, this, &MainWindow::updateRequestToast);

    reqToast = new QFrame(this);
    reqToast->setStyleSheet("QFrame { background: rgba(20, 22, 28, 230); border: 1px solid #3a7bd5; border-radius: 10px; }"
                            "QLabel { color: white; background: transparent; border: none; }"
                            "QPushButton { color: white; border: none; border-radius: 6px; padding: 4px 10px; }");
    new QVBoxLayout(reqToast);
    reqToast->hide();
}

// what waits for approval, top right of the player; buttons never take the keyboard focus
void MainWindow::updateRequestToast()
{
    if (!reqToast)
        return;
    QLayout *lay = reqToast->layout();
    while (QLayoutItem *li = lay->takeAt(0)) {
        if (li->widget())
            li->widget()->deleteLater();
        if (li->layout()) {
            while (QLayoutItem *c = li->layout()->takeAt(0)) {
                if (c->widget()) c->widget()->deleteLater();
                delete c;
            }
        }
        delete li;
    }
    const QList<SongRequestServer::Request> wait = reqServer->pending();
    if (wait.isEmpty()) {
        reqToast->hide();
        return;
    }
    QLabel *head = new QLabel(tr("คำขอเพลงจากมือถือ (%1)").arg(wait.size()), reqToast);
    QFont hf = head->font();
    hf.setBold(true);
    head->setFont(hf);
    lay->addWidget(head);
    for (int i = 0; i < wait.size() && i < 5; i++) {
        const SongRequestServer::Request r = wait[i];
        QHBoxLayout *row = new QHBoxLayout();
        QLabel *t = new QLabel(r.title.left(48) + "\n" + (r.who.isEmpty() ? tr("ไม่ระบุชื่อ") : r.who), reqToast);
        QPushButton *ok = new QPushButton(tr("อนุมัติ"), reqToast);
        QPushButton *no = new QPushButton(tr("ไม่"), reqToast);
        ok->setStyleSheet("QPushButton { background: #2e9e5b; color: white; font-weight: bold; border-radius: 5px; padding: 5px 14px; min-width: 68px; }");
        no->setStyleSheet("QPushButton { background: #c94a4a; color: white; font-weight: bold; border-radius: 5px; padding: 5px 14px; min-width: 54px; }");
        for (QPushButton *b : { ok, no })
            b->setFocusPolicy(Qt::NoFocus);
        const int id = r.id;
        connect(ok, &QPushButton::clicked, this, [this, id]() { reqServer->decide(id, true); });
        connect(no, &QPushButton::clicked, this, [this, id]() { reqServer->decide(id, false); });
        row->addWidget(t, 1);
        row->addWidget(ok);
        row->addWidget(no);
        static_cast<QVBoxLayout *>(lay)->addLayout(row);
    }
    reqToast->setFixedWidth(360);
    reqToast->adjustSize();
    reqToast->move(width() - reqToast->width() - 16, 16);
    reqToast->raise();
    reqToast->show();
}

void MainWindow::showSongRequestDialog()
{
    ensureRequestServer();
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("ขอเพลงจากมือถือ"));

    QComboBox *ip = new QComboBox(&dlg);
    ip->addItems(RemoteServer::localAddresses());
    QPushButton *onOff = new QPushButton(&dlg);
    QrWidget *qrC = new QrWidget(&dlg), *qrO = new QrWidget(&dlg);
    QLabel *urlC = new QLabel(&dlg), *urlO = new QLabel(&dlg);
    for (QLabel *l : { urlC, urlO }) {
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setAlignment(Qt::AlignCenter);
        l->setWordWrap(true);
    }
    QCheckBox *autoOk = new QCheckBox(tr("อนุมัติอัตโนมัติ (คำขอเข้าคิวทันที ไม่ต้องรอเจ้าของ)"), &dlg);
    autoOk->setChecked(reqServer->autoApprove());
    QLabel *hint = new QLabel(tr("ลูกค้า: สแกน QR ซ้าย ค้นหาเพลงแล้วกด \"ขอเพลง\"  •  เจ้าของ: สแกน QR ขวา อนุมัติได้จากมือถือ\n"
                                 "คำขอจะขึ้นมุมขวาบนของโปรแกรมด้วย  •  มือถือต้องใช้ Wi-Fi เดียวกับคอมพิวเตอร์"), &dlg);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");

    auto refresh = [=]() {
        const bool on = reqServer->isRunning();
        qrC->setText(on ? reqServer->customerUrl() : QString());
        qrO->setText(on ? reqServer->ownerUrl() : QString());
        urlC->setText(on ? reqServer->customerUrl() : QString());
        urlO->setText(on ? reqServer->ownerUrl() : QString());
        onOff->setText(on ? tr("ปิดการขอเพลง") : tr("เปิดการขอเพลงจากมือถือ"));
    };
    connect(onOff, &QPushButton::clicked, &dlg, [=]() {
        if (reqServer->isRunning())
            reqServer->stop();
        else if (ip->currentText().isEmpty() || !reqServer->start(ip->currentText()))
            QMessageBox::warning(dialogParent(), tr("ขอเพลงจากมือถือ"), tr("เปิดไม่สำเร็จ (ไม่พบเครือข่าย Wi-Fi/LAN หรือพอร์ตไม่ว่าง)"));
        refresh();
    });
    connect(ip, QOverload<int>::of(&QComboBox::activated), &dlg, [=](int) {
        if (reqServer->isRunning()) {
            reqServer->start(ip->currentText());
            refresh();
        }
    });
    connect(autoOk, &QCheckBox::toggled, &dlg, [this](bool on) {
        settings->setValue("RequestAutoApprove", on);
        reqServer->setAutoApprove(on);
    });

    QGridLayout *g = new QGridLayout();
    QLabel *lc = new QLabel(tr("QR ลูกค้า (ขอเพลง)"), &dlg), *lo = new QLabel(tr("QR เจ้าของ (อนุมัติ)"), &dlg);
    for (QLabel *l : { lc, lo }) {
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        l->setAlignment(Qt::AlignCenter);
    }
    g->addWidget(lc, 0, 0);
    g->addWidget(lo, 0, 1);
    g->addWidget(qrC, 1, 0, Qt::AlignCenter);
    g->addWidget(qrO, 1, 1, Qt::AlignCenter);
    g->addWidget(urlC, 2, 0);
    g->addWidget(urlO, 2, 1);

    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("เครือข่าย (IP):"), &dlg));
    top->addWidget(ip, 1);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addLayout(top);
    v->addWidget(onOff);
    v->addLayout(g);
    v->addWidget(autoOk);
    v->addWidget(hint);
    v->addWidget(bb);
    refresh();
    dlg.exec();
    setFocus();
}

// ---- codes by artist ------------------------------------------------------------------------------

// after songs were added: they get codes in their artist's run (only once codes are in use)
void MainWindow::refreshArtistCodes()
{
    const SongDatabase::CodeStats st = db->artistCodeStats();
    if (st.songs == 0)
        return;
    db->assignArtistCodes(st.first, settings->value("ArtistCodeReserve", 10).toInt(), false);
}

void MainWindow::showArtistCodeDialog()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("จัดรหัสเพลงตามศิลปิน"));
    dlg.setMinimumWidth(520);

    QLabel *intro = new QLabel(tr("ให้รหัส 7 หลักกับทุกเพลง เรียงตามศิลปิน (เพลงของศิลปินเดียวกันได้เลขติดกัน เรียงตามชื่อเพลง) "
                                  "และเว้นเลขท้ายของแต่ละศิลปินไว้ให้เพลงใหม่ของศิลปินคนนั้น\n"
                                  "รหัสเดิมของเพลงยังใช้ได้ตามปกติ ไฟล์เพลงไม่ถูกแก้ไข (ใช้ร่วมกับโปรแกรมอื่นได้)\n"
                                  "จัดซ้ำได้ทุกเมื่อ รหัสที่ให้ไปแล้วจะไม่เปลี่ยน เพลงที่เพิ่มใหม่จะได้รหัสในช่วงของศิลปินของตัวเอง"), &dlg);
    intro->setWordWrap(true);

    QSpinBox *start = new QSpinBox(&dlg);
    start->setRange(1000000, 9000000);
    start->setSingleStep(100000);
    QSpinBox *reserve = new QSpinBox(&dlg);
    reserve->setRange(0, 200);
    reserve->setValue(settings->value("ArtistCodeReserve", 10).toInt());
    reserve->setSuffix(tr(" เลข"));
    QLabel *status = new QLabel(&dlg);
    status->setWordWrap(true);

    auto refresh = [=]() {
        const SongDatabase::CodeStats st = db->artistCodeStats();
        start->setValue(st.songs > 0 ? int(st.first) : 8000000);
        start->setEnabled(st.songs == 0);
        status->setText(st.songs == 0 ? tr("ยังไม่ได้จัดรหัส")
                                      : tr("จัดรหัสแล้ว %1 เพลง %2 ศิลปิน  (รหัส %3 - %4)\nพิมพ์รหัส 7 หลักแล้วกด Enter เพื่อเล่นได้เลย")
                                            .arg(st.songs).arg(st.artists).arg(st.first).arg(st.last));
    };

    auto run = [=](bool fresh) {
        settings->setValue("ArtistCodeReserve", reserve->value());
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const SongDatabase::CodeStats st = db->assignArtistCodes(start->value(), reserve->value(), fresh);
        QApplication::restoreOverrideCursor();
        refresh();
        QMessageBox::information(dialogParent(), tr("จัดรหัสเพลงตามศิลปิน"),
                                 tr("ให้รหัสใหม่ %1 เพลง\nเพลงที่ไม่มีไฟล์แล้ว (คืนรหัส) %2 เพลง\nทั้งหมด %3 เพลง %4 ศิลปิน")
                                     .arg(st.added).arg(st.removed).arg(st.songs).arg(st.artists));
    };

    QPushButton *bAssign = new QPushButton(tr("จัดรหัส / ให้รหัสเพลงใหม่"), &dlg);
    QPushButton *bFresh = new QPushButton(tr("ล้างแล้วจัดใหม่ทั้งหมด..."), &dlg);
    QPushButton *bExport = new QPushButton(tr("ส่งออกสมุดเพลง (Excel / CSV)..."), &dlg);
    connect(bAssign, &QPushButton::clicked, &dlg, [=]() { run(false); });
    connect(bFresh, &QPushButton::clicked, &dlg, [=]() {
        if (QMessageBox::question(dialogParent(), tr("จัดรหัสเพลงตามศิลปิน"),
                                  tr("รหัสตามศิลปินทั้งหมดจะเปลี่ยนใหม่ สมุดเพลงที่พิมพ์ไว้จะใช้ไม่ได้\nต้องการจัดใหม่ทั้งหมดหรือไม่?"))
                == QMessageBox::Yes)
            run(true);
    });
    connect(bExport, &QPushButton::clicked, &dlg, [=]() {
        const QString file = QFileDialog::getSaveFileName(dialogParent(), tr("ส่งออกสมุดเพลง"),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + tr("/สมุดเพลง PTHK Karaoke.csv"),
                tr("ไฟล์ CSV (เปิดด้วย Excel) (*.csv)"));
        if (file.isEmpty())
            return;
        if (db->exportSongBook(file))
            QMessageBox::information(dialogParent(), tr("ส่งออกสมุดเพลง"), tr("บันทึกแล้ว:\n") + file);
        else
            QMessageBox::warning(dialogParent(), tr("ส่งออกสมุดเพลง"), tr("บันทึกไม่สำเร็จ"));
    });

    QFormLayout *form = new QFormLayout();
    form->addRow(tr("รหัสเริ่มต้น:"), start);
    form->addRow(tr("เว้นเลขให้เพลงใหม่ ต่อศิลปิน:"), reserve);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addWidget(bAssign);
    buttons->addWidget(bFresh);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addWidget(intro);
    v->addLayout(form);
    v->addWidget(status);
    v->addLayout(buttons);
    v->addWidget(bExport);
    v->addWidget(bb);
    refresh();
    dlg.exec();
    setFocus();
}

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
    connect(&dlg, &YouTubeSearchDialog::playNow, this, [this](const YtItem &it) { addYouTubeToQueue(it, true); });
    connect(&dlg, &YouTubeSearchDialog::addToQueue, this, [this](const YtItem &it) { addYouTubeToQueue(it, false); });
    connect(&dlg, &YouTubeSearchDialog::downloadRequested, this, [this](const QString &url) { showYouTubeDownload(url); });
    dlg.exec();
    setFocus();
}

void MainWindow::addYouTubeToQueue(const YtItem &item, bool playNow)
{
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
        ytMirrorTimer->setInterval(33);
        connect(ytMirrorTimer, &QTimer::timeout, this, [this]() {
            if (!ytActive || !ytPlayer->isVisible())
                return;
            // copy only while the new clip plays: before that the view can still show the last
            // picture of the previous song, which (delayed for the AI) would flash up before the new one
            QImage img;
            if (ytPlayer->isStarted())
                img = ytPlayer->grab().toImage();
#ifdef KM_HAVE_PROCLOOP
            // AI vocal cut: the sound is D seconds late, so the picture is shown D seconds late too
            const double d = (ytAudio && settings->value("YtDelayPicture", true).toBool()) ? ytAudio->delaySeconds() : 0.0;
            if (d > 0.15) {
                if (!frameClock.isValid())
                    frameClock.start();
                const qint64 now = frameClock.elapsed();
                // kept small (854 x 480, 3 bytes a pixel): about 240 MB for 6.5 s at 30 frames a second
                if (!img.isNull())
                    delayedFrames.push_back({ now, img.scaled(854, 480, Qt::KeepAspectRatio, Qt::FastTransformation)
                                                       .convertToFormat(QImage::Format_RGB888) });
                const qint64 target = now - qint64(d * 1000);
                QImage show;
                while (!delayedFrames.empty() && delayedFrames.front().t <= target) {
                    show = delayedFrames.front().img;
                    delayedFrames.pop_front();
                }
                while (delayedFrames.size() > 400)          // never more than ~13 s of picture
                    delayedFrames.pop_front();
                img = show;                                 // null: the delayed picture is not due yet
            } else if (!delayedFrames.empty()) {
                delayedFrames.clear();
            }
#endif
            if (img.isNull())
                return;
            videoWidget->setFrame(img);
            if (secondLyr != nullptr)
                secondMonitor->setVideoFrame(img);
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
    lyrWidget->hide();
    if (secondLyr != nullptr)
        secondLyr->hide();
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
    ytPlayer->resize(ui->centralWidget->size());
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
        if (ytAudio->aiMode())
            aiEngine->reset();
    }
    delayedFrames.clear();
#endif
    ytPlayer->playVideo(playingSong.path(), ytAudioLive() ? 100 : effectiveVolume());
    dmxLights->setPlaying(true);
#else
    QMessageBox::warning(dialogParent(), tr("YouTube"), tr("เวอร์ชันนี้เล่น YouTube ออนไลน์ไม่ได้"));
#endif
}

// "+ เพิ่มเพลง": a whole folder of new songs (e.g. a set of NCN songs as received) or single files
void MainWindow::showImportDialog()
{
    QMenu m(this);
    QAction *aFolder = m.addAction(tr("เลือกทั้งโฟลเดอร์ (เช่น โฟลเดอร์เพลง NCN ที่ได้มา)..."));
    QAction *aFiles = m.addAction(tr("เลือกไฟล์เพลง (NCN / วิดีโอ / MP3 / KAR)..."));
    QAction *pick = m.exec(QCursor::pos());
    if (pick == nullptr)
        return;

    QStringList files;
    if (pick == aFolder) {
        const QString dir = QFileDialog::getExistingDirectory(dialogParent(), tr("เลือกโฟลเดอร์ที่มีเพลงใหม่"),
                                                              Utils::LAST_OPEN_DIR);
        if (dir.isEmpty())
            return;
        Utils::LAST_OPEN_DIR = dir;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            files << it.next();
            if (files.size() % 2000 == 0)
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        QApplication::restoreOverrideCursor();
    } else if (pick == aFiles) {
        files = QFileDialog::getOpenFileNames(
                    dialogParent(), tr("เลือกไฟล์เพลงที่จะเพิ่ม (เพลง NCN เลือก .mid .lyr .cur มาด้วยกัน)"), Utils::LAST_OPEN_DIR,
                    tr("เพลงคาราโอเกะ (*.mid *.midi *.lyr *.cur *.mp4 *.mkv *.avi *.mpg *.mpeg *.wmv *.vob *.dat *.mp3 *.kar *.emk *.sib);;ทุกไฟล์ (*)"));
        if (files.isEmpty())
            return;
        Utils::LAST_OPEN_DIR = QFileInfo(files.first()).absolutePath();
    }
    if (!files.isEmpty())
        importSongs(files);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList files;
    for (const QUrl &u : event->mimeData()->urls()) {
        QFileInfo fi(u.toLocalFile());
        if (fi.isDir()) {           // a dropped folder: every file in it
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                files << it.next();
        } else if (fi.isFile()) {
            files << fi.absoluteFilePath();
        }
    }
    event->acceptProposedAction();
    if (!files.isEmpty())
        importSongs(files);
}

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

    // MP3 goes through the synth's main mixer (master EQ / effects / volume like the MIDI songs);
    // Qt's player is only the fallback when the synth is not running
    if (!mediaIsAudio || !startMp3Stream(path)) {
        videoPlayer->setVolume(effectiveVolume());
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
        ytPlayer->resize(ui->centralWidget->size());
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

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (ui->songMedley->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Escape: {
            ui->songMedley->hide();
            timer2->start(playlist_timeout);
            break;
        }
        case Qt::Key_Enter:
        case Qt::Key_Return: {
            Song *s = playlist[ui->playlistWidget->currentRow()];
            s->setCutStartBar(ui->songMedley->cutStartBar());
            s->setCutEndBar(ui->songMedley->cutEndBar());
            this->setFocus();
            ui->songMedley->hide();
            timer2->start(playlist_timeout);

            if (ui->playlistWidget->currentRow() == playingIndex + 1) {
                MidiSequencer *seq = player->midiSequencerTemp();
                seq->setCutStartBar(s->cutStartBar());
                seq->setCutEndBar(s->cutEndBar());
            }
            break;
        }
        default:
            break;
        }
        return;
    }

    if (event->modifiers() == Qt::ControlModifier) {
        switch (event->key()) {
            case Qt::Key_Up:
                if (ui->frameSearch->isVisible())
                {
                    preSetTranspose(db->currentSong()->transpose() + 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setTranspose(s->transpose() + 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                else
                {
                    ui->sliderVolume->setValue(ui->sliderVolume->value() + 5);
                }
                break;
            case Qt::Key_Down:
                if (ui->frameSearch->isVisible())
                {
                    preSetTranspose(db->currentSong()->transpose() - 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setTranspose(s->transpose() - 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                else
                {
                    ui->sliderVolume->setValue(ui->sliderVolume->value() - 5);
                }
                break;
            case Qt::Key_Right:
                if (ui->frameSearch->isVisible())
                {
                    preSetBpmSpeed(db->currentSong()->bpmSpeed() + 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setBpmSpeed(s->bpmSpeed() + 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_Left:
                if (ui->frameSearch->isVisible())
                {
                    preSetBpmSpeed(db->currentSong()->bpmSpeed() - 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setBpmSpeed(s->bpmSpeed() - 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_X:
                if (ui->frameSearch->isVisible()) {
                    setFrameSearch( db->search("") );
                    ui->lbSearch->setText("_");
                    ui->frameSearch->show();
                    timer2->start(search_timeout);
                }
                break;
            case Qt::Key_Equal:     // same as + / - without Ctrl: + up, - down
            case Qt::Key_Plus:
                if (ui->playlistWidget->isVisible()) {
                    int i = ui->playlistWidget->currentRow();
                    if (i > 0)
                        swapInPlaylist(i, i-1);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_Minus:
                if (ui->playlistWidget->isVisible()) {
                    int i = ui->playlistWidget->currentRow();
                    if (i >= 0 && i < ui->playlistWidget->rowCount() - 1)
                        swapInPlaylist(i, i+1);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_S:
                if (ui->playlistWidget->isVisible())
                {
                    timer2->stop();
                    savePlaylist();
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_O:
                if (ui->playlistWidget->isVisible())
                {
                    timer2->stop();
                    loadPlaylist();
                    timer2->start(playlist_timeout);
                }
                else
                    this->sendDrumPads(event, true);
                break;
        case Qt::Key_Delete:
            if (ui->frameSearch->isVisible()) {
                DeleteSongDialog dlg(this);
                dlg.setModal(true);
                dlg.adjustSize();
                dlg.setFixedSize(dlg.size());
                dlg.exec();

                if (dlg.removeConfirmed()) {
                    db->removeCurrentSong(dlg.removeFromStorage());
                    setFrameSearch( db->searchNext() );
                    if (ui->frameSearch->isVisible()) {
                        timer2->start(search_timeout);
                    }
                }
            }
            break;
            default:
                this->sendDrumPads(event, true);
                break;
        }
        return;
    }


    switch (event->key()) {
    case Qt::Key_Escape: {
        hideUIFrame();
        break;
    }
    case Qt::Key_F2: {      // favorite on/off: the song in the search box, else the playing song
        Song *s = ui->frameSearch->isVisible() ? db->currentSong() : &playingSong;
        if (s->path().isEmpty())
            break;
        bool fav = db->toggleFavorite(s);
        ui->detail->setDetail(tr("เพลงโปรด "), fav ? tr("เพิ่มแล้ว") : tr("เอาออกแล้ว"));
        ui->detail->show();
        detailTimer->start(3000);
        break;
    }
    case Qt::Key_F1: {
        showHelpDialog();
        break;
    }
    case Qt::Key_F10: {     // YouTube search: play online or add to the queue
        showYouTubeSearch();
        break;
    }
    case Qt::Key_F12: {     // record music + microphone to a WAV file (stops by itself at the end of the song)
        toggleRecording();
        break;
    }
    case Qt::Key_F3: {      // favorites / recently played / most played
        SongListsDialog dlg(this, db);
        if (dlg.exec() == QDialog::Accepted && dlg.hasSelection()) {
            Song s = dlg.selectedSong();
            addToPlaylist(&s);
            if (auto_playnext && playlist.count() == 1 && !isSongPlaying())
                play(0);
        }
        setFocus();
        break;
    }
    case Qt::Key_F4: {
        if (player->isUseMedley() && player->midiFile() && !player->midiFile()->events().isEmpty()) {
        player->midiFile()->setSingleTempo(true);
        const int targetBpm = player->medleyBPM() > 0 ? player->medleyBPM() : player->currentBpm();
        const int baseBpm = player->currentBpm() - player->bpmSpeed();
        player->setBpmSpeed(targetBpm - baseBpm);
        playingSong.setBpmSpeed(player->bpmSpeed());
        player->midiSequencer()->applyAutoMedleyBounds(lyrWidget->curData(), playingSong.cutStartBar(), playingSong.cutEndBar(), lyrWidget->lastActiveLyricTick(), player->medleySkipIntro(), player->medleyAutoCutOutro(), player->medleyWaitBars(), player->medleyQuantizeBar(), player->medleyIntroLeadInBars());
    }
    if (player->isUseMedley()) {
            QMessageBox box(this);
            box.setWindowTitle(tr("เมดเลย์"));
            box.setText(tr("โหมดเมดเลย์กำลังเปิดอยู่ ต้องการทำอะไร?"));
            QPushButton *btnSettings = box.addButton(tr("ตั้งค่าเมดเลย์"), QMessageBox::AcceptRole);
            QPushButton *btnOff = box.addButton(tr("ปิดเมดเลย์"), QMessageBox::DestructiveRole);
            box.addButton(tr("ยกเลิก"), QMessageBox::RejectRole);
            box.setDefaultButton(btnSettings);
            box.exec();
            if (box.clickedButton() == btnSettings) {
                openMedleySettings();
            } else if (box.clickedButton() == btnOff) {
                player->setUseMedley(false);
                player->unloadNextMedley();
                if (!player->isPlayerStopped())
                    player->midiSequencer()->setEndTick(0);
                if (btnMedley) { QSignalBlocker b(btnMedley); btnMedley->setChecked(false); }
                ui->detail->setDetail(tr("เมดเลย์ "), tr("ปิด"));
                ui->detail->show();
                detailTimer->start(3000);
                if (this->width() < 1160 && ui->chMix->isVisible())
                    ui->lcdTime->hide();
            }
        } else {
            openMedleySettings();
        }
        if (btnMedley) { QSignalBlocker b(btnMedley); btnMedley->setChecked(player->isUseMedley()); }
        break;
    }
    case Qt::Key_F5: {
        if (player->isPlayerPaused())
            resume();
        else
            play(-1);
        break;
    }
    case Qt::Key_F6: {
        pause();
        break;
    }
    case Qt::Key_F7: {
        stop();
        break;
    }
    case Qt::Key_F8: {
        playPrevious();
        break;
    }
    case Qt::Key_F9: {
        playNext();
        break;
    }
    case Qt::Key_Asterisk: {
        if (ui->chMix->isMuteVoice()) {
            ui->chMix->setMuteVoice(false);
            ui->detail->setDetail(tr("เสียงร้อง "), tr("เปิด"));
        } else {
            ui->chMix->setMuteVoice(true);
            ui->detail->setDetail(tr("เสียงร้อง "), tr("ปิด"));
        }
        {   // keep the bar button in step with the key
            QSignalBlocker block(btnMuteVocal);
            btnMuteVocal->setChecked(ui->chMix->isMuteVoice());
        }
#ifdef KM_HAVE_PROCLOOP
        applyYtVocalCut();
#endif
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_Insert: {
        if (ui->frameSearch->isVisible()) {
            preSetTranspose(db->currentSong()->transpose() + 1);
            break;
        }
        if (ytActive) {
            changeYtKey(+1);
            break;
        }
        playingSong.setTranspose(player->transpose() + 1);
        player->setTranspose(player->transpose() + 1);
        int trp = player->transpose();
        QString t;
        if (trp > 0) t = "+" + QString::number(trp);
        else t = QString::number(trp);
        ui->songDetail->setDetail(&playingSong);
        ui->songDetail->adjustSize();
        ui->detail->setDetail(tr("คีย์เพลง "), t);
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_Delete: {
        if (ui->frameSearch->isVisible()) {
            preSetTranspose(db->currentSong()->transpose() - 1);
            break;
        }
        if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i<0)
                break;
            removeFromPlaylist(i);
            showPlaylist();
            timer2->start(playlist_timeout);
            break;
        }
        if (ytActive) {
            changeYtKey(-1);
            break;
        }
        playingSong.setTranspose(player->transpose() - 1);
        player->setTranspose(player->transpose() - 1);
        int trp = player->transpose();
        QString t;
        if (trp > 0) t = "+" + QString::number(trp);
        else t = QString::number(trp);
        ui->songDetail->setDetail(&playingSong);
        ui->songDetail->adjustSize();
        ui->detail->setDetail(tr("คีย์เพลง "), t);
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_PageUp:
        if (ui->frameSearch->isVisible()) {
            preSetBpmSpeed(db->currentSong()->bpmSpeed() + 1);
        } else if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i < 0)
                break;
            Song *s = playlist[i];
            s->setBpmSpeed(s->bpmSpeed() + 1);
            ui->playlistWidget->updateDetail(i, s);
            timer2->start(playlist_timeout);
        } else {
            addBpmSpeed(1);
        }
        break;
    case Qt::Key_PageDown:
        if (ui->frameSearch->isVisible()) {
            preSetBpmSpeed(db->currentSong()->bpmSpeed() - 1);
        } else if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i < 0)
                break;
            Song *s = playlist[i];
            s->setBpmSpeed(s->bpmSpeed() - 1);
            ui->playlistWidget->updateDetail(i, s);
            timer2->start(playlist_timeout);
        } else {
            addBpmSpeed(-1);
        }
        break;
    case Qt::Key_Home:
        if (playingSong.id() != "") {
            play(-1);
        }
        break;
    case Qt::Key_End:
        endSongSmoothly();
        break;
    case Qt::Key_Up:
        if (ui->playlistWidget->isVisible()) {
            if (ui->playlistWidget->currentRow() > 0 )
                ui->playlistWidget->setCurrentRow( ui->playlistWidget->currentRow() - 1 );
            timer2->start(playlist_timeout);
        } else {
            ui->frameSearch->hide();
            if (ui->playlistWidget->currentRow() == -1)
                ui->playlistWidget->setCurrentRow(0);
            showPlaylist();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(playlist_timeout);
        }
        break;
    case Qt::Key_Down:
        if (ui->playlistWidget->isVisible()) {
            if (ui->playlistWidget->currentRow() < ui->playlistWidget->rowCount() - 1 )
                ui->playlistWidget->setCurrentRow( ui->playlistWidget->currentRow() + 1 );
            timer2->start(playlist_timeout);
        } else {
            ui->frameSearch->hide();
            if (ui->playlistWidget->currentRow() == -1)
                ui->playlistWidget->setCurrentRow(0);
            showPlaylist();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(playlist_timeout);
        }
        break;
    case Qt::Key_Right:
        if (ui->frameSearch->isVisible()) {
            if (searchBoxChangeBpm) {
                searchBoxChangeBpm = false;
                ui->lbSearch->setText(db->searchText() + "_");
            }
            setFrameSearch( db->searchNext() );
            timer2->start(search_timeout);
        } else {
            searchBoxChangeBpm = false;
            db->currentSong()->setBpmSpeed(0);
            db->currentSong()->setTranspose(0);
            ui->playlistWidget->hide();
            if (ui->lbId->text() == "")
                setFrameSearch( db->search("") );
            ui->lbSearch->setText("_");
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Left:
        if (ui->frameSearch->isVisible()) {
            if (searchBoxChangeBpm) {
                searchBoxChangeBpm = false;
                ui->lbSearch->setText(db->searchText() + "_");
            }
            setFrameSearch( db->searchPrevious() );
            timer2->start(search_timeout);
        } else {
            searchBoxChangeBpm = false;
            db->currentSong()->setBpmSpeed(0);
            db->currentSong()->setTranspose(0);
            ui->playlistWidget->hide();
            if (ui->lbId->text() == "")
                setFrameSearch( db->search("") );
            ui->lbSearch->setText("_");
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Tab:
        if (ui->frameSearch->isVisible()) {
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 1, 1, "");
            setFrameSearch( db->nextType(s) );
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Backspace:
        if (ui->frameSearch->isVisible()) {
            if (ui->lbSearch->text() == "_") {
                timer2->start(search_timeout);
                break;
            }
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 2, 2, "");
            setFrameSearch( db->search(s) );
            ui->lbSearch->setText(s + "_");
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (ui->frameSearch->isVisible()) {
            Song *s = db->currentSong();
            addToPlaylist(s);

            if (auto_playnext && playlist.count() == 1 && !isSongPlaying()) {
                play(0);
            } else {
                hideUIFrame();
            }
        }
        if (ui->playlistWidget->isVisible() && ui->playlistWidget->rowCount() > 0) {
            ui->playlistWidget->hide();
            play(ui->playlistWidget->currentRow());
        }
        break;
    case Qt::Key_Space:
        if (player->isUseMedley() && ui->playlistWidget->isVisible()) {
            timer2->stop();
            Song *s = playlist[ui->playlistWidget->currentRow()];
            ui->songMedley->setup(s->cutStartBar(), s->cutEndBar());
            ui->songMedley->show();
            break;
        }

        if (ui->frameSearch->isHidden()) {
            showSongDetail();
            timer2->start(songDetail_timeout);
            break;
        }
    // Queue (open with ↑ ↓): + moves the selected song up, nearer to its turn; - moves it down.
    // "=" is the + key without Shift on the main keyboard; the keypad + / - work as well.
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i > 0)
                swapInPlaylist(i, i-1);
            timer2->start(playlist_timeout);
            break;
        }
    case Qt::Key_Minus:
        if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i >= 0 && i < ui->playlistWidget->rowCount() - 1)
                swapInPlaylist(i, i+1);
            timer2->start(playlist_timeout);
            break;
        }
    default:
        if (searchBoxChangeBpm) {
            ui->lbSearch->setText("_");
            searchBoxChangeBpm = false;
        }
        if (ui->frameSearch->isVisible()) {
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 1, 1, "");
            QString text = searchWithThaiFallback(s + event->text());
            ui->lbSearch->setText(text + "_");
            timer2->start(search_timeout);
        } else {
            db->setSearchType(SearchType::ByAll);
            QString text = searchWithThaiFallback(event->text());
            ui->lbSearch->setText(text + "_");
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    }
}

QString MainWindow::searchWithThaiFallback(const QString &text)
{
    // Typed with the keyboard still in English mode (e.g. "lyxH" for "สายฝ")?
    // When nothing matches, retry with the same keys on the Thai Kedmanee layout.
    Song *s = db->search(text);
    if (!db->lastSearchFound() && text.contains(QRegExp("[A-Za-z]"))) {
        QString thai = SongDatabase::kedmaneeToThai(text);
        Song *t = db->search(thai);
        if (db->lastSearchFound()) {
            setFrameSearch(t);
            return thai;
        }
        db->search(text);   // keep the typed text as the search text
    }
    setFrameSearch(s);
    return text;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->sliderPosition) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const int w = ui->sliderPosition->width();
                if (w > 0) {
                    const double ratio = qBound(0.0, double(me->pos().x()) / double(w), 1.0);
                    const int minVal = ui->sliderPosition->minimum();
                    const int maxVal = ui->sliderPosition->maximum();
                    const int val = int(minVal + ratio * (maxVal - minVal));
                    ui->sliderPosition->setValue(val);
                    // setSliderDown(true) emits sliderPressed() -> onSliderPositionPressed().
                    // Do NOT call it again here: a second call reads the already-paused
                    // state, sets playAfterSeek=false and the song stays paused after seek.
                    ui->sliderPosition->setSliderDown(true);
                    emit ui->sliderPosition->sliderMoved(val);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                const int w = ui->sliderPosition->width();
                if (w > 0) {
                    const double ratio = qBound(0.0, double(me->pos().x()) / double(w), 1.0);
                    const int minVal = ui->sliderPosition->minimum();
                    const int maxVal = ui->sliderPosition->maximum();
                    const int val = int(minVal + ratio * (maxVal - minVal));
                    ui->sliderPosition->setValue(val);
                    emit ui->sliderPosition->sliderMoved(val);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                // setSliderDown(false) emits sliderReleased() -> onSliderPositionReleased().
                // Calling it twice would seek twice and break resume.
                if (ui->sliderPosition->isSliderDown())
                    ui->sliderPosition->setSliderDown(false);
                return true;
            }
        }
    }
    // Player shortcuts typed while the mixer is the active window go to the player.
    // Esc stays with the mixer (closes it); typing into a text / number field stays there.
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            && QApplication::activeWindow() == synthMix && synthMix->isVisible()
            && QApplication::activeModalWidget() == nullptr && QApplication::activePopupWidget() == nullptr) {
        QKeyEvent *k = static_cast<QKeyEvent *>(event);
        QWidget *f = QApplication::focusWidget();
        const bool editing = qobject_cast<QLineEdit *>(f) || qobject_cast<QAbstractSpinBox *>(f)
                || (qobject_cast<QComboBox *>(f) && qobject_cast<QComboBox *>(f)->isEditable());
        if (k->key() != Qt::Key_Escape && !editing && watched == QApplication::focusObject()) {
            if (event->type() == QEvent::KeyPress)
                keyPressEvent(k);
            else
                keyReleaseEvent(k);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier) {
        this->sendDrumPads(event, false);
    }
}

// Opens the Medley settings (BPM / transition bars / drum fill pattern).
// Works whether Medley is currently ON or OFF; OK always turns Medley ON
// with the new settings and re-prepares the next song.
void MainWindow::openMedleySettings()
{
    MedleyDialog dlg(this, player);
    dlg.setModal(true);
    dlg.exec();

    if (player->isUseMedley()) {
        if (player->midiFile() && !player->midiFile()->events().isEmpty()) {
            player->midiFile()->setSingleTempo(true);
            const int targetBpm = player->medleyBPM() > 0 ? player->medleyBPM() : player->currentBpm();
            const int baseBpm = player->currentBpm() - player->bpmSpeed();
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
        const int nextMedleyIdx = (remove_playlist ? 0 : playingIndex + 1);
        if (!player->isPlayerStopped() && (nextMedleyIdx >= 0 && nextMedleyIdx < playlist.size()))
            loadNextMedley(playlist[nextMedleyIdx]);
        const QString barsInfo = player->medleyTransitionBars() > 0
            ? tr("(%1 BPM, ลูกส่งรอ %2 ห้อง)").arg(player->medleyBPM()).arg(player->medleyTransitionBars())
            : tr("(%1 BPM, ต่อทันที)").arg(player->medleyBPM());
        ui->detail->setDetail(tr("เมดเลย์ "), tr("เปิด %1").arg(barsInfo));
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible())
            ui->lcdTime->hide();
    }
    if (btnMedley) { QSignalBlocker b(btnMedley); btnMedley->setChecked(player->isUseMedley()); }
}

void MainWindow::loadNextMedley(Song *song)
{
    if (!song || (song->songType() != "NCN" && song->songType() != "EMK" && song->songType() != "HNK" && song->songType() != "KAR" && song->songType() != "SIB")) {
        player->unloadNextMedley();
        return;
    }
    if ((medleyLoader != nullptr) && medleyLoader->isRunning())
        medleyLoader->wait();

    medleyLoader = new MedleyLoader(this, song, db, player, lyrWidget, secondLyr);
    connect(medleyLoader, &MedleyLoader::finished, [this](){
        medleyLoader->deleteLater();
        medleyLoader = nullptr;
    });
    medleyLoader->start();
}

void MainWindow::addToPlaylist(Song *song)
{
    Song *songToAdd = new Song();
    *songToAdd = *song;

    if (player->isUseMedley() && player->midiFile() && !player->midiFile()->events().isEmpty()) {
        player->midiFile()->setSingleTempo(true);
        const int targetBpm = player->medleyBPM() > 0 ? player->medleyBPM() : player->currentBpm();
        const int baseBpm = player->currentBpm() - player->bpmSpeed();
        player->setBpmSpeed(targetBpm - baseBpm);
        playingSong.setBpmSpeed(player->bpmSpeed());
        player->midiSequencer()->applyAutoMedleyBounds(lyrWidget->curData(), playingSong.cutStartBar(), playingSong.cutEndBar(), lyrWidget->lastActiveLyricTick(), player->medleySkipIntro(), player->medleyAutoCutOutro(), player->medleyWaitBars(), player->medleyQuantizeBar(), player->medleyIntroLeadInBars());
    }
    if (player->isUseMedley()) {
        int medleySpeed = songToAdd->tempo() > 0 ? (player->medleyBPM() - songToAdd->tempo()) : 0;
        songToAdd->setBpmSpeed(songToAdd->bpmSpeed() + medleySpeed);
    }

    playlist.append(songToAdd);
    ui->playlistWidget->addSong(songToAdd);

    const int nextMedleyIdx = (remove_playlist ? 0 : playingIndex + 1);
    if (player->isUseMedley() && !player->isPlayerStopped() && (playlist.size() == (nextMedleyIdx + 1))) {
        loadNextMedley(songToAdd);
    }
}

void MainWindow::removeFromPlaylist(int index)
{
    delete playlist.takeAt(index);
    ui->playlistWidget->removeRow(index);

    if (playlist.size() == 0)
        player->unloadNextMedley();

    if (player->isUseMedley() && (index == playingIndex + 1) && (playlist.size() > playingIndex + 1))
        loadNextMedley(playlist[playingIndex + 1]);
}

void MainWindow::swapInPlaylist(int index, int toIndex)
{
    playlist.swap(index, toIndex);
    ui->playlistWidget->swapRow(index, toIndex);
    ui->playlistWidget->setCurrentRow(toIndex);

    // the playing song (kept in the queue when "remove played songs" is off) moves too
    if (playingIndex == index)
        playingIndex = toIndex;
    else if (playingIndex == toIndex)
        playingIndex = index;

    bool isNextSong = (index == playingIndex + 1) || (toIndex == playingIndex + 1);

    if (player->isUseMedley() && isNextSong && (playlist.size() > playingIndex + 1)) {
        loadNextMedley(playlist[playingIndex + 1]);
    }
}

void MainWindow::updateShutdownRequest()
{
    qApp->quit();
}

void MainWindow::showFrameSearch()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->songDetail->hide();
    ui->playlistWidget->hide();

    ui->frameSearch->show();
}

void MainWindow::showPlaylist()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->songDetail->hide();
    ui->frameSearch->hide();

    ui->playlistWidget->show();
}

void MainWindow::showSongDetail()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->frameSearch->hide();
    ui->playlistWidget->hide();

    ui->songDetail->show();
}


void MainWindow::showChMix()
{
    if (ui->playlistWidget->isVisible()
            || ui->frameSearch->isVisible()
            || ui->songDetail->isVisible())
    {
        return;
    }

    ui->chMix->hide();
    ui->expandChMix->hide();

    if (false && ui->detail->isVisible() && this->width() < 1160) {
        ui->lcdTime->hide();
    }
}

void MainWindow::onChMixMouseLeaved()
{
    if (ui->chMix->isLock())
        return;

    ui->chMix->hide();
    ui->expandChMix->hide();
}

void MainWindow::onChMixLockChanged(bool lock)
{
    if (lock) {
        settings->setValue("ChMixLock", true);
    } else {
        settings->setValue("ChMixLock", false);
    }
}

void MainWindow::hideUIFrame()
{
    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->songDetail->hide();

    if (false && ui->chMix->isLock()) {
        ui->chMix->hide();
        ui->expandChMix->hide();
    }
}

void MainWindow::showCurrentTime()
{
    QTime time = QTime::currentTime();
    QString currentTime = locale.toString(time, "hh:mm:ss");
    ui->lcdTime->display(currentTime);
}

void MainWindow::setFrameSearch(Song *s)
{
    ui->lbId->setText(s->id());
    ui->lbName->setText(" " + s->name());
    ui->lbBtw->setText("-");
    ui->lbArtist->setText(s->artist());
    if (s->key() != "")
        ui->lbTempoKey->setText(" (" + QString::number(s->tempo()) + "-" + s->key() + ")");
    else
        ui->lbTempoKey->setText(" (" + QString::number(s->tempo()) + ")");
    ui->lbType->setText("[" + s->songType() + "]");
    ui->lbLyrics->setText(s->lyrics().replace("\r\n", " "));

    if (s->artist().length() == 0) {
        ui->lbBtw->hide();
    }
}

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

    QAction actionImport(tr("เพิ่มเพลง (NCN / วิดีโอ / MP3 / KAR)..."), this);
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
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::YouTube), tr("YouTube"));
        m->addAction(MenuIcons::make(MenuIcons::Search), tr("ค้นหาเพลง YouTube (เล่นสด)\tF10"), this, [this]() { showYouTubeSearch(); });
        m->addAction(MenuIcons::make(MenuIcons::AiSparkle), tr("ตัดเสียงร้อง YouTube (AI / เร็ว, ระดับเสียง)..."), this, [this]() { showYtVocalDialog(); });
        m->addAction(MenuIcons::make(MenuIcons::Download), tr("ดาวน์โหลดจากลิงก์ YouTube..."), this, [this]() { showYouTubeDownload(); });
    }
    menu.addAction(&actionLists);
    menu.addAction(&actionRecord);
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

void MainWindow::showCheckUpdateDialog()
{
}

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
    taskbarButton->progress()->setValue(tick);
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
        videoPlayer->setPosition(ui->sliderPosition->value());
        if (mediaIsAudio)
            lyrWidget->setSeekPositionCursor(ui->sliderPosition->value());
        if (playAfterSeek)
            videoPlayer->play();
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

    if (player->isPlayerStopped()) {
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
    lyrWidget->hide();
    lyrWidget->switchToLyricsTemp();
    lyrWidget->setSeekPositionCursor(player->positionTick());
    lyrWidget->show();
}

void MainWindow::switchMedleyLyrics2()
{
    if (secondLyr != nullptr) {
        secondLyr->hide();
        secondLyr->switchToLyricsTemp();
        secondLyr->setSeekPositionCursor(player->positionTick());
        secondLyr->show();
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

    positionTimer->stop();

    onPlayerDurationTickChanged(player->durationTick());
    onPlayerDurationMSChanged(player->durationMs());

    #ifdef _WIN32
    taskbarButton->progress()->setValue(0);
    taskbarButton->progress()->setMaximum(player->durationTick());
    taskbarButton->progress()->resume();
    taskbarButton->progress()->show();
    #endif

    ui->rhmWidget->setBeat(MidiHelper::calculateBeats(player->midiFile()), player->beatCount());

    positionTimer->start();

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
    int bpm = player->currentBpm() + speed;
    int bpmSp = player->bpmSpeed() + speed;

    player->setBpmSpeed(bpmSp);

    if (playingSong.id() != "")
        playingSong.setBpmSpeed(bpmSp);
    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();

    QString value = QString::number(bpm);
    if (bpmSp != 0) {
        value += bpmSp > 0 ? " (+" : " (";
        value += QString::number(bpmSp) + ")";
    }

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
