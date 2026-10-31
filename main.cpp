#include "MainWindow.h"
#include <QPainterPath>
#include <QRegion>

#include <QApplication>
#include <QQuickWindow>
#include <QSettings>
#include <QSplashScreen>
#include <QDirIterator>
#include <QMetaType>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QThread>
#include <QMessageBox>
#include <QFileInfo>
#ifndef __linux__
#endif
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif
#include <QFontDatabase>
#include <QSettings>
#include <QDir>
#include <QTimer>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <cmath>
#include <atomic>
#include <bass.h>
#include <bassmidi.h>

#include "BASSFX/VSTFX.h"
#include "version.h"
#include "Config.h"
#include "Utils.h"
#include "Theme.h"
#include "Widgets/DialogSkin.h"


#ifdef __linux__
#include <QFontDatabase>
#endif

void registerMetaType();

void checkDatabase(QSplashScreen *splash, SongDatabase *db);
void loadSoundfonts(QSplashScreen *splash, MidiSynthesizer *synth);

#ifndef __linux__
void loadVSTi(QSplashScreen *splash, MidiSynthesizer *synth);
void makeVSTList(QSplashScreen *splash, MidiSynthesizer *synth);
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    // Qt 5 uses DirectShow by default on Windows, which cannot open MP4 / H.264 without extra
    // codecs; Media Foundation (built into Windows) plays the karaoke and background videos.
    #ifdef _WIN32
    if (qEnvironmentVariableIsEmpty("QT_MULTIMEDIA_PREFERRED_PLUGINS"))
        qputenv("QT_MULTIMEDIA_PREFERRED_PLUGINS", "windowsmediafoundation");
    #endif

    #ifdef PTHK_WEBENGINE
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);    // required by Qt WebEngine
    // ANGLE (OpenGL ES on Direct3D) instead of the graphics driver's desktop OpenGL: copying the
    // YouTube picture to the 2nd screen crashed inside the AMD OpenGL driver (atio6axx.dll)
    // within a minute; with ANGLE it runs without problems
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
    #endif

    QApplication a(argc, argv);

    // Started by "Open config": give the previous instance time to release audio/MIDI devices.
    if (a.arguments().contains("--delay"))
        QThread::msleep(1500);

    QCoreApplication::setApplicationName(VER_PRODUCTNAME_STR);
    QCoreApplication::setApplicationVersion(VER_FILEVERSION_STR);
    QCoreApplication::setOrganizationName(VER_COMPANYNAME_STR);
    QCoreApplication::setOrganizationDomain(VER_COMPANYDOMAIN_STR);

    registerMetaType();

    QPixmap *pixmap = new QPixmap(":/Icons/App/pthk-splash.png");
    QSplashScreen *splash = new QSplashScreen(*pixmap);
    {   // rounded splash window
        QPainterPath sp;
        sp.addRoundedRect(QRectF(splash->rect()), 14, 14);
        splash->setMask(QRegion(sp.toFillPolygon().toPolygon()));
    }
    splash->show();
    qApp->processEvents();

    splash->showMessage("กำลังเริ่มโปรแกรม...", Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
    qApp->processEvents();

    { // Config Dir
        Config::initConfigDataPath();

        #ifdef PTHK_WEBENGINE
        // YouTube drawn by the CPU (Qt Quick software renderer + Chromium without GPU).
        // The AI vocal cut runs on the GPU in long bursts; a GPU-drawn player made every
        // picture copy wait up to ~0.4 s for it (measured), so the video stuttered.
        // Must be set before the first web view / Qt Quick window exists.
        if (QSettings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat).value("YtSoftwareVideo", true).toBool()) {
            QQuickWindow::setSceneGraphBackend(QSGRendererInterface::Software);
            QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
            if (!flags.contains("--disable-gpu"))
                qputenv("QTWEBENGINE_CHROMIUM_FLAGS", (flags + " --disable-gpu").trimmed());
        }
        #endif

        QDir dir(TEMP_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(TEMP_DIR_PATH);

        dir.setPath(ALL_DATA_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(ALL_DATA_DIR_PATH);

        dir.setPath(Config::CONFIG_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(Config::CONFIG_DIR_PATH);
    }

    // Color theme (follow Windows / dark / light)
    Theme::init(a);
    a.installEventFilter(new DialogSkin(&a));

    // crisper text rendering
    {
        QFont f = a.font();
        f.setHintingPreference(QFont::PreferFullHinting);
        f.setStyleStrategy(QFont::PreferAntialias);
        a.setFont(f);
    }


    // Bundled font with the infinity sign used by the dB read-outs (all platforms)
    QFontDatabase::addApplicationFont(":/Fonts/DejaVu/DejaVuSans-Bold.ttf");

    // Add font for linux
    #ifdef __linux__
    QFontDatabase::addApplicationFont(":/Fonts/THSarabunNew/THSarabunNew Bold.ttf");
    #endif
    //----------------------------------

    // Player edition: MainWindow is the visible karaoke player (search, playlist,
    // lyrics); the synth mixer is a second window that only hides when closed.
    MainWindow w;
    qApp->setWindowIcon(QIcon(":/Icons/App/pthk.png"));

    checkDatabase(splash, w.database());
    loadSoundfonts(splash, w.midiPlayer()->midiSynthesizer());

    #ifndef __linux__
    loadVSTi(splash, w.midiPlayer()->midiSynthesizer());
    makeVSTList(splash, w.midiPlayer()->midiSynthesizer());
    w.synthMixerDialog()->setVSTVendorMenu();
    #endif
    w.synthMixerDialog()->setFXToSynth();

    SynthMixerDialog *mixer = w.synthMixerDialog();
    mixer->setWindowTitle("PTHK Karaoke - Mixer");
    mixer->setCloseQuitsApp(false);
    w.showSavedWindowState();
    {   // the mixer only opens by itself when chosen in the menu (it covered the lyrics)
        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        if (st.value("ShowMixerAtStart", false).toBool())
            mixer->show();
    }
    w.activateWindow();     // keyboard (song number search) goes to the player, not the mixer
    {   // auto-hide of the mixer while the karaoke program is used (Settings)
        QSettings ahs(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        w.setMixerAutoHide(ahs.value("MixerAutoHide", false).toBool(),
                           ahs.value("MixerAutoHideDelay", 3.0).toDouble(),
                           ahs.value("MixerAutoHideProgram", "").toString());
    }

    splash->finish(&w);

    {   // song files given on the command line (dropped on the program icon, "Open with"): add them
        QStringList files;
        for (const QString &arg : a.arguments().mid(1))
            if (QFileInfo(arg).isFile())
                files << arg;
        if (!files.isEmpty())
            QTimer::singleShot(500, &w, [&w, files]() { w.importSongs(files); });

        // --update-database: rebuild the whole song database (as Settings > update)
        if (a.arguments().contains("--update-database")) {
            QTimer::singleShot(500, &w, [&w]() {
                w.database()->setUpdateType(UpdateType::UpdateAll);
                w.database()->start();
            });
        }
        // --update-new: only the NCN / HNK files not in the database yet
        if (a.arguments().contains("--update-new"))
            QTimer::singleShot(500, &w, [&w]() { w.updateNewSongs(); });
    }

    delete splash;
    delete pixmap;

    const int ret = a.exec();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return ret;
}


void registerMetaType()
{
    qRegisterMetaType<MidiEvent>("MidiEvent");
    qRegisterMetaType<InstrumentType>("InstrumentType");

    //<QList<int>>("QList<int>");
    qRegisterMetaTypeStreamOperators<QList<int>>("QList<int>");

    //qRegisterMetaType<QList<uint>>("QList<uint>");
    qRegisterMetaTypeStreamOperators<QList<uint>>("QList<uint>");

    //qRegisterMetaType<QList<float>>("QList<float>");
    qRegisterMetaTypeStreamOperators<QList<float>>("QList<float>");

    //qRegisterMetaType<QList<bool>>("QList<bool>");
    qRegisterMetaTypeStreamOperators<QList<bool>>("QList<bool>");

    //qRegisterMetaType<QList<QList<float>>>("QList<QList<float>>");
    qRegisterMetaTypeStreamOperators<QList<QList<float>>>("QList<QList<float>>");

    qRegisterMetaTypeStreamOperators<QList<QByteArray>>("QList<QByteArray>>");
}

void checkDatabase(QSplashScreen *splash, SongDatabase *db)
{
    if (db->isNewVersion())
        return;

    splash->showMessage("กำลังปรับปรุงฐานข้อมูลเพลง", Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
    qApp->processEvents();

    db->updateToNewVersion();
}

void loadSoundfonts(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);

    synth->setLoadAllSoundfont(settings.value("SynthSoundfontsLoadAll", false).toBool());

    // set soundfont to synth
    QStringList sfList = settings.value("SynthSoundfonts", QStringList()).toStringList();
    bool sfRelocated = false;
    for (int i = 0; i < sfList.count(); i++) {
        const QString rel = Config::relocatePath(sfList.at(i));
        if (rel != sfList.at(i)) { sfList[i] = rel; sfRelocated = true; }
    }
    if (sfRelocated) settings.setValue("SynthSoundfonts", sfList);
    if (sfList.isEmpty()) {
        QDir sfDir(qApp->applicationDirPath() + "/SoundFonts");
        for (const QFileInfo &fi : sfDir.entryInfoList(QStringList() << "*.sf2" << "*.SF2", QDir::Files, QDir::Name))
            sfList.append(QDir::fromNativeSeparators(fi.absoluteFilePath()));
        if (!sfList.isEmpty())
            settings.setValue("SynthSoundfonts", sfList);
    }
    settings.beginReadArray("SynthSoundfontsVolume");
    for (int i=0; i<sfList.count(); i++)
    {
        settings.setArrayIndex(i);
        int volume = settings.value("SoundfontVolume", 100).toInt();

        splash->showMessage("กำลังโหลด : " + QFileInfo(sfList.at(i)).fileName(), Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
        splash->repaint();

        if (synth->addSoundfont(sfList.at(i)))
            synth->setSoundfontVolume(i, volume / 100.0f);
    }
    settings.endArray();
    if (synth->soundfontFiles().isEmpty()) {
        QDir sfDir(qApp->applicationDirPath() + "/SoundFonts");
        for (const QFileInfo &fi : sfDir.entryInfoList(QStringList() << "*.sf2" << "*.SF2", QDir::Files, QDir::Name)) {
            if (synth->addSoundfont(QDir::fromNativeSeparators(fi.absoluteFilePath())))
                synth->setSoundfontVolume(synth->soundfontFiles().count() - 1, 1.0f);
        }
    }


    // set Map soundfont
    for (int i = 0; i < SF_PRESET_COUNT; i++) {
        QString sfKey = "SynthSoundfontsMap";
        QString drKey = "SynthSoundfontsDrumMap";
        if (i > 0) {
            sfKey = sfKey + QString::number(i);
            drKey = drKey + QString::number(i);
        }
        QList<int> sfMap     = settings.value(sfKey).value<QList<int>>();
        QList<int> sfDrumMap = settings.value(drKey).value<QList<int>>();

        if (sfMap.count() == 0)
            sfMap = synth->getMapSoundfontIndex(i);
        if (sfDrumMap.count() == 0)
            sfDrumMap = synth->getDrumMapSfIndex(i);

        synth->setMapSoundfontIndex(i, sfMap, sfDrumMap);
    }
}

#ifndef __linux__

void loadVSTi(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);

    st.beginReadArray("VSTiGroup");

    for (int i=0; i<4; i++)
    {
        st.setArrayIndex(i);

        QString      filePath   = Config::relocatePath(st.value("VstiFilePath", "").toString());
        int          program    = st.value("VstiPrograms", 0).toInt();
        QList<float> params     = st.value("VstiParams").value<QList<float>>();
        QByteArray   chunk      = st.value("VstiChunk", QByteArray()).toByteArray();

        if (filePath == "")
            continue;

#ifdef _WIN32
        if (!Utils::is64BitDll(filePath))
            continue;
#endif
        splash->showMessage("กำลังโหลด : " + QFileInfo(filePath).fileName(), Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
        splash->repaint();

        DWORD vsti = synth->setVSTiFile(i, filePath);

        if (vsti != 0)
        {
            if (chunk.length() > 0) {
                bv::SetChunk(vsti, false, chunk.constData(), chunk.length());
            } else {
                bv::SetProgram(vsti, program);
                FX::setVSTParams(vsti, params);
            }
        }
    }

    st.endArray();
}

void makeVSTList(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    QStringList dirs = st.value("VSTDirs", QStringList()).toStringList();

    QStringList vstDirs;
    vstDirs << qApp->applicationDirPath() + "/VST";
    for (QString &d : dirs) d = Config::relocatePath(d);
    vstDirs += dirs;

    QMap<uint, VSTNamePath> vstList;

    for (const QString &dir : vstDirs)
    {
        QDirIterator it(dir, QStringList() << "*.DLL" << "*.dll" << "*.vst3" << "*.VST3",
                        QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks, QDirIterator::Subdirectories);

        while(it.hasNext()) {

            it.next();

            // skip files inside a *.vst3 folder (the folder itself is scanned)
            if (it.filePath().contains(".vst3/", Qt::CaseInsensitive))
                continue;

#ifdef _WIN32
            if (!Utils::is64BitDll(it.filePath()))
                continue;
#endif
            splash->showMessage("กำลังตรวจสอบ : " + it.fileName(), Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
            splash->repaint();

            QList<VSTNamePath> infos;
            if (!Utils::vstInfos(it.filePath(), &infos))
                continue;

            for (const VSTNamePath &info : infos)
                vstList[info.uniqueID] = info;
        }
    }

    synth->setVSTList(vstList);
}

#endif
