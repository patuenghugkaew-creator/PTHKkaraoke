#ifndef SOUNDPADS_H
#define SOUNDPADS_H

// Sound pads ("แป้นเสียงเชียร์"): 9 pads (Alt + 1..9, or the on-screen panel). A pad plays either
// a built-in sound, made with the loaded soundfont on its own MIDI stream (so it never touches the
// song's channels), or a sound file of the shop's own (WAV / MP3 / ...). Everything goes into the
// main mixer: it follows the volume, the master limiter and the F12 recording.
// Header-only, no Q_OBJECT (timers use this object as their context).

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <algorithm>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QString>
#include <QTimer>
#include <QVector>
#include <cmath>

#include <bass.h>
#include <bassmidi.h>
#include <bassmix.h>

#include "Config.h"
#include "Midi/MidiSynthesizer.h"

class SoundPads : public QObject
{
public:
    static const int Count = 9;

    enum Builtin { None = 0, Applause, Cheer, Rimshot, DrumRoll, Horn, Whistle, Crash, Triangle, BuiltinCount };

    struct Pad {
        QString name;
        int builtin = None;     // used when file is empty
        QString file;           // the shop's own sound
        double gainDb = 0.0;
    };

    explicit SoundPads(MidiSynthesizer *synth, QObject *parent = nullptr) : QObject(parent), syn(synth)
    {
        clock.start();
        load();
    }
    ~SoundPads() override
    {
        for (DWORD &h : files)
            stopFile(h);
        stopFile(previewStream);
        if (midi)
            BASS_StreamFree(midi);
    }

    static QString builtinName(int b)
    {
        switch (b) {
        case Applause: return QString::fromUtf8("ปรบมือ");
        case Cheer:    return QString::fromUtf8("เชียร์ (ปรบมือ + นกหวีด)");
        case Rimshot:  return QString::fromUtf8("ตบมุก (ตูม-ตูม-ฉึ่ง)");
        case DrumRoll: return QString::fromUtf8("กลองรัว + ฉาบ");
        case Horn:     return QString::fromUtf8("แตรสามช่า");
        case Whistle:  return QString::fromUtf8("นกหวีด");
        case Crash:    return QString::fromUtf8("ฉาบ");
        case Triangle: return QString::fromUtf8("ฉิ่ง");
        default:       return QString::fromUtf8("(ว่าง)");
        }
    }

    // ---- sound banks ("ชุดเสียง"): bank 0 = the shop's own 9 pads ("ตั้งเอง"); the others are
    // folders of sound files (e.g. another karaoke program's SoundFx folders): pad 1..9 = its first
    // 9 files by name, named after the files.
    struct Bank { QString name; QStringList files; };

    int bankCount() const { return banks.size() + 1; }
    int currentBank() const { return bank; }
    QString bankName(int b) const
    {
        return b <= 0 ? QString::fromUtf8("ตั้งเอง") : (b <= banks.size() ? banks[b - 1].name : QString());
    }
    void setBank(int b)
    {
        bank = (b >= 0 && b < bankCount()) ? b : 0;
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        s.setValue("SoundPads/Bank", bank == 0 ? QString() : bankName(bank));
    }
    void nextBank() { setBank((bank + 1) % bankCount()); }
    void setBankSilently(int b) { bank = (b >= 0 && b < bankCount()) ? b : 0; }   // not saved

    // The shop's own sound effects ("เพิ่มเสียงเอฟเฟกต์"): copied into this folder, one sub-folder per
    // bank, next to the program's data (.PTHKKaraoke\SoundFx, or <program>\SoundFx when portable).
    // Always scanned, listed first.
    static QString ownRoot()
    {
        return QDir::cleanPath(QDir(Config::DATABASE_DIR_PATH).absoluteFilePath("../SoundFx"));
    }
    // PTHK's own sound effects, shipped with the program (made by tools/make_soundfx.cpp):
    // <program>\Sounds, one sub-folder per bank, listed as "PTHK › <bank>"
    static QString builtinRoot() { return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/Sounds"); }
    static QString defaultBankName() { return QString::fromUtf8("PTHK › พื้นฐาน"); }
    // read the folders again (after sounds were added / removed)
    void reload() { rescan(); }
    // the bank whose name is this (-1 when there is none)
    int findBank(const QString &name) const
    {
        for (int b = 1; b < bankCount(); ++b)
            if (bankName(b) == name)
                return b;
        return -1;
    }
    static QString ownBankName(const QString &folder) { return QString::fromUtf8("เสียงของร้าน › ") + folder; }
    // the sound files of a folder, in pad order (1, 2, ... 10 by the numbers in their names)
    static QStringList soundFiles(const QString &dir) { return soundsIn(dir); }
    static bool isSoundFile(const QString &path) { return isSound(QFileInfo(path)); }
    // play any sound file once (the sound effects manager's "ลองฟัง"); pressed again: stops
    bool preview(const QString &path)
    {
        if (!syn || !syn->isOpened() || !syn->mainMixerHandle())
            return false;
        if (fileAlive(previewStream)) {
            fadeOut(previewStream);
            if (path == previewPath)
                return true;
        }
        previewPath = path;
        const HSTREAM h = BASS_StreamCreateFile(FALSE, path.utf16(), 0, 0,
                                                BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
        if (!h)
            return false;
        BASS_ChannelSetAttribute(h, BASS_ATTRIB_VOL, dbToVol(padsGain));
        if (!BASS_Mixer_StreamAddChannel(syn->mainMixerHandle(), h, BASS_MIXER_CHAN_NORAMPIN | BASS_STREAM_AUTOFREE)) {
            BASS_StreamFree(h);
            return false;
        }
        previewStream = h;
        return true;
    }
    void stopPreview() { fadeOut(previewStream); }
    // the file preview() is playing now ("" when nothing)
    QString previewPlaying() const { return fileAlive(previewStream) ? previewPath : QString(); }
    // how far the preview has played, 0..1 (-1 when nothing plays)
    double previewProgress() const
    {
        if (!fileAlive(previewStream))
            return -1.0;
        const QWORD len = BASS_ChannelGetLength(previewStream, BASS_POS_BYTE);
        const QWORD pos = BASS_Mixer_ChannelGetPosition(previewStream, BASS_POS_BYTE);
        return (len > 0 && pos != QWORD(-1)) ? qBound(0.0, double(pos) / double(len), 1.0) : 0.0;
    }
    // a sound file's length in seconds (-1 unknown); quick: the file is opened only to read its length
    static double fileSeconds(const QString &path)
    {
        const HSTREAM h = BASS_StreamCreateFile(FALSE, path.utf16(), 0, 0, BASS_STREAM_DECODE | BASS_UNICODE);
        if (!h)
            return -1.0;
        const double s = BASS_ChannelBytes2Seconds(h, BASS_ChannelGetLength(h, BASS_POS_BYTE));
        BASS_StreamFree(h);
        return s;
    }
    // the sound files of a bank (1 .. bankCount()-1), for the sound effects window
    QStringList bankFiles(int b) const { return (b >= 1 && b <= banks.size()) ? banks[b - 1].files : QStringList(); }

    QStringList bankRoots() const { return roots; }
    void setBankRoots(const QStringList &r) { roots = r; rootsAsked = true; rescan(); }
    double bankGainDb() const { return bankGain; }
    void setBankGainDb(double db) { bankGain = db; }

    // folders of sound banks of karaoke programs on this computer: "<a folder with karaoke in its
    // name>\SoundFx" or "\FX", at most two levels below a drive's root
    static QStringList findBankRoots()
    {
        QStringList found;
        auto look = [&found](const QString &dir) {
            for (const char *sub : { "SoundFx", "SoundFX", "FX", "Fx" }) {
                const QString p = QDir(dir).absoluteFilePath(QString::fromLatin1(sub));
                if (QFileInfo(p).isDir() && !found.contains(p, Qt::CaseInsensitive) && !scanBanks(p).isEmpty()) {
                    found << QDir::cleanPath(p);
                    return;
                }
            }
        };
        for (const QFileInfo &drive : QDir::drives()) {
            const QString root = drive.absoluteFilePath();
            if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) != DRIVE_FIXED)
                continue;               // no CD, network or removable drives
            const QFileInfoList top = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &d : top) {
                if (d.fileName().contains("karaoke", Qt::CaseInsensitive))
                    look(d.absoluteFilePath());
                if (d.fileName().startsWith('$') || d.fileName() == "Windows")
                    continue;
                for (const QFileInfo &d2 : QDir(d.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                    if (d2.fileName().contains("karaoke", Qt::CaseInsensitive))
                        look(d2.absoluteFilePath());
            }
        }
        return found;
    }

    Pad pad(int i) const { return (i >= 0 && i < Count) ? pads[i] : Pad(); }
    void setPad(int i, const Pad &p)
    {
        if (i >= 0 && i < Count)
            pads[i] = p;
    }
    // a pad's label for buttons / messages (in the current bank): its name, else what it plays
    QString label(int i) const
    {
        if (bank > 0) {
            const QStringList &f = banks[bank - 1].files;
            return i < f.size() ? fileLabel(f[i]) : QString::fromUtf8("(ว่าง)");
        }
        const Pad p = pad(i);
        if (!p.name.trimmed().isEmpty())
            return p.name.trimmed();
        if (!p.file.isEmpty())
            return fileLabel(p.file);
        return builtinName(p.builtin);
    }
    bool isEmpty(int i) const
    {
        if (bank > 0)
            return i >= banks[bank - 1].files.size();
        const Pad p = pad(i);
        return p.file.isEmpty() && p.builtin == None;
    }

    void load()
    {
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        roots = s.value("SoundPads/BankRoots").toStringList();
        bankGain = qBound(-30.0, s.value("SoundPads/BankGainDb", 0.0).toDouble(), 12.0);
        padsGain = qBound(-30.0, s.value("SoundPads/PadsGainDb", 0.0).toDouble(), 12.0);
        rootsAsked = s.contains("SoundPads/BankRoots");
        rescan();
        // a new installation starts on PTHK's own sounds
        const QString want = s.contains("SoundPads/Bank") ? s.value("SoundPads/Bank").toString() : defaultBankName();
        for (int b = 1; b < bankCount(); ++b)
            if (bankName(b) == want)
                bank = b;
        s.beginGroup("SoundPads");
        const int defaults[Count] = { Applause, Cheer, Rimshot, DrumRoll, Horn, Whistle, Crash, Triangle, None };
        for (int i = 0; i < Count; ++i) {
            const QString k = QString("Pad%1/").arg(i + 1);
            pads[i].name = s.value(k + "Name").toString();
            pads[i].builtin = qBound(0, s.value(k + "Builtin", defaults[i]).toInt(), BuiltinCount - 1);
            pads[i].file = s.value(k + "File").toString();
            pads[i].gainDb = qBound(-30.0, s.value(k + "GainDb", 0.0).toDouble(), 12.0);
        }
        s.endGroup();
    }
    // nothing chosen yet: the karaoke programs' sound folders found on this computer (once)
    bool autoFindRoots()
    {
        if (rootsAsked)
            return false;
        rootsAsked = true;
        roots = findBankRoots();
        rescan();
        save();
        return !roots.isEmpty();
    }

    void save() const
    {
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        s.setValue("SoundPads/BankRoots", roots);
        s.setValue("SoundPads/BankGainDb", bankGain);
        s.setValue("SoundPads/Bank", bank == 0 ? QString() : bankName(bank));
        s.beginGroup("SoundPads");
        for (int i = 0; i < Count; ++i) {
            const QString k = QString("Pad%1/").arg(i + 1);
            s.setValue(k + "Name", pads[i].name);
            s.setValue(k + "Builtin", pads[i].builtin);
            s.setValue(k + "File", pads[i].file);
            s.setValue(k + "GainDb", pads[i].gainDb);
        }
        s.endGroup();
    }

    // GUI thread. False when nothing could be played (no mixer, empty pad, file missing).
    // Pressed while its sound still plays: stops it instead (wasStop() tells which).
    bool trigger(int i)
    {
        played.clear();
        stopped = false;
        if (i < 0 || i >= Count || !syn || !syn->isOpened() || !syn->mainMixerHandle())
            return false;
        if (isPlaying(i)) {
            stop(i);
            stopped = true;
            played = "stopped";
            return true;
        }
        if (bank > 0) {
            const QStringList &f = banks[bank - 1].files;
            return i < f.size() && playFile(i, f[i], bankGain);
        }
        const Pad p = pads[i];
        if (!p.file.isEmpty())
            return playFile(i, p.file, p.gainDb);
        const QString real = realFileFor(p.builtin);
        if (!real.isEmpty() && playFile(i, real, p.gainDb))
            return true;
        if (p.builtin != None)
            return playBuiltin(i, p.builtin, p.gainDb);
        return false;
    }
    bool wasStop() const { return stopped; }

    // a pad's sound still playing
    bool isPlaying(int i) const
    {
        if (i < 0 || i >= Count)
            return false;
        return fileAlive(files[i]) || builtinUntil[i] > clock.elapsed();
    }
    // stop a pad's sound (files fade out quickly; the built-in sounds all stop together)
    void stop(int i)
    {
        if (i < 0 || i >= Count)
            return;
        fadeOut(files[i]);
        if (builtinUntil[i] > clock.elapsed())
            stopBuiltin();
    }
    // every pad's sound (Alt + Backspace, the panel's stop button). False when nothing played.
    bool stopAll()
    {
        bool any = false;
        for (int i = 0; i < Count; ++i)
            if (isPlaying(i))
                any = true;
        if (fileAlive(previewStream))
            any = true;
        for (DWORD &h : files)
            fadeOut(h);
        fadeOut(previewStream);
        stopBuiltin();
        return any;
    }

    // level of all the pads (the panel's slider), on top of each pad's / bank's own level
    double padsGainDb() const { return padsGain; }
    void setPadsGainDb(double db)
    {
        padsGain = qBound(-30.0, db, 12.0);
        for (int i = 0; i < Count; ++i)
            if (fileAlive(files[i]))
                BASS_ChannelSetAttribute(files[i], BASS_ATTRIB_VOL, dbToVol(fileGain[i] + padsGain));
        if (midi)
            BASS_ChannelSetAttribute(midi, BASS_ATTRIB_VOL, dbToVol(padsGain));
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        s.setValue("SoundPads/PadsGainDb", padsGain);
    }

    // what the last trigger() played, for the log: a file path, or "soundfont: <sound>"
    QString lastPlayed() const { return played; }

    // A recorded sound in the banks for a built-in sound the soundfont does badly (a soundfont's
    // "applause" is a hiss loop that sounds like running water). Empty when there is none.
    QString realFileFor(int builtin) const
    {
        QStringList want;
        if (builtin == Applause)
            want << QString::fromUtf8("เสียงปรบมือ") << QString::fromUtf8("ปรบมือ");
        else if (builtin == Cheer)
            want << QString::fromUtf8("ปรบมือ คอนเสิร์ต") << QString::fromUtf8("ปรบมือเชียร์") << QString::fromUtf8("โห่")
                 << QString::fromUtf8("กรี๊ด") << QString::fromUtf8("ปรบมือ");
        for (const QString &w : want)               // the best match first
            for (const Bank &b : banks)
                for (const QString &f : b.files)
                    if (fileLabel(f).contains(w, Qt::CaseInsensitive))
                        return f;
        return QString();
    }

    // "01-เสียงปรบมือ.mp3" -> "เสียงปรบมือ"
    static QString fileLabel(const QString &path)
    {
        QString n = QFileInfo(path).completeBaseName();
        static const QRegularExpression num(QStringLiteral("^\\d+\\s*[-._)]?\\s*"));
        const QString cut = n;
        n.remove(num);
        return n.trimmed().isEmpty() ? cut : n.trimmed();
    }

private:
    static bool isSound(const QFileInfo &f)
    {
        static const QStringList ext = { "wav", "mp3", "ogg", "flac", "m4a", "wma", "aif", "aiff" };
        return f.isFile() && ext.contains(f.suffix().toLower());
    }
    static QStringList soundsIn(const QString &dir)
    {
        QStringList out;
        const QFileInfoList l = QDir(dir).entryInfoList(QDir::Files);
        for (const QFileInfo &f : l)
            if (isSound(f))
                out << f.absoluteFilePath();
        QCollator c;                    // 1, 2, ... 10 (not 1, 10, 2)
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(out.begin(), out.end(), [&c](const QString &a, const QString &b) {
            return c.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });
        return out;
    }
    // the banks under one folder: the folder itself when it has sounds, and each sub-folder that has
    static QVector<Bank> scanBanks(const QString &root)
    {
        QVector<Bank> out;
        const QDir dir(root);
        if (QDir::cleanPath(root).compare(ownRoot(), Qt::CaseInsensitive) == 0) {
            // the shop's own sounds: each sub-folder is a bank
            for (const QFileInfo &d : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
                const QStringList f = soundsIn(d.absoluteFilePath());
                if (!f.isEmpty())
                    out.append({ ownBankName(d.fileName()), f.mid(0, Count) });
            }
            return out;
        }
        // name the banks after the program: "SIBSIB KARAOKE\FX" -> "SIBSIB KARAOKE"
        QString program = dir.dirName();
        if (QDir::cleanPath(root).compare(builtinRoot(), Qt::CaseInsensitive) == 0)
            program = "PTHK";
        else if (QStringList({ "fx", "soundfx", "sounds" }).contains(program.toLower())) {
            QDir up(root);
            if (up.cdUp())
                program = up.dirName();
        }
        const QStringList own = soundsIn(root);
        if (!own.isEmpty())
            out.append({ program, own.mid(0, Count) });
        for (const QFileInfo &d : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
            const QStringList f = soundsIn(d.absoluteFilePath());
            if (!f.isEmpty())
                out.append({ program + QString::fromUtf8(" › ") + d.fileName(), f.mid(0, Count) });
        }
        return out;
    }
    void rescan()
    {
        const QString was = bank > 0 ? bankName(bank) : QString();
        banks.clear();
        QStringList bankRoot;               // the root folder of each bank
        QSet<QString> seen;                 // the same files in two places (a copy on another drive): once
        QStringList all;                    // the shop's own sounds first, then PTHK's, then other folders
        all << ownRoot() << builtinRoot();
        for (const QString &r : roots)
            if (QDir::cleanPath(r).compare(ownRoot(), Qt::CaseInsensitive) != 0
                    && QDir::cleanPath(r).compare(builtinRoot(), Qt::CaseInsensitive) != 0)
                all << r;
        for (const QString &r : all) {
            for (const Bank &b : scanBanks(r)) {
                QString sig;
                for (const QString &f : b.files)
                    sig += QFileInfo(f).fileName().toLower() + ':' + QString::number(QFileInfo(f).size()) + '|';
                if (seen.contains(sig))
                    continue;
                seen.insert(sig);
                banks.append(b);
                bankRoot << r;
            }
        }
        // the same name twice (e.g. one program on drive D: and E:): add the drive
        QVector<bool> dup(banks.size(), false);    // found first, renamed after
        for (int k = 0; k < banks.size(); ++k)
            for (int o = 0; o < banks.size(); ++o)
                if (o != k && banks[o].name == banks[k].name)
                    dup[k] = true;
        for (int k = 0; k < banks.size(); ++k)
            if (dup[k])
                banks[k].name += QString(" (%1)").arg(QDir::toNativeSeparators(bankRoot[k]).left(2));
        bank = 0;
        for (int b = 1; b < bankCount(); ++b)
            if (bankName(b) == was)
                bank = b;
    }

    static float dbToVol(double db) { return float(std::pow(10.0, db / 20.0)); }

    // ---- the shop's own sound files
    static bool fileAlive(DWORD h)
    {
        // a finished file frees itself (auto-free): then it is no longer in the mixer
        return h && BASS_Mixer_ChannelGetMixer(h) != 0 && BASS_ChannelIsActive(h) != BASS_ACTIVE_STOPPED;
    }
    static void stopFile(DWORD &h)
    {
        if (h) {
            BASS_Mixer_ChannelRemove(h);
            BASS_StreamFree(h);         // fails harmlessly when it already ended (auto-freed)
            h = 0;
        }
    }
    // a quick fade (no click), then gone
    void fadeOut(DWORD &h)
    {
        if (!fileAlive(h)) {
            h = 0;
            return;
        }
        const DWORD gone = h;
        h = 0;
        BASS_ChannelSlideAttribute(gone, BASS_ATTRIB_VOL, 0.0f, 250);
        QTimer::singleShot(300, this, [gone]() {
            BASS_Mixer_ChannelRemove(gone);
            BASS_StreamFree(gone);
        });
    }
    bool playFile(int i, const QString &path, double gainDb)
    {
        stopFile(files[i]);
        const HSTREAM h = BASS_StreamCreateFile(FALSE, path.utf16(), 0, 0,
                                                BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
        if (!h) {
            played = QString("file failed to open (BASS error %1): %2").arg(BASS_ErrorGetCode()).arg(path);
            return false;
        }
        fileGain[i] = gainDb;
        BASS_ChannelSetAttribute(h, BASS_ATTRIB_VOL, dbToVol(gainDb + padsGain));
        if (!BASS_Mixer_StreamAddChannel(syn->mainMixerHandle(), h, BASS_MIXER_CHAN_NORAMPIN | BASS_STREAM_AUTOFREE)) {
            played = QString("file not added to the mixer (BASS error %1): %2").arg(BASS_ErrorGetCode()).arg(path);
            BASS_StreamFree(h);
            return false;
        }
        BASS_CHANNELINFO ci;
        BASS_ChannelGetInfo(h, &ci);
        played = QString("file (%1 Hz, %2 ch, %3 s): %4").arg(ci.freq).arg(ci.chans)
                     .arg(BASS_ChannelBytes2Seconds(h, BASS_ChannelGetLength(h, BASS_POS_BYTE)), 0, 'f', 1).arg(path)
                     .prepend(played.isEmpty() ? QString() : played + " | then ");
        files[i] = h;
        return true;
    }

    // ---- built-in sounds: the soundfont, on a MIDI stream of their own
    bool ensureMidi()
    {
        const DWORD mixer = syn->mainMixerHandle();
        if (midi && BASS_Mixer_ChannelGetMixer(midi) != mixer) {   // the synth was opened again
            BASS_StreamFree(midi);
            midi = 0;
        }
        if (!midi) {
            BASS_CHANNELINFO ci;
            const DWORD freq = BASS_ChannelGetInfo(mixer, &ci) ? ci.freq : 44100;
            midi = BASS_MIDI_StreamCreate(16, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT, freq);
            if (!midi)
                return false;
            if (!BASS_Mixer_StreamAddChannel(mixer, midi, BASS_MIXER_CHAN_NORAMPIN)) {
                BASS_StreamFree(midi);
                midi = 0;
                return false;
            }
            BASS_ChannelSetAttribute(midi, BASS_ATTRIB_VOL, dbToVol(padsGain));
            BASS_MIDI_StreamEvent(midi, 0, MIDI_EVENT_PROGRAM, 126);   // ch 1: applause
            BASS_MIDI_StreamEvent(midi, 1, MIDI_EVENT_PROGRAM, 61);    // ch 2: brass section
        }
        // the soundfonts may have changed since: always the current ones, the default first
        // (a sound the default lacks is taken from the next one that has it)
        const QList<HSOUNDFONT> sf = syn->soundfonts();
        if (sf.isEmpty())
            return false;
        QVector<BASS_MIDI_FONT> fonts;
        for (HSOUNDFONT h : sf) {
            BASS_MIDI_FONT f;
            f.font = h;
            f.preset = -1;
            f.bank = 0;
            fonts.append(f);
        }
        BASS_MIDI_StreamSetFonts(midi, fonts.constData(), DWORD(fonts.size()));
        return true;
    }

    void noteOn(int ch, int note, int vel)
    {
        BASS_MIDI_StreamEvent(midi, DWORD(ch), MIDI_EVENT_NOTE, MAKEWORD(note, qBound(1, vel, 127)));
    }
    void noteOff(int ch, int note)
    {
        BASS_MIDI_StreamEvent(midi, DWORD(ch), MIDI_EVENT_NOTE, MAKEWORD(note, 0));
    }
    // a note at `atMs`, released `lenMs` later
    void hit(int atMs, int ch, int note, int vel, int lenMs)
    {
        const int v = qBound(1, int(vel * velScale + 0.5), 127);
        const unsigned g = gen;
        auto on = [this, ch, note, v, g]() { if (midi && g == gen) noteOn(ch, note, v); };   // not after a stop
        auto off = [this, ch, note]() { if (midi) noteOff(ch, note); };
        if (atMs <= 0)
            on();
        else
            QTimer::singleShot(atMs, Qt::PreciseTimer, this, on);
        QTimer::singleShot(atMs + lenMs, Qt::PreciseTimer, this, off);
        seqEnd = (std::max)(seqEnd, atMs + lenMs);
    }

    // all the built-in sounds: the notes still to come are dropped, the ringing ones silenced
    void stopBuiltin()
    {
        ++gen;
        for (qint64 &u : builtinUntil)
            u = 0;
        if (midi)
            for (int ch = 0; ch < 16; ++ch)
                BASS_MIDI_StreamEvent(midi, DWORD(ch), MIDI_EVENT_SOUNDOFF, 0);
    }

    bool playBuiltin(int i, int b, double gainDb)
    {
        if (!ensureMidi()) {
            played += QString(" | soundfont: no MIDI stream / soundfont");
            return false;
        }
        played += (played.isEmpty() ? QString() : QString(" | then ")) + "soundfont: " + builtinName(b);
        velScale = std::pow(10.0, gainDb / 20.0);
        seqEnd = 0;
        struct Until { SoundPads *s; int i; ~Until() { s->builtinUntil[i] = s->clock.elapsed() + s->seqEnd; } } until{ this, i };
        const int D = 9;        // drum channel
        switch (b) {
        case Applause:
            hit(0, 0, 60, 110, 3500);
            break;
        case Cheer:
            hit(0, 0, 60, 115, 3500);
            hit(0, 0, 64, 95, 3000);
            hit(150, D, 71, 105, 200);      // short whistle, short whistle, long whistle
            hit(450, D, 71, 105, 200);
            hit(800, D, 72, 115, 600);
            break;
        case Rimshot:
            hit(0, D, 50, 110, 150);        // high tom, low tom, crash + kick
            hit(200, D, 45, 112, 150);
            hit(480, D, 49, 120, 1500);
            hit(480, D, 36, 118, 200);
            break;
        case DrumRoll: {
            const int len = 1500, step = 62;
            for (int t = 0; t < len; t += step)
                hit(t, D, 38, 45 + (70 * t) / len, 60);   // snare, getting louder
            hit(len + 40, D, 49, 122, 1500);
            hit(len + 40, D, 36, 118, 200);
            break;
        }
        case Horn: {                        // three short brass hits, the last one held
            const int chord[3] = { 60, 64, 67 };
            for (int k = 0; k < 3; ++k) {
                const int at = k * 260;
                const int len = (k == 2) ? 700 : 150;
                for (int n : chord)
                    hit(at, 1, n, 112, len);
            }
            break;
        }
        case Whistle:
            hit(0, D, 71, 110, 200);
            hit(260, D, 71, 110, 200);
            hit(520, D, 72, 118, 700);
            break;
        case Crash:
            hit(0, D, 49, 122, 1500);
            hit(0, D, 36, 118, 200);
            break;
        case Triangle:                      // ฉิ่ง-ฉับ: open, muted, open
            hit(0, D, 81, 110, 600);
            hit(300, D, 80, 105, 200);
            hit(600, D, 81, 115, 900);
            break;
        default:
            return false;
        }
        return true;
    }

    MidiSynthesizer *syn = nullptr;
    Pad pads[Count];
    QStringList roots;
    QVector<Bank> banks;
    int bank = 0;
    double bankGain = 0.0;
    bool rootsAsked = false;
    DWORD files[Count] = {};
    HSTREAM midi = 0;
    double velScale = 1.0;
    QString played;
    bool stopped = false;
    double padsGain = 0.0;
    double fileGain[Count] = {};
    qint64 builtinUntil[Count] = {};    // clock ms when a pad's built-in sound is over
    int seqEnd = 0;                     // length (ms) of the built-in sound being started
    unsigned gen = 0;                   // bumped by a stop: notes still to come are dropped
    QElapsedTimer clock;
    DWORD previewStream = 0;
    QString previewPath;
};

#endif // SOUNDPADS_H
