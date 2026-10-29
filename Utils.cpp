#include "Utils.h"

#include <thread>

#include <QDataStream>
#include <QTextStream>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QSet>
#include <QTextCodec>
#include <QRegExp>
#ifdef _WIN32
#include <windows.h>
#endif

#include "Config.h"
#include "Midi/MidiHelper.h"

Utils::Utils()
{

}

QVector<long> Utils::readCurFile(const QString &curFile, uint32_t resolution)
{
    QVector<long> curs;
    QFile f(curFile);
    f.open(QIODevice::ReadOnly);

    QDataStream in(&f);
    while (!in.atEnd()) {
        quint8 b1 = 0;
        quint8 b2 = 0;
        in >> b1;
        in >> b2;
        long cs = (b1 + (b2 << 8)) * resolution / 24;
        curs.append(cs);
    }
    f.close();
    return curs;
}

QVector<long> Utils::readCurFile(const QByteArray &data, uint32_t resolution)
{
    QVector<long> curs;
    QDataStream in(data);
    while (!in.atEnd()) {
        quint8 b1 = 0;
        quint8 b2 = 0;
        in >> b1;
        in >> b2;
        long cs = (b1 + (b2 << 8)) * resolution / 24;
        curs.append(cs);
    }
    return curs;
}

QString Utils::decodeThaiText(const QByteArray &data)
{
    // A UTF-8 BOM, or bytes that are valid UTF-8, mean UTF-8;
    // TIS-620 / Windows-874 Thai (bytes 0xA1..0xFB) is almost never valid UTF-8.
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.mid(3));

    QTextCodec::ConverterState state;
    QString utf8 = QTextCodec::codecForName("UTF-8")->toUnicode(data.constData(), data.size(), &state);
    if (state.invalidChars == 0)
        return utf8;

    // Windows-874 (TIS-620 plus a few punctuation bytes Thai Windows editors write,
    // e.g. 0xA0 no-break space, which Qt's TIS-620 codec turns into U+FFFD)
    QString out;
    out.reserve(data.size());
    for (char ch : data) {
        const uchar b = uchar(ch);
        if (b < 0x80)
            out += QChar(b);
        else if (b >= 0xA1)
            out += QChar(0x0E00 + (b - 0xA0));          // Thai block, same order as the bytes
        else switch (b) {
            case 0xA0: out += QChar(' '); break;
            case 0x80: out += QChar(0x20AC); break;     // euro
            case 0x85: out += QChar(0x2026); break;     // ellipsis
            case 0x91: out += QChar(0x2018); break;
            case 0x92: out += QChar(0x2019); break;
            case 0x93: out += QChar(0x201C); break;
            case 0x94: out += QChar(0x201D); break;
            case 0x95: out += QChar(0x2022); break;
            case 0x96: out += QChar(0x2013); break;
            case 0x97: out += QChar(0x2014); break;
            // unused bytes still become one character: the lyric cursors count characters
            default:   out += QChar(' '); break;
        }
    }
    return out;
}

// The first 4 lines are name, artist, key and a spare line
static QString lyricsAfterHeader(const QString &text)
{
    QStringList lines = text.split(QRegExp("\r\n|\n|\r"));
    return lines.mid(4).join("\n");
}

QString Utils::readLyrics(const QString &lyrFile)
{
    QFile f(lyrFile);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return lyricsAfterHeader(decodeThaiText(f.readAll()));
}

QString Utils::readLyrics(const QByteArray &data)
{
    return lyricsAfterHeader(decodeThaiText(data));
}

uint Utils::concurentThreadsSupported()
{
    return std::thread::hardware_concurrency();
}

// VSTChecker is looked up next to the program (not in the current directory) and every failed check is logged,
// so a plug-in that does not show up in the list can be explained (~/.PTHKKaraoke/vstscan.log).
static QString vstCheckerExe()
{
    return QCoreApplication::applicationDirPath() + "/" + QString(VST_CHECKER_NAME);
}

static void logVstScan(const QString &path, QProcess &process)
{
    QFile f(QDir::homePath() + "/.PTHKKaraoke/vstscan.log");
    if (f.size() > 200000)
        f.remove();
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&f);
    out.setCodec("UTF-8");
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "  " << path
        << "  exit=" << process.exitCode() << " status=" << (process.exitStatus() == QProcess::CrashExit ? "crash" : "normal")
        << " error=" << int(process.error())
        << " out=" << QString::fromUtf8(process.readAllStandardOutput()).trimmed().replace('\n', ' ')
        << " err=" << QString::fromUtf8(process.readAllStandardError()).trimmed().replace('\n', ' ') << "\n";
}

// ---- plug-in scan: safety net -------------------------------------------------------------------------------
// Scanning loads every plug-in in a helper process. Some plug-ins open their own windows (licence / copy-protection
// errors, "this host is not supported") that can hide behind the mixer and block the scan, or never return at all.
// While the helper runs, any window it opens is closed automatically and written to the log; a plug-in that hangs,
// crashes or only produced such windows is put on a skip list (~/.PTHKKaraoke/vst-skip.txt) and not loaded again
// until the file changes (or the list is cleared with Ctrl+click on the update button).
namespace {
    int g_vstSkipped = 0;
    QSet<QString> g_skip;
    bool g_skipLoaded = false;

    QString skipFilePath() { return QDir::homePath() + "/.PTHKKaraoke/vst-skip.txt"; }

    QString skipKey(const QString &path)
    {
        QFileInfo fi(path);
        return QDir::toNativeSeparators(fi.absoluteFilePath()) + "|" + QString::number(fi.size()) + "|"
               + QString::number(fi.lastModified().toMSecsSinceEpoch());
    }

    void loadSkip()
    {
        if (g_skipLoaded) return;
        g_skipLoaded = true;
        QFile f(skipFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QTextStream in(&f);
        in.setCodec("UTF-8");
        while (!in.atEnd()) {
            const QString l = in.readLine().trimmed();
            if (!l.isEmpty()) g_skip.insert(l);
        }
    }

    void addSkip(const QString &path)
    {
        loadSkip();
        const QString key = skipKey(path);
        if (g_skip.contains(key)) return;
        g_skip.insert(key);
        QFile f(skipFilePath());
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            out.setCodec("UTF-8");
            out << key << "\n";
        }
    }

    void logVstLine(const QString &msg)
    {
        QFile f(QDir::homePath() + "/.PTHKKaraoke/vstscan.log");
        if (f.size() > 200000) f.remove();
        if (!f.open(QIODevice::Append | QIODevice::Text)) return;
        QTextStream out(&f);
        out.setCodec("UTF-8");
        out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "  " << msg << "\n";
    }

#ifdef _WIN32
    struct ChildText { QString text; };
    BOOL CALLBACK enumChildProc(HWND h, LPARAM lp)
    {
        wchar_t cls[32] = {0};
        GetClassNameW(h, cls, 31);
        if (QString::fromWCharArray(cls).compare("Static", Qt::CaseInsensitive) == 0) {
            wchar_t t[512] = {0};
            GetWindowTextW(h, t, 511);
            const QString s = QString::fromWCharArray(t).trimmed();
            if (!s.isEmpty()) {
                ChildText *c = reinterpret_cast<ChildText *>(lp);
                if (!c->text.isEmpty()) c->text += " ";
                c->text += s;
            }
        }
        return TRUE;
    }

    struct WinCtx { DWORD pid; QStringList closed; };
    BOOL CALLBACK enumWinProc(HWND h, LPARAM lp)
    {
        WinCtx *c = reinterpret_cast<WinCtx *>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != c->pid || !IsWindowVisible(h))
            return TRUE;
        wchar_t title[256] = {0};
        GetWindowTextW(h, title, 255);
        ChildText ct;
        EnumChildWindows(h, enumChildProc, reinterpret_cast<LPARAM>(&ct));
        c->closed << QString("'%1' %2").arg(QString::fromWCharArray(title), ct.text);
        PostMessageW(h, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);   // OK / default button
        PostMessageW(h, WM_CLOSE, 0, 0);
        return TRUE;
    }
#endif

    // close every visible window owned by the checker process; returns what was closed
    QStringList closeCheckerWindows(qint64 pid)
    {
#ifdef _WIN32
        if (pid <= 0) return QStringList();
        WinCtx c;
        c.pid = (DWORD)pid;
        EnumWindows(enumWinProc, reinterpret_cast<LPARAM>(&c));
        return c.closed;
#else
        Q_UNUSED(pid)
        return QStringList();
#endif
    }

    // runs the checker on one file; false = no usable answer (output of a good run is returned in *out)
    bool runChecker(const QString &path, QByteArray *out)
    {
        loadSkip();
        if (g_skip.contains(skipKey(path))) {
            g_vstSkipped++;
            logVstLine(path + "  skipped (problem plug-in, listed in vst-skip.txt)");
            return false;
        }

        QProcess process;
        process.start(vstCheckerExe(), QStringList() << path);
        if (!process.waitForStarted(10000)) {
            logVstLine(path + "  checker could not start: " + vstCheckerExe());
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        QStringList closed;
        bool timedOut = false;
        while (process.state() != QProcess::NotRunning && !process.waitForFinished(150)) {
            if (process.state() == QProcess::NotRunning)
                break;
            closed << closeCheckerWindows(process.processId());
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (timer.elapsed() > 45000) {
                timedOut = true;
                process.kill();
                process.waitForFinished(3000);
                break;
            }
        }

        for (const QString &w : closed)
            logVstLine(path + "  closed a window opened by the plug-in: " + w);

        const bool crashed = process.exitStatus() == QProcess::CrashExit;
        if (timedOut) {
            logVstLine(path + "  no answer after 45 s, plug-in skipped from now on");
            addSkip(path);
            return false;
        }
        if (process.exitCode() != 0 || crashed) {
            logVstScan(path, process);
            if (crashed || !closed.isEmpty())
                addSkip(path);       // a plug-in that crashes or nags on every load is not scanned again
            return false;
        }
        *out = process.readAllStandardOutput();
        return true;
    }
}

void Utils::clearVstSkipList()
{
    g_skip.clear();
    g_skipLoaded = true;
    QFile::remove(skipFilePath());
}

void Utils::resetVstScanCounters() { g_vstSkipped = 0; }
int Utils::vstSkippedCount() { return g_vstSkipped; }

bool Utils::is64BitDll(const QString &filePath)
{
#ifndef _WIN32
    Q_UNUSED(filePath);
    return true;
#else
    if (filePath.isEmpty())
        return false;

    QString target = filePath;
    QFileInfo fi(filePath);
    if (fi.isDir()) {
        QString candidate = filePath + "/Contents/x86_64-win/" + fi.fileName();
        if (QFile::exists(candidate))
            target = candidate;
    }

    QFile file(target);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray dosHeader = file.read(64);
    if (dosHeader.size() < 64 || dosHeader.at(0) != 'M' || dosHeader.at(1) != 'Z')
        return false;

    quint32 peOffset = *reinterpret_cast<const quint32 *>(dosHeader.constData() + 0x3C);
    if (!file.seek(peOffset))
        return false;

    QByteArray peHeader = file.read(6);
    if (peHeader.size() < 6 || peHeader.at(0) != 'P' || peHeader.at(1) != 'E' || peHeader.at(2) != '\0' || peHeader.at(3) != '\0')
        return false;

    quint16 machine = *reinterpret_cast<const quint16 *>(peHeader.constData() + 4);
    return (machine == 0x8664 || machine == 0xAA64);
#endif
}

bool Utils::vstInfo(const QString &vstPath, VSTNamePath *info)
{
#ifdef _WIN32
    if (!is64BitDll(vstPath))
        return false;
#endif
    QByteArray raw;
    if (!runChecker(vstPath, &raw))
        return false;

    const QStringList vstStrInfo = QString::fromLocal8Bit(raw).replace("\r\n", "\n").split('\n');
    if (vstStrInfo.count() < 3)
        return false;

    info->uniqueID = vstStrInfo[0].toUInt();
    info->vstName = vstStrInfo[1];
    info->vstvendor = vstStrInfo[2];
    info->vstPath = vstPath;

    return true;
}

bool Utils::vstInfos(const QString &vstPath, QList<VSTNamePath> *infos)
{
#ifdef _WIN32
    if (!is64BitDll(vstPath))
        return false;
#endif
    if (!vstPath.endsWith(".vst3", Qt::CaseInsensitive))
    {
        VSTNamePath info;
        if (!vstInfo(vstPath, &info))
            return false;
        infos->append(info);
        return true;
    }

    QByteArray raw;
    if (!runChecker(vstPath, &raw))
        return false;

    QString strout = QString::fromUtf8(raw);
    QStringList lines = strout.split('\n');

    for (int i = 0; i + 3 < lines.count(); i += 4)
    {
        VSTNamePath info;
        info.uniqueID = lines[i].trimmed().toUInt();
        info.vstName = lines[i + 1].trimmed();
        info.vstvendor = lines[i + 2].trimmed();
        info.vstPath = lines[i + 3].trimmed();
        infos->append(info);
    }

    return !infos->isEmpty();
}

bool Utils::savePlaylist(const QString &filePath, const QList<Song *> &songs)
{
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
        return false;

    QTextStream out(&file);
    out.setCodec("UTF-8");

    out << APP_PLAYLIST_HEADTEXT << endl;
    out << APP_PLAYLIST_VERSION_2 << endl;
    out << APP_PLAYLIST_NAME + QFileInfo(filePath).baseName() << endl;
    out << APP_PLAYLIST_COUNT + QString::number(songs.count()) << endl;

    out << endl;

    out << APP_PLAYLIST_SONGLIST << endl;

    for (int i=0; i<songs.count(); i++)
    {
        Song *s = songs[i];
        out << APP_PLAYLIST_SONG + QString::number(i+1) << endl;
        out << s->id() << endl;
        out << s->name() << endl;
        out << s->artist() << endl;
        out << s->key() << endl;
        out << s->tempo() << endl;
        out << s->bpmSpeed() << endl;
        out << s->transpose() << endl;
        out << s->songType() << endl;
        out << s->lyrics() << endl;
        out << s->path() << endl;
        out << s->cutStartBar() << endl;
        out << s->cutEndBar() << endl;
        out << endl;
    }

    out << APP_PLAYLIST_END_SONGLIST;

    file.close();

    return true;
}

bool Utils::loadPlaylist(const QString &filePath, QList<Song *> &songs)
{
    int version = 0;

    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    QTextStream in(&file);
    in.setCodec("UTF-8");

    const QString head = in.readLine();
    if (head != APP_PLAYLIST_HEADTEXT && head != APP_PLAYLIST_OLD_HEADTEXT)
    {
        file.close();
        return false;
    }

    QString versionString = in.readLine();
    if (versionString == APP_PLAYLIST_VERSION_1)
        version = 1;
    else if (versionString == APP_PLAYLIST_VERSION_2)
        version = 2;
    else
    {
        file.close();
        return false;
    }

    if (in.readLine().indexOf(APP_PLAYLIST_NAME) != 0)
    {
        file.close();
        return false;
    }

    QString countText = in.readLine();
    if (countText.indexOf(APP_PLAYLIST_COUNT) != 0)
    {
        file.close();
        return false;
    }

    int count = countText.replace(APP_PLAYLIST_COUNT, "").toInt();

    in.readLine();

    if (in.readLine() != APP_PLAYLIST_SONGLIST)
    {
        file.close();
        return false;
    }

    bool valid = true;
    QList<Song*> songList;

    for (int i=0; i<count; i++)
    {
        QString songListText = in.readLine();
        if (songListText.indexOf(APP_PLAYLIST_SONG) != 0)
        {
            valid = false;
            break;
        }
        if (songListText.replace(APP_PLAYLIST_SONG, "").toInt() != (i+1))
        {
            valid = false;
            break;
        }

        Song *s = new Song();
        s->setId(in.readLine());
        s->setName(in.readLine());
        s->setArtist(in.readLine());
        s->setKey(in.readLine());
        s->setTempo(in.readLine().toInt());
        s->setBpmSpeed(in.readLine().toInt());
        s->setTranspose(in.readLine().toInt());
        s->setSongType(in.readLine());
        s->setLyrics(in.readLine());
        s->setPath(in.readLine());

        if (version >= 2)
        {
            s->setCutStartBar(in.readLine().toInt());
            s->setCutEndBar(in.readLine().toInt());
        }

        in.readLine();

        songList.append(s);
    }

    if (valid && (in.readLine() != APP_PLAYLIST_END_SONGLIST))
        valid = false;

    file.close();

    if (valid)
    {
        for (Song *s : songs)
            delete s;
        songs.clear();
        songs = songList;
        return true;
    }
    else
    {
        for (Song *s : songList)
            delete s;
        songList.clear();
        return false;
    }
}

QString Utils::LAST_OPEN_DIR = QDir::homePath();
