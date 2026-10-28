#include "Config.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

QString Config::DATABASE_DIR_PATH       = ALL_DATA_DIR_PATH + "/Data";
QString Config::DATABASE_FILE_PATH      = Config::DATABASE_DIR_PATH + "/Database.db3";

QString Config::CONFIG_DIR_PATH         = ALL_DATA_DIR_PATH + "/Config";
QString Config::CONFIG_APP_FILE_PATH    = Config::CONFIG_DIR_PATH + "/PTHKKaraoke.conf";
QString Config::CONFIG_SYNTH_FILE_PATH  = Config::CONFIG_DIR_PATH + "/SynthMixer.conf";

static bool s_portableMode = false;

bool Config::isPortableMode()
{
    return s_portableMode;
}

void Config::initConfigDataPath()
{
    const QString appDir = QDir::fromNativeSeparators(qApp->applicationDirPath());

    bool removableDrive = false;
#ifdef Q_OS_WIN
    if (appDir.length() >= 3 && appDir[1] == ':') {
        const QString root = QDir::toNativeSeparators(appDir.left(3));
        if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) == DRIVE_REMOVABLE)
            removableDrive = true;
    }
#endif

    // Ensure standard SoundFonts / Songs / VST folders exist when outside protected Program Files
    if (!appDir.contains("Program Files", Qt::CaseInsensitive)) {
        const char *stdDirs[] = {
            "/SoundFonts",
            "/VST",
            "/Songs/NCN/Song",
            "/Songs/NCN/Lyrics",
            "/Songs/NCN/Cursor",
            "/Songs/HNK",
            "/Songs/KAR",
            "/Songs/VIDEO",
            "/Songs/MP3"
        };
        QDir d;
        for (const char *sub : stdDirs)
            d.mkpath(appDir + QLatin1String(sub));
    }

    const bool hasPortableDirs = QDir(appDir + "/Data").exists()
                              || QDir(appDir + "/Config").exists()
                              || QFileInfo::exists(appDir + "/Portable.txt")
                              || removableDrive;

    if (hasPortableDirs) {
        s_portableMode = true;
        QDir().mkpath(appDir + "/Data");
        QDir().mkpath(appDir + "/Config");

        DATABASE_DIR_PATH = appDir + "/Data";
        DATABASE_FILE_PATH = DATABASE_DIR_PATH + "/Database.db3";

        CONFIG_DIR_PATH = appDir + "/Config";
        CONFIG_APP_FILE_PATH = CONFIG_DIR_PATH + "/PTHKKaraoke.conf";
        CONFIG_SYNTH_FILE_PATH = CONFIG_DIR_PATH + "/SynthMixer.conf";
    }

    // settings made before the rename (KaraMixer.conf) carry over once
    const QString old = QFileInfo(CONFIG_APP_FILE_PATH).absolutePath() + "/KaraMixer.conf";
    if (!QFile::exists(CONFIG_APP_FILE_PATH) && QFile::exists(old))
        QFile::copy(old, CONFIG_APP_FILE_PATH);
}

QString Config::relocatePath(const QString &savedPath)
{
    if (savedPath.trimmed().isEmpty())
        return savedPath;

    const QString p = QDir::fromNativeSeparators(savedPath);
    if (QFileInfo::exists(p))
        return p;

    const QString appDir = QDir::fromNativeSeparators(qApp->applicationDirPath());

    // 1. Try the current drive letter where PTHKKaraoke.exe is running (e.g. D:/... -> E:/... or F:/...)
    if (p.length() >= 3 && p[1] == ':' && appDir.length() >= 2 && appDir[1] == ':') {
        const QString cand = appDir.left(2) + p.mid(2);
        if (QFileInfo::exists(cand))
            return cand;
    }

    // 2. Try resolving portable subfolders relative to the current application directory
    const char *portableMarkers[] = {
        "/SoundFonts/", "/Songs/", "/VST/",
        "/SoundFonts",  "/Songs",  "/VST"
    };
    for (const char *marker : portableMarkers) {
        const int idx = p.indexOf(QLatin1String(marker), 0, Qt::CaseInsensitive);
        if (idx >= 0) {
            const QString cand = appDir + p.mid(idx);
            if (QFileInfo::exists(cand))
                return cand;
        }
    }

    // 3. Scan mounted drives (External HD / Flash Drive mounted under a new drive letter)
    if (p.length() >= 3 && p[1] == ':') {
        for (const QFileInfo &drv : QDir::drives()) {
            const QString root = QDir::fromNativeSeparators(drv.absoluteFilePath());
            if (root.length() >= 2 && root[1] == ':') {
                const QString cand = root.left(2) + p.mid(2);
                if (QFileInfo::exists(cand))
                    return cand;
            }
        }
    }

    return savedPath;
}
