// The song file types PTHK plays, in one place: the "+ เพิ่มเพลง" file picker, adding songs (files,
// folders, drag and drop), the library scan and the idle music folder all use these lists, so a type
// the program plays can always be added, and a type it can not play is never offered.
// Checked on this program's players (2026-11-12):
//  - sound (BASS, with Windows Media Foundation): mp3 wav flac m4a aac ogg wma play; opus does not
//  - video picture (Qt + Media Foundation): mp4 mkv avi mpg mpeg wmv vob dat mov m4v ts play;
//    webm and flv do not (no picture / no sound)
#ifndef SONGFORMATS_H
#define SONGFORMATS_H

#include <QObject>
#include <QString>
#include <QStringList>

namespace SongFormats {

inline const QStringList &videoExts()
{
    static const QStringList l = { "mp4", "mkv", "avi", "mpg", "mpeg", "wmv", "vob", "dat", "mov", "m4v", "ts" };
    return l;
}
inline const QStringList &audioExts()       // added as type "MP3" (played by BASS)
{
    static const QStringList l = { "mp3", "wav", "flac", "m4a", "aac", "ogg", "wma" };
    return l;
}
inline const QStringList &midiExts()        // a .mid with its .lyr and .cur is an NCN song, else KAR
{
    static const QStringList l = { "mid", "midi", "kar" };
    return l;
}
inline const QStringList &archiveExts()
{
    static const QStringList l = { "zip", "rar", "7z" };
    return l;
}
// files that come along with a song (NCN lyrics / cursor, MP3 lyrics): not songs of their own
inline const QStringList &partExts()
{
    static const QStringList l = { "lyr", "cur", "lrc" };
    return l;
}

// the library type a file is added as ("" = not a song file)
inline QString typeFor(const QString &suffix)
{
    const QString e = suffix.toLower();
    if (videoExts().contains(e)) return QStringLiteral("VIDEO");
    if (audioExts().contains(e)) return QStringLiteral("MP3");
    if (midiExts().contains(e))  return QStringLiteral("KAR");
    if (e == "emk") return QStringLiteral("EMK");
    if (e == "sib") return QStringLiteral("SIB");
    if (e == "hnk") return QStringLiteral("HNK");
    return QString();
}

// worth taking from a folder: a song, a song part, or an archive that may hold songs
inline bool isSongFile(const QString &suffix)
{
    const QString e = suffix.toLower();
    return !typeFor(e).isEmpty() || partExts().contains(e) || archiveExts().contains(e);
}

// "*.mp4 *.MP4 ..." for QDir / QDirIterator name filters
inline QStringList globs(const QStringList &exts)
{
    QStringList f;
    for (const QString &e : exts)
        f << "*." + e << "*." + e.toUpper();
    return f;
}

// the "+ เพิ่มเพลง" file picker: everything that plays first, then one entry per kind
inline QString dialogFilter()
{
    auto pat = [](const QStringList &exts) {
        QStringList p;
        for (const QString &e : exts)
            p << "*." + e;
        return p.join(' ');
    };
    const QStringList karaoke = { "mid", "midi", "lyr", "cur", "kar", "emk", "sib", "hnk" };
    const QStringList all = karaoke + videoExts() + audioExts() + archiveExts();
    return QStringList({
        QObject::tr("ทุกชนิดที่โปรแกรมเล่นได้ (%1)").arg(pat(all)),
        QObject::tr("เพลง NCN - เลือก .mid .lyr .cur มาด้วยกัน (*.mid *.midi *.lyr *.cur)"),
        QObject::tr("เพลง EMK (*.emk)"),
        QObject::tr("เพลง SIB (*.sib)"),
        QObject::tr("เพลง HNK (*.hnk)"),
        QObject::tr("เพลง MIDI / KAR (*.mid *.midi *.kar)"),
        QObject::tr("วิดีโอคาราโอเกะ (%1)").arg(pat(videoExts())),
        QObject::tr("เพลงเสียง MP3 / WAV / FLAC / M4A ... (%1)").arg(pat(audioExts())),
        QObject::tr("ไฟล์บีบอัดที่มีเพลง (%1)").arg(pat(archiveExts())),
    }).join(";;");
}

}

#endif // SONGFORMATS_H
