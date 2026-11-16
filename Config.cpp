#include "Config.h"
#include <QVariant>
#include <QSettings>
#include <QDirIterator>

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

// A flash drive / external HD gets another drive letter (or folder) on another computer, and a
// copied "PTHK Karaoke" folder can sit anywhere. Every saved path (song folders, SoundFonts, VST,
// backgrounds, slideshow, sound pads, medley sets...) that pointed into the old program folder is
// moved along to the new one, and paths on the old drive letter go to the new letter (C: paths
// belong to the computer and are left alone).
// All the settings files of the Config folder are handled: the INI files through QSettings (so
// Thai folder names, which INI files store escaped, are compared correctly), other text files as
// UTF-8 text. "portable-location.txt" remembers where the program was last time.
namespace {
struct Rebase {
    QString oldApp, newApp, oldDrive, newDrive;
    bool driveMoved = false;

    // one stored string: may hold one path or several (";" lists), either slash style
    QString apply(QString v) const
    {
        const QString before = v;
        auto swap = [&v](const QString &from, const QString &to) {
            if (from.isEmpty()) return;
            v.replace(from, to, Qt::CaseInsensitive);
        };
        const QString oA = oldApp, nA = newApp;
        const QString oB = QString(oldApp).replace('/', '\\'), nB = QString(newApp).replace('/', '\\');
        // "<old>/" and "<old>" at the end of the value (a folder setting)
        swap(oA + "/", nA + "/");
        swap(oB + "\\", nB + "\\");
        if (v.compare(oA, Qt::CaseInsensitive) == 0) v = nA;
        if (v.compare(oB, Qt::CaseInsensitive) == 0) v = nB;
        if (driveMoved) {
            swap(oldDrive + "/", newDrive + "/");
            swap(oldDrive + "\\", newDrive + "\\");
        }
        Q_UNUSED(before)
        return v;
    }

    QVariant applyVar(const QVariant &val, bool *changed) const
    {
        if (val.type() == QVariant::String) {
            const QString n = apply(val.toString());
            if (n != val.toString()) { *changed = true; return n; }
        } else if (val.type() == QVariant::StringList) {
            QStringList l = val.toStringList();
            bool c = false;
            for (QString &e : l) {
                const QString n = apply(e);
                if (n != e) { e = n; c = true; }
            }
            if (c) { *changed = true; return l; }
        }
        return val;
    }
};
}

static void rebasePortablePaths(const QString &appDir)
{
    const QString marker = Config::CONFIG_DIR_PATH + "/portable-location.txt";
    QString oldApp;
    {
        QFile m(marker);
        if (m.open(QIODevice::ReadOnly))
            oldApp = QDir::fromNativeSeparators(QString::fromUtf8(m.readAll()).trimmed());
    }
    const QString newApp = appDir;
    if (!oldApp.isEmpty() && oldApp.compare(newApp, Qt::CaseInsensitive) != 0) {
        Rebase rb;
        rb.oldApp = oldApp;
        rb.newApp = newApp;
        rb.oldDrive = oldApp.left(2);       // "E:"
        rb.newDrive = newApp.left(2);
        rb.driveMoved = rb.oldDrive.compare(rb.newDrive, Qt::CaseInsensitive) != 0
                        && rb.oldDrive.compare(QStringLiteral("C:"), Qt::CaseInsensitive) != 0
                        && rb.oldDrive.length() == 2 && rb.oldDrive[1] == ':';

        QDirIterator it(Config::CONFIG_DIR_PATH, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            const QString ext = QFileInfo(file).suffix().toLower();
            if (ext == "conf" || ext == "ini") {
                // medley sets are written as UTF-8 INI, the main settings with escapes
                const bool utf8 = file.contains("/MedleySets/", Qt::CaseInsensitive);
                QSettings st(file, QSettings::IniFormat);
                if (utf8)
                    st.setIniCodec("UTF-8");
                for (const QString &k : st.allKeys()) {
                    bool changed = false;
                    const QVariant n = rb.applyVar(st.value(k), &changed);
                    if (changed)
                        st.setValue(k, n);
                }
                st.sync();
            } else if (ext == "bmpl" || ext == "json" || ext == "txt" || ext == "m3u" || ext == "lst") {
                if (QFileInfo(file).fileName() == "portable-location.txt")
                    continue;
                QFile f(file);
                if (!f.open(QIODevice::ReadOnly))
                    continue;
                const QString text = QString::fromUtf8(f.readAll());
                f.close();
                QStringList lines = text.split('\n');
                bool changed = false;
                for (QString &l : lines) {
                    const QString n = rb.apply(l);
                    if (n != l) { l = n; changed = true; }
                }
                if (changed && f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    f.write(lines.join('\n').toUtf8());
            }
        }
    }
    QFile m(marker);
    if (m.open(QIODevice::WriteOnly | QIODevice::Truncate))
        m.write(newApp.toUtf8());
}

// First start of a copied / installed folder: songs and SoundFonts put next to the program
// ("Songs\NCN", "Songs\SIB", ... "SoundFonts") are used without any setting. A library setting is
// taken over only when it is empty or points to a folder this computer does not have; the song
// database is then filled by the main window ("AutoSetup/ScanSongs").
static bool hasAnyFile(const QString &dir)
{
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

// Copies a file that another program may have open (the song database of a PTHK Karaoke that is
// still running): opened with every share flag, written to a temporary name, then renamed.
static bool copySharedFile(const QString &src, const QString &dst)
{
#ifdef Q_OS_WIN
    HANDLE in = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(src).utf16()), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE)
        return false;
    QFile out(dst + ".part");
    bool ok = out.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QByteArray buf(4 << 20, Qt::Uninitialized);
    DWORD got = 0;
    while (ok && ReadFile(in, buf.data(), DWORD(buf.size()), &got, nullptr) && got > 0)
        ok = out.write(buf.constData(), qint64(got)) == qint64(got);
    CloseHandle(in);
    out.close();
    if (ok) {
        QFile::remove(dst);
        ok = QFile::rename(dst + ".part", dst);
    }
    if (!ok)
        QFile::remove(dst + ".part");
    return ok;
#else
    return QFile::copy(src, dst);
#endif
}

// The installer's one-folder mode on a computer that used PTHK Karaoke before brought the old
// settings over and set "AutoSetup/ImportProfileData": the song database / lyrics index of the user
// profile come along now (the installer itself could not read them while the old program ran).
static void importProfileData()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    if (!st.value("AutoSetup/ImportProfileData", false).toBool())
        return;
    st.remove("AutoSetup/ImportProfileData");
    st.sync();
    const QString from = ALL_DATA_DIR_PATH + "/Data";
    for (const QFileInfo &fi : QDir(from).entryInfoList({ "*.db3" }, QDir::Files)) {
        const QString dst = Config::DATABASE_DIR_PATH + "/" + fi.fileName();
        if (!QFileInfo::exists(dst))
            copySharedFile(fi.absoluteFilePath(), dst);
    }
}

static void autoBindAppFolders(const QString &appDir)
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    const struct { const char *key; const char *dirs[3]; } libs[] = {
        { "NCNPath",   { "/Songs/NCN", nullptr, nullptr } },
        { "HNKPath",   { "/Songs/HNK", "/Songs/EMK", nullptr } },
        { "SIBPath",   { "/Songs/SIB", nullptr, nullptr } },
        { "KARPath",   { "/Songs/KAR", nullptr, nullptr } },
        { "VideoPath", { "/Songs/VIDEO", "/Songs/Video", nullptr } },
        { "MP3Path",   { "/Songs/MP3", nullptr, nullptr } },
    };
    bool scan = false;
    for (const auto &l : libs) {
        const QString cur = Config::relocatePath(st.value(l.key).toString());
        if (!cur.isEmpty() && QDir(cur).exists() && hasAnyFile(cur))
            continue;                               // a working library: keep it
        for (const char *d : l.dirs) {
            if (!d) break;
            const QString cand = appDir + QLatin1String(d);
            if (QDir(cand).exists() && hasAnyFile(cand)) {
                st.setValue(l.key, cand);
                scan = true;
                break;
            }
        }
    }
    // SoundFonts: when none of the saved ones is on this computer, the ones next to the program
    QStringList sfs = st.value("SynthSoundfonts").toStringList();
    bool anySf = false;
    for (const QString &f : sfs)
        if (QFileInfo(Config::relocatePath(f)).isFile()) { anySf = true; break; }
    if (!anySf) {
        QStringList found;
        QDirIterator it(appDir + "/SoundFonts", { "*.sf2", "*.sf3", "*.sfz", "*.SF2", "*.SF3", "*.SFZ" },
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            found << it.next();
        found.sort(Qt::CaseInsensitive);
        if (!found.isEmpty())
            st.setValue("SynthSoundfonts", found);
    }
    if (scan)
        st.setValue("AutoSetup/ScanSongs", true);
    st.sync();
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
            "/Songs/SIB",
            "/Songs/KAR",
            "/Songs/VIDEO",
            "/Songs/MP3",
            "/Backgrounds"
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
        rebasePortablePaths(appDir);
        importProfileData();
        autoBindAppFolders(appDir);
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
