#ifndef UTILS_H
#define UTILS_H

#include <QFile>
#include <QMenu>
#include <QSignalMapper>

#include "Song.h"
#include "Midi/MidiSynthesizer.h"

#ifdef _WIN32
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
#endif

class Utils
{
public:
    Utils();

    static QVector<long> readCurFile(const QString &curFile, uint32_t resolution);
    static QVector<long> readCurFile(const QByteArray &data, uint32_t resolution);

    // Thai text files are TIS-620 (old) or UTF-8 (newer song packs): picks the right one
    static QString decodeThaiText(const QByteArray &data);
    static QString readLyrics(const QString &lyrFile);
    static QString readLyrics(const QByteArray &data);

    static uint concurentThreadsSupported();

    // PE architecture check: returns true if the binary is 64-bit (x64 / ARM64)
    static bool is64BitDll(const QString &filePath);

    static bool vstInfo(const QString &vstPath, VSTNamePath *info);
    // VST2 file -> 1 entry, VST3 file -> 1 entry per audio class
    static bool vstInfos(const QString &vstPath, QList<VSTNamePath> *infos);
    // scan safety net: plug-ins that hang / crash / nag are skipped on later scans
    static void clearVstSkipList();
    static void resetVstScanCounters();
    static int  vstSkippedCount();

    static bool savePlaylist(const QString &filePath, const QList<Song*> &songs);
    static bool loadPlaylist(const QString &filePath, QList<Song*> &songs);

    static QString LAST_OPEN_DIR;
};

#endif // UTILS_H
