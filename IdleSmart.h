// IdleSmart.h: smart idle music ("เพลงคลอฉลาด", IdleMusic/Source 4).
//
// Picks background songs for an empty queue: what is popular now, every style ("auto", the owner's
// choice). History of the design: YouTube's "Mix" after a customer's song gave live clips of the
// same band; the customers' singers' hits were old songs; the owner wants songs in fashion now.
//  - YouTube's weekly chart "Top 100 Songs Thailand" (string / pop; read once a day, idle-smart.json)
//  - YouTube searches "ลูกทุ่ง" / "หมอลำ" / "เพื่อชีวิต" uploaded this year and "เพลงลูกทุ่งใหม่" this
//    month, sorted by views (trendSources); no live sessions / covers / compilations
//  - "hits": the chart only; a style the shop picked (slow, fast, a genre of the YouTube page): a search
// The customers still count: their songs are not repeated, and Thai-only when they sing Thai.
// YouTube is read with yt-dlp (no API key), in the background. Each clip is looked up in the song
// library on a worker thread (song name and artist must both agree): a library song plays from its
// file, any other from YouTube (online, not downloaded).
//
// Settings: IdleMusic/Style  "auto" (default), "hits", "slow", "fast" or a genre text.

#ifndef IDLESMART_H
#define IDLESMART_H

#include "Song.h"
#include "Dialogs/YouTube.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <functional>
#include <memory>

struct IdlePick {
    bool local = false;     // a library song (song), else a YouTube clip (yt)
    Song song;
    YtItem yt;
    QString libKey;         // songtype + path of the library song ("" if none)
    QString contextTag;     // human-readable 4D context label (genre • era • tempo • type)
};

class IdleSmart
{
public:
    struct Seed {
        QString name;
        QString artist;
        QString ytId;
        QString key;        // songtype + path
        QString songType;   // NCN, EMK, SIB, HNK, KAR, VIDEO, MP3, YOUTUBE
        QString lyrics;     // snippet of lyrics (for dialect / mood detection)
        int tempo = 0;      // BPM (0 when unknown)
    };

    enum Genre {
        GenreUnknown = 0,
        GenreLukThung,      // ลูกทุ่ง
        GenreMorLam,        // หมอลำ / อีสาน
        GenrePhueaChiwit,   // เพื่อชีวิต
        GenreLukKrung,      // ลูกกรุง / สุนทราภรณ์
        GenreString,        // สตริง / ป็อป / ร็อก
        GenreInternational  // สากล
    };

    enum Era {
        EraUnknown = 0,
        EraClassic80s,      // ยุคคลาสสิก / 80s
        Era90s,             // ยุค 90
        Era2000s,           // ยุค 2000
        Era2010s,           // ยุค 2010
        EraModern           // ยุคใหม่
    };

    enum TempoBand {
        TempoAny = 0,
        TempoSlow,          // < 88 BPM (ช้า / ซึ้ง)
        TempoMedium,        // 88..118 BPM (จังหวะกลาง)
        TempoFast           // >= 119 BPM (เร็ว / สนุก / สามช่า / ลำซิ่ง)
    };

    enum MediaKind {
        MediaAny = 0,
        MediaMidi,          // NCN, EMK, SIB, HNK, KAR
        MediaLocalAV,       // VIDEO, MP3
        MediaYouTube        // YOUTUBE
    };

    struct ContextProfile {
        bool hasSeed = false;
        Genre genre = GenreUnknown;
        Era era = EraUnknown;
        TempoBand tempoBand = TempoAny;
        int targetBpm = 0;
        MediaKind mediaKind = MediaAny;
        QString primarySongType;
        QString lastArtist;
        QString lastSongName;
        QString lastYtId;
        int lang = 0;       // 0 = any, 1 = Thai, 2 = International
    };

    // set by MainWindow
    std::function<QVector<Seed>()> seeds;               // tonight's songs, else this hour's (newest / most first)
    std::function<void(const QString &)> log;
    QString dbFile;                                     // the song database, for the worker's own connection

    explicit IdleSmart(QObject *owner) : ctx(owner), alive(std::make_shared<int>(1))
    {
        loadCache();
    }

    // the YouTube page's styles + ours, for the settings
    static QStringList styleNames()
    {
        return { QObject::tr("อัตโนมัติ: คลอตามบริบทเพลงล่าสุด (ชนิดเพลง • ความเร็ว • แนวเพลง • ยุคสมัย)"),
                 QObject::tr("เพลงฮิตสตริง / ป็อป (อันดับ YouTube ไทย)"),
                 QObject::tr("เพลงช้า ซึ้ง ๆ"), QObject::tr("เพลงเร็ว สนุก ๆ") };
    }
    static QStringList styleKeys() { return { "auto", "hits", "slow", "fast" }; }

    void setStyle(const QString &s)
    {
        if (s == style)
            return;
        style = s;
        ready.clear();      // the songs prepared for the old style are not wanted now
        stale = true;
    }

    // A customer song started or changed tempo: immediately adapt context and purge mismatched ready songs
    void customerSang()
    {
        stale = true;
        contextDirty = true;
        lastRefill = 0;     // allow immediate refill for the new song context
        if (seeds) {
            const QVector<Seed> sd = seeds();
            const ContextProfile cp = buildContext(sd);
            currentCtx = cp;
            filterReadyForContext(cp);
        }
    }

    QString contextDescription() const
    {
        return formatContextTag(currentCtx);
    }

    // Starts fetching when the prepared list is short or old (cheap to call every second).
    void refill()
    {
        if (busy)
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!contextDirty && now - lastRefill < (ready.size() < 3 ? 15000 : 90000))
            return;                                     // not too often unless customer song context changed
        if (ready.size() >= 8 && !stale && !contextDirty)
            return;
        lastRefill = now;
        stale = false;
        contextDirty = false;
        busy = true;
        startRefill();
    }

    // The next song to play; false when none is ready (the caller plays a library song instead).
    // fileOk: does the library song's file exist (MainWindow::songFilePath).
    // allowYouTube false (no internet): library songs only; the YouTube ones stay for later.
    bool next(IdlePick *out, const std::function<bool(Song)> &fileOk, bool allowYouTube = true)
    {
        QSet<QString> sungTitles;       // the customers' songs of tonight (another copy is the same song)
        if (seeds) {
            const QVector<Seed> sd = seeds();
            for (const Seed &x : sd) {
                const QString k = titleKey(x.name);
                if (!k.isEmpty())
                    sungTitles.insert(k);
            }
            currentCtx = buildContext(sd);
            filterReadyForContext(currentCtx);
        }
        auto sameAsSung = [&sungTitles](const IdlePick &p) {
            return titleClash(pickTitle(p), !p.local, sungTitles);
        };
        if (!allowYouTube) {
            for (int i = 0; i < ready.size(); ++i) {
                IdlePick p = ready.at(i);
                if (!p.local)
                    continue;
                ready.removeAt(i--);
                if (wasPlayed(p) || sameAsSung(p) || !fileOk(p.song))
                    continue;
                if (p.contextTag.isEmpty())
                    p.contextTag = formatContextTag(currentCtx);
                markPlayed(p);
                *out = p;
                return true;
            }
            return false;
        }
        // other singers than the one the customer just sang and the last idle songs
        QStringList avoidArtists = recentIdleArtists;
        if (!currentCtx.lastArtist.trimmed().isEmpty())
            avoidArtists << norm(currentCtx.lastArtist);
        while (!ready.isEmpty()) {
            // among the best few, the first two by other singers (random of them), else the best
            QVector<int> fresh;
            for (int j = 0; j < qMin(8, ready.size()) && fresh.size() < 2; ++j)
                if (!artistClash(ready.at(j), avoidArtists))
                    fresh << j;
            const int i = fresh.isEmpty() ? QRandomGenerator::global()->bounded(qMin(2, ready.size()))
                                          : fresh.at(QRandomGenerator::global()->bounded(fresh.size()));
            IdlePick p = ready.takeAt(i);
            if (wasPlayed(p) || sameAsSung(p))
                continue;
            if (p.local && !fileOk(p.song)) {
                if (p.yt.id.isEmpty())
                    continue;
                p.local = false;                        // the file is gone: from YouTube
            }
            if (p.contextTag.isEmpty())
                p.contextTag = formatContextTag(currentCtx);
            markPlayed(p);
            if (p.local) {
                Song so = p.song;
                const QString a = norm(so.artist());
                if (a.size() >= 3) {
                    recentIdleArtists.prepend(a);
                    while (recentIdleArtists.size() > 3)
                        recentIdleArtists.removeLast();
                }
            }
            *out = p;
            return true;
        }
        return false;
    }

    // the song is by one of `avoid` (normalized artist names): a library song by its artist field,
    // a YouTube clip when its title holds the name
    static bool artistClash(const IdlePick &p, const QStringList &avoid)
    {
        QString a;
        if (p.local) {
            Song so = p.song;
            a = norm(so.artist());
        }
        const QString t = p.local ? QString() : norm(p.yt.title);
        for (const QString &x : avoid) {
            if (x.size() < 3)
                continue;
            if (p.local ? (!a.isEmpty() && (a.contains(x) || x.contains(a))) : t.contains(x))
                return true;
        }
        return false;
    }

    // a clip YouTube does not let us play here (embedding off, removed): never again
    void markBad(const QString &id)
    {
        if (id.isEmpty() || bad.contains(id))
            return;
        bad.insert(id);
        saveCache();
    }

    int readyCount() const { return ready.size(); }
    QStringList recentIdleArtists;      // normalized singers of the last 3 idle songs

    // A song's title without what tells copies apart: "จักรยานสีแดง ( A )", "จักรยานสีแดง (Loso Special)"
    // and "จักรยานสีแดง" are one song. Brackets and their text, "karaoke" words, spaces and punctuation
    // go; letters, digits and Thai marks stay. "" when too short to compare.
    static QString titleKey(const QString &name)
    {
        QString t = name.toLower();
        static const QRegularExpression brackets(QStringLiteral("[\\(\\[\\{][^\\)\\]\\}]*[\\)\\]\\}]"));
        t.remove(brackets);
        for (const char *w : { "karaoke", "คาราโอเกะ", "official", "mv", "lyrics", "audio", "cover" })
            t.remove(QString::fromUtf8(w));
        QString o;
        for (const QChar c : t)
            if (c.isLetterOrNumber() || c.isMark())
                o += c;
        return o.size() >= 3 ? o : QString();
    }
    static QString pickTitle(const IdlePick &p)
    {
        Song s = p.song;          // Song::name() is not const
        return p.local ? s.name() : p.yt.title;
    }
    // the same song as one in `avoid`: the same title key, or (a YouTube clip title, which also holds
    // the singer and extras) a title that contains one of them
    static bool titleClash(const QString &name, bool clipTitle, const QSet<QString> &avoid)
    {
        const QString k = titleKey(name);
        if (k.isEmpty())
            return false;
        if (avoid.contains(k))
            return true;
        if (clipTitle)
            for (const QString &a : avoid)
                if (a.size() >= 4 && k.contains(a))
                    return true;
        return false;
    }

    // Format a concise Thai summary of the 4D context (genre • era • tempo • type)
    static QString formatContextTag(const ContextProfile &cp)
    {
        if (!cp.hasSeed)
            return QString();
        QStringList parts;
        switch (cp.genre) {
        case GenreLukThung:      parts << QString::fromUtf8("ลูกทุ่ง"); break;
        case GenreMorLam:        parts << QString::fromUtf8("หมอลำ/อีสาน"); break;
        case GenrePhueaChiwit:   parts << QString::fromUtf8("เพื่อชีวิต"); break;
        case GenreLukKrung:      parts << QString::fromUtf8("ลูกกรุง"); break;
        case GenreString:        parts << QString::fromUtf8("สตริง/ป็อป"); break;
        case GenreInternational: parts << QString::fromUtf8("สากล"); break;
        default: break;
        }
        switch (cp.era) {
        case EraClassic80s: parts << QString::fromUtf8("ยุคคลาสสิก/80s"); break;
        case Era90s:        parts << QString::fromUtf8("ยุค 90"); break;
        case Era2000s:      parts << QString::fromUtf8("ยุค 2000"); break;
        case Era2010s:      parts << QString::fromUtf8("ยุค 2010"); break;
        case EraModern:     parts << QString::fromUtf8("ยุคใหม่"); break;
        default: break;
        }
        if (cp.targetBpm > 0) {
            const QString band = cp.tempoBand == TempoSlow ? QString::fromUtf8("เพลงช้า")
                               : cp.tempoBand == TempoFast ? QString::fromUtf8("เพลงเร็ว")
                                                           : QString::fromUtf8("จังหวะกลาง");
            parts << QString("%1 ~%2 BPM").arg(band).arg(cp.targetBpm);
        } else {
            switch (cp.tempoBand) {
            case TempoSlow:   parts << QString::fromUtf8("เพลงช้า"); break;
            case TempoMedium: parts << QString::fromUtf8("จังหวะกลาง"); break;
            case TempoFast:   parts << QString::fromUtf8("เพลงเร็ว"); break;
            default: break;
            }
        }
        if (!cp.primarySongType.isEmpty())
            parts << cp.primarySongType;
        return parts.join(QString::fromUtf8(" • "));
    }

private:
    QObject *ctx;
    std::shared_ptr<int> alive;         // the worker's result is dropped once this object is gone
    QString style = "auto";
    ContextProfile currentCtx;
    bool contextDirty = true;
    QVector<IdlePick> ready;
    QVector<YtItem> chart;
    qint64 chartTime = 0;
    QHash<QString, QVector<YtItem>> searches;   // style query -> clips (kept 12 h)
    QHash<QString, qint64> searchTime;
    QHash<QString, qint64> views;               // YouTube id -> view count (from the searches)
    QHash<QString, qint64> played;              // key -> when played as idle music
    QSet<QString> bad;
    bool busy = false;
    bool stale = true;
    qint64 lastRefill = 0;
    int seedTurn = 0;

    static QString ytdlp() { return YtTools::ytdlpPath(); }
    static QString cacheFile() { return QFileInfo(YtTools::toolsDir()).absolutePath() + "/idle-smart.json"; }

    void say(const QString &t) { if (log) log("idle smart: " + t); }

    // ---- 4-Dimensional Context Classification (SongType, Tempo/BPM, Genre, Era) --------------------
    static MediaKind classifyMediaKind(const QString &songType)
    {
        const QString t = songType.toUpper();
        if (t == "YOUTUBE")
            return MediaYouTube;
        if (t == "VIDEO" || t == "MP3")
            return MediaLocalAV;
        if (t == "NCN" || t == "EMK" || t == "SIB" || t == "HNK" || t == "KAR")
            return MediaMidi;
        return MediaAny;
    }

    static TempoBand classifyTempoBand(int bpm, const QString &text = QString())
    {
        if (bpm > 0) {
            if (bpm < 88)  return TempoSlow;
            if (bpm <= 118) return TempoMedium;
            return TempoFast;
        }
        static const QRegularExpression reFast(
            QString::fromUtf8("สามช่า|3\\s*ช่า|ลำซิ่ง|โจ๊ะ|แดนซ์|dance|มันส์|รำวง|เซิ้ง|รถแห่|สายย่อ|ยกล้อ|ตื๊ด|เพลงเร็ว|สนุก|ชะชะช่า|ตะลุง|หมอลำซิ่ง|ร็อกมันส์"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reSlow(
            QString::fromUtf8("เพลงช้า|ซึ้ง|อกหัก|เศร้า|คิดถึง|น้ำตา|ร้องไห้|เหงา|ใจพัง|ปวดใจ|ขอโทษ|หลับตา|ลา|คืนนี้|acoustic|อะคูสติก|ballad"),
            QRegularExpression::CaseInsensitiveOption);
        if (reFast.match(text).hasMatch())
            return TempoFast;
        if (reSlow.match(text).hasMatch())
            return TempoSlow;
        return TempoAny;
    }

    struct ArtistMeta {
        const char *pattern;
        Genre genre;
        Era era;
    };

    static const QVector<ArtistMeta> &artistCatalog()
    {
        static const QVector<ArtistMeta> cat = {
            // ลูกกรุง / สุนทราภรณ์ / ย้อนยุคคลาสสิก (Classic / 70s-80s)
            { "สุนทราภรณ์|เอื้อ สุนทรสนาน|ชรินทร์|สุเทพ วงศ์กำแหง|ธานินทร์|เศรษฐา|สวลี|รวงทอง|เพ็ญศรี|บุษยา|มัณฑนา|วินัย จุลละบุษปะ", GenreLukKrung, EraClassic80s },
            { "ดาวใจ|จินตนา สุขสถิตย์|ทิพย์วรรณ|ศรีไศล|กุ้ง กิตติคุณ|อรวี|หยาด นภาลัย|ดอน สอนระเบียบ|นพ พลากร|ปั่น ไพบูลย์เกียรติ|แจ้ ดนุพล", GenreLukKrung, EraClassic80s },
            { "แกรนด์เอ็กซ์|ดิอิมพอสซิเบิ้ล|ชาตรี|รอยัลสไปรท์|คีรีบูน|ฟรุ๊ตตี้|เรนโบว์|บรั่นดี|ต้อม เรนโบว์|อ๊อด คีรีบูน|ชมพู ฟรุ๊ตตี้|สาว สาว สาว", GenreString, EraClassic80s },

            // ลูกทุ่งคลาสสิก (Classic / 80s)
            { "สุรพล สมบัติเจริญ|ไวพจน์|ชาย เมืองสิงห์|ศรคีรี|ทูล ทองใจ|ก้าน แก้วสุพรรณ|ผ่องศรี|บุปผา สายชล|ระพิน ภูไท|เพลิน พรหมแดน", GenreLukThung, EraClassic80s },
            { "สายัณห์ สัญญา|ยอดรัก สลักใจ|พุ่มพวง ดวงจันทร์|ศรเพชร ศรสุพรรณ|เสรี รุ่งสว่าง|สันติ ดวงสว่าง|พัชรา แวงวรรณ|สุนารี ราชสีมา", GenreLukThung, EraClassic80s },

            // ลูกทุ่งยุค 90 - 2000 - 2010 - ใหม่
            { "ก๊อต จักรพรรณ์|จักรพรรณ์ อาบครบุรี|ไมค์ ภิรมย์พร|ยิ่งยง ยอดบัวงาม|เอกราช สุวรรณภูมิ|มนต์สิทธิ์ คำสร้อย|รุ่ง สุริยา|อาภาพร|ฮาย อาภาพร|ยุ้ย ญาติเยอะ|ลูกนก สุภาพร|คัฑลียา มารศรี|ทศพล หิมพานต์|อ๊อด โฟร์เอส", GenreLukThung, Era90s },
            { "ต่าย อรทัย|ไผ่ พงศธร|ตั๊กแตน ชลดา|กุ้ง สุทธิราช|แคท รัตติกาล|ดวงจันทร์ สุวรรณี|การะเกด|บ่าววี|หลวงไก่|พจน์ สุวรรณพันธ์|อาร์สยาม|ใบเตย อาร์สยาม|กระแต อาร์สยาม", GenreLukThung, Era2000s },
            { "หญิงลี|จ๊ะ อาร์สยาม|จ๊ะ นงผณี|เปาวลี|ก้อง ห้วยไร่|เบิ้ล ปทุมราช|แซ็ค ชุมแพ|ตรี ชัยณรงค์|เต๊ะ ตระกูลตอ", GenreLukThung, Era2010s },
            { "เบียร์ พร้อมพงษ์|ลำเพลิน วงศกร|มีนตรา|ทิดแอม|เล็ก รัชเมศฐ์|ไรอัล กาจบัณฑิต|ซัน วงศธร|ฟอร์ม ชลพิพรรธน์", GenreLukThung, EraModern },

            // หมอลำ / ลูกทุ่งอีสาน (Classic / 90s / 2000s / 2010s / Modern)
            { "พรศักดิ์ ส่องแสง|บานเย็น รากแก่น|เฉลิมพล มาลาคำ|หงษ์ทอง ดาวอุดร|เดือนเพ็ญ อำนวยพร|สมจิตร บ่อทอง|ศักดิ์ ภูเวียง", GenreMorLam, Era90s },
            { "จินตหรา พูนลาภ|ศิริพร อำไพพงษ์|ลูกแพร|ไหมไทย|มนต์แคน แก่นคูน|แมน มณีวรรณ|เอม อภัสรา|ดอกอ้อ ทุ่งทอง|ก้านตอง ทุ่งเงิน|เสียงอีสาน|นกน้อย อุไรพร", GenreMorLam, Era2000s },
            { "ลำไย ไหทองคำ|เพชร สหรัตน์|เต้ย อธิบดินทร์|ศาล สานศิลป์|ปรีชา ปัดภัย|เนสกาแฟ ศรีนคร|กระต่าย พรรณนิภา|อาม ชุติมา|ธัญญ่า อาร์สยาม", GenreMorLam, Era2010s },
            { "เวียง นฤมล|แอน อรดี|บอย พนมไพร|เน็ค นฤพล|อุ๋งอิ๋ง|สาวน้อยเพชรบ้านแพง|บิว จิตรฉรีญา|ระเบียบวาทะศิลป์|ประถมบันเทิงศิลป์|จ่อย รวมมิตร|เจมส์ จตุรงค์", GenreMorLam, EraModern },

            // เพื่อชีวิต (80s / 90s / 2000s / 2010s)
            { "คาราวาน|หงา คาราวาน|มงคล อุทก|คนด่านเกวียน|ซูซู|แฮมเมอร์|โฮป", GenrePhueaChiwit, EraClassic80s },
            { "คาราบาว|แอ๊ด คาราบาว|เล็ก คาราบาว|เทียรี่|พงษ์สิทธิ์ คำภีร์|ปู พงษ์สิทธิ์|พงษ์เทพ กระโดนชำนาญ|หมู พงษ์เทพ|ฤทธิพร|อินคา", GenrePhueaChiwit, Era90s },
            { "มาลีฮวนน่า|คฑาวุธ ทองไทย|แช่ม แช่มรัมย์|พี สะเดิด|แสน นากา|เดวิด อินธี|กล้วย แสตมป์|จ๊อบ บรรจบ", GenrePhueaChiwit, Era2000s },
            { "วงพัทลุง|มหาหิงค์|ไปส่งกู บขส|อ้อย กะท้อน|สหาย|โจอี้ ภูวศิษฐ์", GenrePhueaChiwit, Era2010s },

            // สตริง / ป็อป / ร็อก ยุค 90
            { "ไมโคร|อำพล ลำพูน|อัสนี|วสันต์|หิน เหล็ก ไฟ|เสือ ธนพล|โลโซ|เสก โลโซ|หรั่ง ร็อกเคสตร้า|อิทธิ พลางกูร|ไฮร็อก|เป้ ไฮร็อก|ฟลาย|แบล็คเฮด|สไมล์บัฟฟาโล่|ออดี้|วายน็อตเซเว่น|โมเดิร์นด็อก|พราว", GenreString, Era90s },
            { "เบิร์ด ธงไชย|ธงไชย แมคอินไตย์|ใหม่ เจริญปุระ|คริสติน่า|เจ เจตริน|มอส ปฏิภาณ|เต๋า สมชาย|ทาทา ยัง|แอม เสาวลักษณ์|โบ สุนิตา|นัท มีเรีย|มาช่า|แอน ธิติมา|นิโคล|กบ ทรงสิทธิ์|เท่ห์ อุเทน|จักรพรรดิ|บิลลี่ โอแกน|ติ๊ก ชิโร่", GenreString, Era90s },
            { "แร็พเตอร์|ลิฟท์ ออย|เจอาร์ วอย|โดม ปกรณ์|เจมส์ เรืองศักดิ์|ดัง พันกร|บาซู|ดีทูบี|d2b|ซาซ่า|ไทรอัมส์คิงดอม|พองพอง|นาวิน ต้าร์|ไบรโอนี่", GenreString, Era90s },

            // สตริง / ป็อป / ร็อก ยุค 2000
            { "บอดี้สแลม|bodyslam|บิ๊กแอส|big ass|โปเตโต้|potato|แคลช|clash|ซิลลี่ฟูลส์|silly fools|กะลา|หนุ่ม กะลา|ลาบานูน|labanoon|พาราด็อกซ์|paradox|เอบีนอร์มอล|ab normal|แท็กซี่|taxi|ไอน้ำ|อุลตร้าช้วด|ซีล|zeal|เรโทรสเปกต์|retrospect|สวีทมัลเล็ต|โซคูล|so cool|เกียร์ไนท์|กะลา", GenreString, Era2000s },
            { "ดา เอ็นโดรฟิน|เอ็นโดรฟิน|endorphine|ปาล์มมี่|palmy|พั้นช์|พลพล|บอย พีซเมคเกอร์|พีซเมคเกอร์|peacemaker|บี้ สุกฤษฎิ์|ไอซ์ ศรัณยู|อ๊อฟ ปองศักดิ์|นิว จิ๋ว|ปาน ธนพร|ลิเดีย|ตอง ภัครมัย|แคทรียา|ชิน ชินวุฒ|กอล์ฟ ไมค์|โฟร์ มด|เฟย์ ฟาง แก้ว|คามิกาเซ่|กรู๊ฟไรเดอร์ส|สครับบ์|scrubb|อพาร์ตเมนต์คุณป้า|เสลอ|slur", GenreString, Era2000s },

            // สตริง / ป็อป / ร็อก ยุค 2010
            { "ค็อกเทล|cocktail|ทเวนตี้ไฟว์อาวเวอร์ส|25 hours|25hours|แทททูคัลเลอร์|tattoo colour|เกทสึโนว่า|getsunova|มายด์|mild|ลิปตา|lipta|สิงโต นำโชค|แสตมป์|ป๊อบ ปองกูล|อะตอม ชนกันต์|เป๊ก ผลิตโชค|ทอย|the toys|เดอะทอยส์|รูม 39|room39|ทอม อิศรา|สlot machine|สล็อตแมชชีน|เคลียร์|klear|อินสติงต์|instinct|ซิงกูล่าร์|แพนเค้ก|เบดรูม ออดิโอ|bedroom audio|ทรงไทย|สงกรานต์|ปู่จ๋าน|ยัวร์บอยทีเจ|urboytj|ทูพี|ตู่ ภพธร|ว่าน ธนกฤต|ลุลา|lula", GenreString, Era2010s },

            // สตริง / ป็อป / ร็อก ยุคใหม่ (2020s)
            { "ทรีแมนดาวน์|three man down|ทิลลี่เบิร์ด|tilly birds|นนท์ ธนนท์|nont tanont|อิ้งค์ วรันธร|ink waruntorn|โบกี้ไลอ้อน|bowkylion|เจฟ ซาเตอร์|jeff satur|เปเปอร์เพลนส์|paper planes|โอนลี่มันเดย์|only monday|เฟิร์ส อนุวัตน์|มีน|mean|เฟลโล่|fellow fellow|พิกซี่|pixxie|โฟร์อีฟ|4eve|แอทลาส|atlas|บัส|bus|ไมยราพ|maiyarap|ยังโอม|youngohm|สไปรท์|sprite|กานต์ ทศน|ทศกัณฐ์|พงศ์ พัทลุง|เนสท์|วิโอเลต|violette|ส้ม มารี|zom marie|บิวกิ้น|พีพี|ปอนด์|ภูวินทร์", GenreString, EraModern }
        };
        return cat;
    }

    static QStringList peerArtistsFor(Genre g, Era e)
    {
        switch (g) {
        case GenreLukKrung:
            return { QString::fromUtf8("สุนทราภรณ์"), QString::fromUtf8("ชรินทร์ นันทนาคร"),
                     QString::fromUtf8("สุเทพ วงศ์กำแหง"), QString::fromUtf8("อรวี สัจจานนท์"),
                     QString::fromUtf8("กุ้ง กิตติคุณ"), QString::fromUtf8("ธานินทร์ อินทรเทพ") };
        case GenreMorLam:
            if (e == EraClassic80s || e == Era90s)
                return { QString::fromUtf8("พรศักดิ์ ส่องแสง"), QString::fromUtf8("เฉลิมพล มาลาคำ"),
                         QString::fromUtf8("จินตหรา พูนลาภ"), QString::fromUtf8("สมจิตร บ่อทอง"),
                         QString::fromUtf8("หงษ์ทอง ดาวอุดร") };
            if (e == Era2000s)
                return { QString::fromUtf8("ศิริพร อำไพพงษ์"), QString::fromUtf8("มนต์แคน แก่นคูน"),
                         QString::fromUtf8("จินตหรา พูนลาภ"), QString::fromUtf8("ไหมไทย ใจตะวัน"),
                         QString::fromUtf8("แมน มณีวรรณ"), QString::fromUtf8("ดอกอ้อ ทุ่งทอง") };
            return { QString::fromUtf8("มนต์แคน แก่นคูน"), QString::fromUtf8("เวียง นฤมล"),
                     QString::fromUtf8("แอน อรดี"), QString::fromUtf8("ลำเพลิน วงศกร"),
                     QString::fromUtf8("เบียร์ พร้อมพงษ์"), QString::fromUtf8("ก้านตอง ทุ่งเงิน") };
        case GenrePhueaChiwit:
            if (e == EraClassic80s || e == Era90s)
                return { QString::fromUtf8("คาราบาว"), QString::fromUtf8("พงษ์สิทธิ์ คำภีร์"),
                         QString::fromUtf8("พงษ์เทพ กระโดนชำนาญ"), QString::fromUtf8("คาราวาน"),
                         QString::fromUtf8("คนด่านเกวียน") };
            return { QString::fromUtf8("พงษ์สิทธิ์ คำภีร์"), QString::fromUtf8("คาราบาว"),
                     QString::fromUtf8("มาลีฮวนน่า"), QString::fromUtf8("แช่ม แช่มรัมย์"),
                     QString::fromUtf8("พี สะเดิด"), QString::fromUtf8("วงพัทลุง") };
        case GenreLukThung:
            if (e == EraClassic80s)
                return { QString::fromUtf8("ยอดรัก สลักใจ"), QString::fromUtf8("สายัณห์ สัญญา"),
                         QString::fromUtf8("พุ่มพวง ดวงจันทร์"), QString::fromUtf8("ศรเพชร ศรสุพรรณ"),
                         QString::fromUtf8("เสรี รุ่งสว่าง") };
            if (e == Era90s)
                return { QString::fromUtf8("ก๊อต จักรพรรณ์"), QString::fromUtf8("ไมค์ ภิรมย์พร"),
                         QString::fromUtf8("ยิ่งยง ยอดบัวงาม"), QString::fromUtf8("เอกราช สุวรรณภูมิ"),
                         QString::fromUtf8("สุนารี ราชสีมา"), QString::fromUtf8("สันติ ดวงสว่าง") };
            if (e == Era2000s)
                return { QString::fromUtf8("ไผ่ พงศธร"), QString::fromUtf8("ต่าย อรทัย"),
                         QString::fromUtf8("ไมค์ ภิรมย์พร"), QString::fromUtf8("ตั๊กแตน ชลดา"),
                         QString::fromUtf8("มนต์แคน แก่นคูน") };
            return { QString::fromUtf8("มนต์แคน แก่นคูน"), QString::fromUtf8("ไผ่ พงศธร"),
                     QString::fromUtf8("ต่าย อรทัย"), QString::fromUtf8("เบียร์ พร้อมพงษ์"),
                     QString::fromUtf8("ลำเพลิน วงศกร"), QString::fromUtf8("ก้อง ห้วยไร่") };
        case GenreString:
            if (e == EraClassic80s)
                return { QString::fromUtf8("แกรนด์เอ็กซ์"), QString::fromUtf8("คีรีบูน"),
                         QString::fromUtf8("เรนโบว์"), QString::fromUtf8("ฟรุ๊ตตี้"),
                         QString::fromUtf8("แจ้ ดนุพล"), QString::fromUtf8("ปั่น ไพบูลย์เกียรติ") };
            if (e == Era90s)
                return { QString::fromUtf8("โลโซ"), QString::fromUtf8("อัสนี วสันต์"),
                         QString::fromUtf8("ไมโคร"), QString::fromUtf8("หิน เหล็ก ไฟ"),
                         QString::fromUtf8("เสือ ธนพล"), QString::fromUtf8("เบิร์ด ธงไชย"),
                         QString::fromUtf8("เจ เจตริน"), QString::fromUtf8("มอส ปฏิภาณ") };
            if (e == Era2000s)
                return { QString::fromUtf8("บอดี้สแลม"), QString::fromUtf8("โปเตโต้"),
                         QString::fromUtf8("แคลช"), QString::fromUtf8("บิ๊กแอส"),
                         QString::fromUtf8("กะลา"), QString::fromUtf8("ลาบานูน"),
                         QString::fromUtf8("ดา เอ็นโดรฟิน"), QString::fromUtf8("พั้นช์") };
            if (e == Era2010s)
                return { QString::fromUtf8("ค็อกเทล"), QString::fromUtf8("25 hours"),
                         QString::fromUtf8("เกทสึโนว่า"), QString::fromUtf8("ป๊อบ ปองกูล"),
                         QString::fromUtf8("อะตอม ชนกันต์"), QString::fromUtf8("เคลียร์") };
            return { QString::fromUtf8("Three Man Down"), QString::fromUtf8("Tilly Birds"),
                     QString::fromUtf8("นนท์ ธนนท์"), QString::fromUtf8("โบกี้ไลอ้อน"),
                     QString::fromUtf8("เจฟ ซาเตอร์"), QString::fromUtf8("Only Monday") };
        default:
            return {};
        }
    }

    static void classifyGenreAndEra(const QString &name, const QString &artist,
                                    const QString &lyrics, const QString &songType,
                                    Genre *outGenre, Era *outEra)
    {
        const QString all = (artist + " " + name + " " + lyrics).toLower();
        const QString na = (artist + " " + name).toLower();

        if (!hasThai(all)) {
            bool hasLatin = false;
            for (const QChar c : na)
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { hasLatin = true; break; }
            if (hasLatin) {
                *outGenre = GenreInternational;
                static const QRegularExpression re80("70s|80s|197\\d|198\\d", QRegularExpression::CaseInsensitiveOption);
                static const QRegularExpression re90("90s|199\\d", QRegularExpression::CaseInsensitiveOption);
                static const QRegularExpression re00("2000s|200\\d", QRegularExpression::CaseInsensitiveOption);
                *outEra = re80.match(na).hasMatch() ? EraClassic80s
                        : re90.match(na).hasMatch() ? Era90s
                        : re00.match(na).hasMatch() ? Era2000s : Era90s;
                return;
            }
        }

        // 1. Check artist catalog first (most reliable for both Genre and Era); regexes compiled once
        static const QVector<QRegularExpression> catalogRe = []() {
            QVector<QRegularExpression> v;
            for (const ArtistMeta &m : artistCatalog())
                v << QRegularExpression(QString::fromUtf8(m.pattern), QRegularExpression::CaseInsensitiveOption);
            return v;
        }();
        const QVector<ArtistMeta> &cat = artistCatalog();
        for (int i = 0; i < cat.size(); ++i) {
            if (catalogRe.at(i).match(na).hasMatch()) {
                *outGenre = cat.at(i).genre;
                *outEra = cat.at(i).era;
                return;
            }
        }

        // 2. Keyword & dialect markers in title / artist / lyrics
        static const QRegularExpression reMorLam(
            QString::fromUtf8("หมอลำ|ลำซิ่ง|ลำเพลิน|ลำเต้ย|ลำล่อง|ลำเดิน|แคน|พิณ|เซิ้ง|อีสาน|ผู้บ่าว|ผู้สาว|คึดฮอด|อีหลี|แม่นบ่|บ่ได้|อ้ายฮัก|ข่อย|มื้อนี้|นำแหน่"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression rePhueaChiwit(
            QString::fromUtf8("เพื่อชีวิต|ตำนานเพื่อชีวิต|ขอนไม้กับเรือ|หนุ่มบาว|สาวปาน|มือปืน|ทะเลใจ|บัวลอย"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reLukKrung(
            QString::fromUtf8("ลูกกรุง|สุนทราภรณ์|ลีลาศ|รำวงมาตรฐาน|ฟลอร์เฟื่องฟ้า|วิมาน|ราตรี|ขวัญใจ"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reLukThung(
            QString::fromUtf8("ลูกทุ่ง|บ้านนา|ท้องนา|หนุ่มนา|สาวนา|แม่ค้า|ทหารเกณฑ์|คนไกลบ้าน|งานวัด|จดหมาย|อาร์สยาม|ไหทองคำ|แกรมมี่โกลด์|อ้าย|น้องหล่า"),
            QRegularExpression::CaseInsensitiveOption);

        if (reMorLam.match(all).hasMatch())
            *outGenre = GenreMorLam;
        else if (rePhueaChiwit.match(all).hasMatch())
            *outGenre = GenrePhueaChiwit;
        else if (reLukKrung.match(all).hasMatch())
            *outGenre = GenreLukKrung;
        else if (reLukThung.match(all).hasMatch())
            *outGenre = GenreLukThung;
        else
            *outGenre = GenreString;

        // 3. Era keywords or fallback from genre + songType
        static const QRegularExpression reEra80(QString::fromUtf8("ยุค\\s*70|ยุค\\s*80|70s|80s|ลูกกรุง|สุนทราภรณ์|ย้อนยุค"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reEra90(QString::fromUtf8("ยุค\\s*90|90s|90's"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reEra00(QString::fromUtf8("ยุค\\s*2000|2000s|00s"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reEra10(QString::fromUtf8("ยุค\\s*2010|2010s"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reEraMod(QString::fromUtf8("เพลงใหม่|มาแรง|202\\d|ติดเทรนด์"), QRegularExpression::CaseInsensitiveOption);

        if (reEra80.match(na).hasMatch() || *outGenre == GenreLukKrung)
            *outEra = EraClassic80s;
        else if (reEra90.match(na).hasMatch())
            *outEra = Era90s;
        else if (reEra00.match(na).hasMatch())
            *outEra = Era2000s;
        else if (reEra10.match(na).hasMatch())
            *outEra = Era2010s;
        else if (reEraMod.match(na).hasMatch())
            *outEra = EraModern;
        else if (songType.toUpper() == "YOUTUBE")
            *outEra = EraModern;
        else
            *outEra = Era2000s;     // typical NCN / EMK / SIB karaoke library era
    }

    // Build 4D ContextProfile: 70% weight on the most recent customer song (sd[0]), 30% on the last 3-5 songs
    static ContextProfile buildContext(const QVector<Seed> &sd)
    {
        ContextProfile cp;
        if (sd.isEmpty())
            return cp;

        cp.hasSeed = true;
        const Seed &last = sd.first();
        cp.lastSongName = last.name;
        cp.lastArtist = last.artist;
        cp.lastYtId = last.ytId;
        cp.primarySongType = last.songType;
        cp.mediaKind = classifyMediaKind(last.songType);

        // Language check across recent songs
        int thai = 0;
        const int langCheckCount = qMin(5, sd.size());
        for (int i = 0; i < langCheckCount; ++i)
            thai += hasThai(sd[i].name + sd[i].artist) ? 1 : 0;
        cp.lang = !hasThai(last.name + last.artist) && thai == 0 ? 2 : (hasThai(last.name + last.artist) ? 1 : 0);

        // Weighted votes: sd[0] = 7 points (70%), sd[1..4] = 1 point each (~30% total)
        QHash<int, int> genreVotes;
        QHash<int, int> eraVotes;
        const int n = qMin(5, sd.size());
        for (int i = 0; i < n; ++i) {
            const int w = (i == 0) ? 7 : 1;
            Genre g = GenreUnknown;
            Era e = EraUnknown;
            classifyGenreAndEra(sd[i].name, sd[i].artist, sd[i].lyrics, sd[i].songType, &g, &e);
            if (g != GenreUnknown)
                genreVotes[int(g)] += w;
            if (e != EraUnknown)
                eraVotes[int(e)] += w;
        }

        int bestGScore = -1;
        for (auto it = genreVotes.constBegin(); it != genreVotes.constEnd(); ++it) {
            if (it.value() > bestGScore) {
                bestGScore = it.value();
                cp.genre = Genre(it.key());
            }
        }
        int bestEScore = -1;
        for (auto it = eraVotes.constBegin(); it != eraVotes.constEnd(); ++it) {
            if (it.value() > bestEScore) {
                bestEScore = it.value();
                cp.era = Era(it.key());
            }
        }

        // Tempo / BPM: prioritize the song that just finished (sd[0]) so the transition matches its feel
        if (last.tempo > 0) {
            cp.targetBpm = last.tempo;
            cp.tempoBand = classifyTempoBand(last.tempo);
        } else {
            cp.tempoBand = classifyTempoBand(0, last.name + " " + last.artist + " " + last.lyrics);
            if (cp.tempoBand == TempoSlow)
                cp.targetBpm = 76;
            else if (cp.tempoBand == TempoFast)
                cp.targetBpm = 132;
            else {
                // Check if recent songs in the same session had an explicit BPM
                int sumBpm = 0, cntBpm = 0;
                for (int i = 1; i < n; ++i) {
                    if (sd[i].tempo > 0) {
                        sumBpm += sd[i].tempo;
                        ++cntBpm;
                    }
                }
                if (cntBpm > 0) {
                    cp.targetBpm = sumBpm / cntBpm;
                    cp.tempoBand = classifyTempoBand(cp.targetBpm);
                } else {
                    cp.tempoBand = TempoMedium;
                    cp.targetBpm = 100;
                }
            }
        }
        return cp;
    }

    // Score how well a candidate song matches the 4D ContextProfile (-1000 = reject)
    static int scoreCandidate(const QString &name, const QString &artist, const QString &lyrics,
                              const QString &songType, int tempo, bool isLocal,
                              const ContextProfile &cp, const QString &styleOverride)
    {
        if (styleOverride == "slow") {
            if (tempo >= 105) return -1000;
            const TempoBand tb = classifyTempoBand(tempo, name + " " + lyrics);
            if (tb == TempoFast) return -1000;
            return (tb == TempoSlow ? 60 : 20);
        }
        if (styleOverride == "fast") {
            if (tempo > 0 && tempo < 110) return -1000;
            const TempoBand tb = classifyTempoBand(tempo, name + " " + lyrics);
            if (tb == TempoSlow) return -1000;
            return (tb == TempoFast ? 60 : 20);
        }
        if (!cp.hasSeed || styleOverride != "auto")
            return 10;

        int score = 0;
        Genre candGenre = GenreUnknown;
        Era candEra = EraUnknown;
        classifyGenreAndEra(name, artist, lyrics, songType, &candGenre, &candEra);
        const TempoBand candTempoBand = classifyTempoBand(tempo, name + " " + artist + " " + lyrics);

        // 1. Tempo / BPM compatibility (Hard gate against jarring tempo jumps)
        if (cp.tempoBand == TempoSlow) {
            if (tempo >= 104 || candTempoBand == TempoFast)
                return -1000;   // Never jump from a slow song (< 88 BPM) to a fast song
            if (tempo > 0 && cp.targetBpm > 0) {
                const int diff = qAbs(tempo - cp.targetBpm);
                if (diff <= 10) score += 45;
                else if (diff <= 18) score += 28;
                else score += 10;
            } else if (candTempoBand == TempoSlow) {
                score += 32;
            }
        } else if (cp.tempoBand == TempoFast) {
            if ((tempo > 0 && tempo < 105) || candTempoBand == TempoSlow)
                return -1000;   // Never drop from a fast dance/3-cha song (>= 119 BPM) to a slow ballad
            if (tempo > 0 && cp.targetBpm > 0) {
                const int diff = qAbs(tempo - cp.targetBpm);
                if (diff <= 12) score += 45;
                else if (diff <= 22) score += 28;
                else score += 12;
            } else if (candTempoBand == TempoFast) {
                score += 35;
            }
        } else if (cp.tempoBand == TempoMedium) {
            if (tempo > 0 && (tempo < 72 || tempo > 134))
                return -1000;
            if (tempo > 0 && cp.targetBpm > 0) {
                const int diff = qAbs(tempo - cp.targetBpm);
                if (diff <= 12) score += 40;
                else if (diff <= 20) score += 25;
                else score += 8;
            } else if (candTempoBand != TempoFast) {
                score += 22;
            }
        }

        // 2. Genre compatibility
        if (cp.genre != GenreUnknown && candGenre != GenreUnknown) {
            if (candGenre == cp.genre) {
                score += 50;
            } else if ((cp.genre == GenreLukThung && candGenre == GenreMorLam)
                    || (cp.genre == GenreMorLam && candGenre == GenreLukThung)) {
                score += 25;    // Luk Thung and Mor Lam are closely related sister genres
            } else if ((cp.genre == GenrePhueaChiwit && (candGenre == GenreLukThung || candGenre == GenreString))
                    || (cp.genre == GenreString && candGenre == GenrePhueaChiwit)) {
                score += 10;
            } else if (cp.genre == GenreLukKrung && candGenre != GenreLukKrung) {
                return -1000;   // Luk Krung has a distinct classic mood; don't mix with modern pop/mor lam
            } else if (cp.genre == GenreInternational && candGenre != GenreInternational) {
                return -1000;
            } else {
                score -= 30;
            }
        }

        // 3. Era compatibility
        if (cp.era != EraUnknown && candEra != EraUnknown) {
            if (candEra == cp.era) {
                score += 35;
            } else if (qAbs(int(candEra) - int(cp.era)) == 1) {
                score += 18;    // Adjacent era (e.g. 90s <-> 2000s) flows smoothly
            } else if (qAbs(int(candEra) - int(cp.era)) >= 3) {
                score -= 25;    // Avoid jumping between Classic 80s and 2020s Modern
            }
        }

        // 4. Same artist: the owner wants other singers of the same style, so a small minus
        if (!cp.lastArtist.trimmed().isEmpty() && !artist.trimmed().isEmpty()) {
            const QString na = norm(artist), nl = norm(cp.lastArtist);
            if (!na.isEmpty() && !nl.isEmpty() && (na.contains(nl) || nl.contains(na)))
                score -= 20;
        }

        // 5. MediaKind / SongType alignment
        const MediaKind candMedia = classifyMediaKind(songType);
        if (cp.mediaKind == MediaMidi && isLocal && candMedia == MediaMidi)
            score += 20;
        else if (cp.mediaKind == MediaLocalAV && isLocal && candMedia == MediaLocalAV)
            score += 25;
        else if (cp.mediaKind == MediaYouTube && !isLocal)
            score += 20;

        return score;
    }

    // Purge any ready items that clash with the newly updated customer song context
    void filterReadyForContext(const ContextProfile &cp)
    {
        if (!cp.hasSeed || style != "auto")
            return;
        const QString tag = formatContextTag(cp);
        QVector<IdlePick> kept;
        for (IdlePick p : ready) {
            if (p.contextTag == tag) {      // prepared for this same context already
                kept << p;
                continue;
            }
            const QString name = pickTitle(p);
            const QString artist = p.local ? p.song.artist() : p.yt.channel;
            const QString lyr = p.local ? p.song.lyrics() : QString();
            const QString sType = p.local ? p.song.songType() : QStringLiteral("YOUTUBE");
            const int bpm = p.local ? p.song.tempo() : 0;
            const int sc = scoreCandidate(name, artist, lyr, sType, bpm, p.local, cp, style);
            if (sc > -500) {                // only clear clashes (tempo jump, wrong genre family) are dropped
                p.contextTag = tag;
                kept << p;
            }
        }
        ready = kept;
    }

    // Build targeted YouTube search queries that match the 4D ContextProfile
    static QStringList buildContextQueries(const ContextProfile &cp)
    {
        QStringList queries;
        if (!cp.hasSeed)
            return queries;

        QString genreWord;
        switch (cp.genre) {
        case GenreLukThung:      genreWord = QString::fromUtf8("เพลงลูกทุ่ง"); break;
        case GenreMorLam:        genreWord = QString::fromUtf8("เพลงหมอลำ ลูกทุ่งอีสาน"); break;
        case GenrePhueaChiwit:   genreWord = QString::fromUtf8("เพลงเพื่อชีวิต"); break;
        case GenreLukKrung:      genreWord = QString::fromUtf8("เพลงลูกกรุง สุนทราภรณ์"); break;
        case GenreString:        genreWord = QString::fromUtf8("เพลงสตริง"); break;
        case GenreInternational: genreWord = QStringLiteral("Pop Rock Hits"); break;
        default:                 genreWord = QString::fromUtf8("เพลงฮิต"); break;
        }

        QString eraWord;
        switch (cp.era) {
        case EraClassic80s: eraWord = (cp.genre == GenreInternational) ? QStringLiteral("80s Classic") : QString::fromUtf8("ยุค 80 ต้นฉบับ"); break;
        case Era90s:        eraWord = (cp.genre == GenreInternational) ? QStringLiteral("90s") : QString::fromUtf8("ยุค 90"); break;
        case Era2000s:      eraWord = (cp.genre == GenreInternational) ? QStringLiteral("2000s") : QString::fromUtf8("ยุค 2000"); break;
        case Era2010s:      eraWord = (cp.genre == GenreInternational) ? QStringLiteral("2010s") : QString::fromUtf8("ยุค 2010"); break;
        case EraModern:     eraWord = (cp.genre == GenreInternational) ? QStringLiteral("Hits") : QString::fromUtf8("มาแรง"); break;
        default: break;
        }

        QString tempoWord;
        if (cp.genre == GenreInternational) {
            if (cp.tempoBand == TempoSlow)      tempoWord = QStringLiteral("Slow Love Ballad");
            else if (cp.tempoBand == TempoFast) tempoWord = QStringLiteral("Upbeat Dance");
        } else {
            if (cp.tempoBand == TempoSlow)
                tempoWord = QString::fromUtf8("เพลงช้า ซึ้งๆ");
            else if (cp.tempoBand == TempoFast)
                tempoWord = (cp.genre == GenreMorLam) ? QString::fromUtf8("ลำซิ่ง มันส์ๆ")
                          : (cp.genre == GenreLukThung || cp.genre == GenrePhueaChiwit) ? QString::fromUtf8("สามช่า สนุกๆ")
                          : QString::fromUtf8("เพลงเร็ว สนุกๆ");
            else
                tempoWord = QString::fromUtf8("เพลงฮิต");
        }

        // Query 1: Genre + Era + Tempo mood
        QString q1 = (genreWord + " " + eraWord + " " + tempoWord).simplified();
        if (!q1.isEmpty())
            queries << q1;

        // Query 2: Last artist + tempo/genre feel (or peer artists of the exact same Genre + Era)
        const QString cleanArtist = cp.lastArtist.trimmed();
        if (peerArtistsFor(cp.genre, cp.era).size() < 2   // only when there are no other singers to ask for
                && !cleanArtist.isEmpty() && cleanArtist != QString::fromUtf8("ไม่ระบุศิลปิน")) {
            const QString q2 = (cleanArtist + " " + (cp.tempoBand == TempoSlow ? QString::fromUtf8("เพลงช้า")
                                                    : cp.tempoBand == TempoFast ? QString::fromUtf8("เพลงเร็ว")
                                                    : QString::fromUtf8("เพลงฮิต"))).simplified();
            if (!queries.contains(q2))
                queries << q2;
        }

        QStringList peers = peerArtistsFor(cp.genre, cp.era);
        const QString lastN = norm(cleanArtist);
        for (int i = peers.size() - 1; i >= 0; --i)     // not the singer just sung
            if (!lastN.isEmpty() && (norm(peers.at(i)).contains(lastN) || lastN.contains(norm(peers.at(i)))))
                peers.removeAt(i);
        for (int n = 0; n < 2 && !peers.isEmpty(); ++n) {
            const QString peer = peers.takeAt(QRandomGenerator::global()->bounded(peers.size()));
            const QString q3 = (peer + " " + tempoWord).simplified();
            if (!queries.contains(q3))
                queries << q3;
        }

        return queries;
    }

    bool wasPlayed(const IdlePick &p) const
    {
        const qint64 limit = QDateTime::currentMSecsSinceEpoch() - 4LL * 3600 * 1000;   // 4 hours
        if ((!p.libKey.isEmpty() && played.value("lib:" + p.libKey, 0) > limit)
                || (!p.yt.id.isEmpty() && played.value("yt:" + p.yt.id, 0) > limit))
            return true;
        QSet<QString> titles;
        for (auto it = played.constBegin(); it != played.constEnd(); ++it)
            if (it.key().startsWith("title:") && it.value() > limit)
                titles.insert(it.key().mid(6));
        return titleClash(pickTitle(p), !p.local, titles);
    }
    void markPlayed(const IdlePick &p)
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!p.libKey.isEmpty())
            played.insert("lib:" + p.libKey, now);
        if (!p.yt.id.isEmpty())
            played.insert("yt:" + p.yt.id, now);
        const QString t = titleKey(pickTitle(p));
        if (!t.isEmpty())
            played.insert("title:" + t, now);
    }

    // ---- cache: the chart and the clips that cannot be played --------------------------------------
    void loadCache()
    {
        QFile f(cacheFile());
        if (!f.open(QIODevice::ReadOnly))
            return;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        chartTime = qint64(o.value("chartTime").toDouble());
        for (const QJsonValue &v : o.value("chart").toArray()) {
            const QJsonObject e = v.toObject();
            YtItem it;
            it.id = e.value("id").toString();
            it.title = e.value("title").toString();
            it.channel = e.value("channel").toString();
            it.seconds = e.value("seconds").toInt();
            if (!it.id.isEmpty())
                chart << it;
        }
        for (const QJsonValue &v : o.value("bad").toArray())
            bad.insert(v.toString());
    }
    void saveCache()
    {
        QJsonArray arr;
        for (const YtItem &it : chart)
            arr.append(QJsonObject{ { "id", it.id }, { "title", it.title }, { "channel", it.channel }, { "seconds", it.seconds } });
        QJsonArray b;
        for (const QString &id : bad)
            b.append(id);
        QFile f(cacheFile());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(QJsonObject{ { "chartTime", double(chartTime) }, { "chart", arr }, { "bad", b } })
                        .toJson(QJsonDocument::Compact));
    }

    // ---- yt-dlp ---------------------------------------------------------------------------------------
    static QVector<YtItem> parse(const QByteArray &json)
    {
        QVector<YtItem> out;
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        for (const QJsonValue &v : root.value("entries").toArray()) {
            const QJsonObject e = v.toObject();
            YtItem it;
            it.id = e.value("id").toString();
            it.title = e.value("title").toString();
            it.channel = e.value("channel").toString();
            if (it.channel.isEmpty())
                it.channel = e.value("uploader").toString();
            it.seconds = int(e.value("duration").toDouble());
            if (it.id.size() == 11)
                out << it;
        }
        return out;
    }

    // view counts the searches give (the chart and Mix give none)
    static QHash<QString, qint64> parseViews(const QByteArray &json)
    {
        QHash<QString, qint64> out;
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        for (const QJsonValue &v : root.value("entries").toArray()) {
            const QJsonObject e = v.toObject();
            if (e.contains("view_count") && !e.value("view_count").isNull())
                out.insert(e.value("id").toString(), qint64(e.value("view_count").toDouble()));
        }
        return out;
    }

    // runs yt-dlp; done(clips) on the UI thread (empty on failure / time out)
    void fetch(const QStringList &args, const std::function<void(const QVector<YtItem> &)> &done)
    {
        QProcess *p = new QProcess(ctx);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONIOENCODING", "utf-8");
        env.insert("PYTHONUTF8", "1");
        p->setProcessEnvironment(env);
        std::weak_ptr<int> guard = alive;
        IdleSmart *self = this;
        QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), ctx,
                         [p, done, guard, self](int code, QProcess::ExitStatus st) {
            const QByteArray out = p->readAllStandardOutput();
            p->deleteLater();
            if (guard.expired())
                return;
            const bool ok = st == QProcess::NormalExit && code == 0;
            if (ok) {
                const QHash<QString, qint64> v = parseViews(out);
                for (auto it = v.constBegin(); it != v.constEnd(); ++it)
                    self->views.insert(it.key(), it.value());
            }
            done(ok ? parse(out) : QVector<YtItem>());
        });
        QObject::connect(p, &QProcess::errorOccurred, ctx, [p, done, guard](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            p->deleteLater();           // finished() does not come
            if (!guard.expired())
                done(QVector<YtItem>());
        });
        // a stuck request (no internet) must not hold the next ones
        QTimer::singleShot(45000, p, [p]() { if (p->state() != QProcess::NotRunning) p->kill(); });
        p->start(ytdlp(), QStringList{ "--flat-playlist", "-J", "--no-warnings", "--socket-timeout", "15" } + args);
    }

    // ---- what to fetch --------------------------------------------------------------------------------
    void startRefill()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool chartOld = chart.isEmpty() || now - chartTime > 24LL * 3600 * 1000;

        if (style == "hits" || style == "auto") {
            const QVector<Seed> sd = (style == "auto" && seeds) ? seeds() : QVector<Seed>();
            if (style == "auto")
                currentCtx = buildContext(sd);

            // Fast path: if ready is empty/low and we have a song database, immediately seed context-matched
            // library songs on a worker thread so idle music never waits on slow yt-dlp network calls.
            if (style == "auto" && ready.size() < 3 && !dbFile.isEmpty() && !sd.isEmpty()) {
                prepare(QVector<YtItem>(), sd, QStringLiteral("local-fast"));
            }

            if (ytdlp().isEmpty()) {
                busy = false;
                return;
            }

            const bool trendAll = (style == "auto");
            auto afterChart = [this, sd, trendAll]() {
                if (!trendAll) {
                    say(QString("chart only (%1 clips)").arg(chart.size()));
                    prepare(sample(chart, 12), {}, QString());
                    return;
                }
                if (currentCtx.hasSeed)
                    fetchContextClips(sd, currentCtx);
                else
                    trending(sd);
            };
            if (chartOld && (!trendAll || !currentCtx.hasSeed || (currentCtx.genre == GenreString && currentCtx.era == EraModern))) {
                fetch({ "https://www.youtube.com/playlist?list=PL4fGSI1pDJn5gHpWhR17s41BbppFJFf08" },
                      [this, afterChart](const QVector<YtItem> &list) {
                    if (!list.isEmpty()) {
                        chart = list;
                        chartTime = QDateTime::currentMSecsSinceEpoch();
                        saveCache();
                        say(QString("chart read: %1 clips").arg(list.size()));
                    } else {
                        say("chart not read (no internet?)");
                    }
                    afterChart();
                });
            } else {
                afterChart();
            }
            return;
        }

        if (ytdlp().isEmpty()) {
            busy = false;
            return;
        }

        // a style: a YouTube search, kept 12 hours
        const QString query = style == "slow" ? QString::fromUtf8("เพลงช้า ซึ้งๆ เพลงฮิต")
                            : style == "fast" ? QString::fromUtf8("เพลงสนุก มันส์ๆ เพลงฮิต")
                                              : style + QString::fromUtf8(" เพลงฮิต");
        if (searches.contains(query) && now - searchTime.value(query) < 12LL * 3600 * 1000) {
            prepare(sample(searches.value(query), 15), {}, QString());
            return;
        }
        fetch({ "ytsearch40:" + query }, [this, query](const QVector<YtItem> &list) {
            if (!list.isEmpty()) {
                searches.insert(query, list);
                searchTime.insert(query, QDateTime::currentMSecsSinceEpoch());
            }
            say(QString("search \"%1\": %2 clips").arg(query).arg(list.size()));
            prepare(sample(onlyHits(list, 1000000), 15), {}, QString());
        });
    }

    // Fetch YouTube clips specifically tailored to the 4D ContextProfile of the customer's songs
    void fetchContextClips(const QVector<Seed> &sd, const ContextProfile &cp)
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QStringList queries = buildContextQueries(cp);
        if (queries.isEmpty()) {
            trending(sd);
            return;
        }

        QStringList missing;
        for (const QString &q : queries) {
            const QString key = "ctx:" + q;
            if (!searches.contains(key) || now - searchTime.value(key) > 6LL * 3600 * 1000)
                missing << q;
        }

        auto combine = [this, sd, cp, queries]() {
            QVector<YtItem> pick;
            QStringList got;
            for (const QString &q : queries) {
                const QString key = "ctx:" + q;
                const QVector<YtItem> raw = searches.value(key);
                QVector<YtItem> filtered = onlyHits(raw, 200000);
                if (filtered.size() < 4)
                    filtered = raw;
                const QVector<YtItem> part = sample(filtered, 6);
                got << QString("\"%1\" (%2)").arg(q).arg(part.size());
                pick += part;
            }
            if (cp.genre == GenreString && cp.era == EraModern && !chart.isEmpty()) {
                const QVector<YtItem> pop = sample(chart, 3);
                pick += pop;
            }
            say(QString("context [%1]: %2").arg(formatContextTag(cp), got.join(", ")));
            prepare(pick, sd, QString());
        };

        if (missing.isEmpty()) {
            combine();
            return;
        }

        auto left = std::make_shared<int>(missing.size());
        for (const QString &q : missing) {
            const QString key = "ctx:" + q;
            fetch({ "ytsearch25:" + q }, [this, key, q, left, combine](const QVector<YtItem> &list) {
                if (!list.isEmpty()) {
                    searches.insert(key, list);
                    searchTime.insert(key, QDateTime::currentMSecsSinceEpoch());
                }
                say(QString("context search \"%1\": %2 clips").arg(q).arg(list.size()));
                if (--*left == 0)
                    combine();
            });
        }
    }

    // the clips seen this many times or more; a stricter bar first, a lower one when it leaves few
    QVector<YtItem> onlyHits(const QVector<YtItem> &list, qint64 minViews) const
    {
        QVector<YtItem> out;
        for (const YtItem &it : list) {
            const qint64 v = views.value(it.id, -1);
            if (v < 0 || v >= minViews)          // (-1: no count given, e.g. the chart)
                out << it;
        }
        return out;
    }

    // Fallback when no customer context exists yet: trending clips across styles
    struct TrendSource { const char *query; const char *sp; int take; };
    static const QVector<TrendSource> &trendSources()
    {
        // sp: CAMSAggF = sort by views, uploaded this year; CAMSAggE = sort by views, this month
        static const QVector<TrendSource> s = {
            { "ลูกทุ่ง", "CAMSAggF", 4 },
            { "เพลงลูกทุ่งใหม่", "CAMSAggE", 2 },
            { "หมอลำ", "CAMSAggF", 3 },
            { "เพื่อชีวิต", "CAMSAggF", 2 },
        };
        return s;
    }
    static QString trendKey(const TrendSource &t) { return QString::fromUtf8(t.query) + "|" + t.sp; }

    void trending(const QVector<Seed> &sd)
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QVector<TrendSource> missing;
        for (const TrendSource &t : trendSources())
            if (!searches.contains(trendKey(t)) || now - searchTime.value(trendKey(t)) > 12LL * 3600 * 1000)
                missing << t;
        auto combine = [this, sd]() {
            QVector<YtItem> pick;
            QStringList got;
            for (const TrendSource &t : trendSources()) {
                const QVector<YtItem> part = sample(onlyHits(searches.value(trendKey(t)), 1000000), t.take);
                got << QString("%1 %2").arg(QString::fromUtf8(t.query)).arg(part.size());
                pick += part;
            }
            const QVector<YtItem> pop = sample(chart, 3);
            got << QString("chart %1").arg(pop.size());
            pick += pop;
            for (int i = pick.size() - 1; i > 0; --i)       // the styles mixed
                std::swap(pick[i], pick[QRandomGenerator::global()->bounded(i + 1)]);
            say("in fashion now: " + got.join(", "));
            prepare(pick, sd, QString());
        };
        if (missing.isEmpty()) {
            combine();
            return;
        }
        auto left = std::make_shared<int>(missing.size());
        for (const TrendSource &t : missing) {
            const QString url = "https://www.youtube.com/results?search_query="
                                + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(t.query)))
                                + "&sp=" + QString::fromLatin1(t.sp);
            const QString key = trendKey(t);
            fetch({ "--playlist-end", "25", url }, [this, key, left, combine](const QVector<YtItem> &list) {
                if (!list.isEmpty()) {
                    searches.insert(key, list);
                    searchTime.insert(key, QDateTime::currentMSecsSinceEpoch());
                }
                say(QString("search \"%1\": %2 clips").arg(key).arg(list.size()));
                if (--*left == 0)
                    combine();
            });
        }
    }

    // clips that are songs: not long medleys / karaoke versions / already played / unplayable
    QVector<YtItem> sample(const QVector<YtItem> &list, int n, bool shuffle = true) const
    {
        static const QRegularExpression notSong(
            QString::fromUtf8("รวมเพลง|ฟังยาว|ฟังต่อเนื่อง|non-?stop|playlist|คาราโอเกะ|karaoke|\\d+\\s*เพลง|เพลงเพราะๆ|ชั่วโมง|full album"
                              "|\\blive\\b|session|แสดงสด|คอนเสิร์ต|concert|รถแห่|cover|คัฟเวอร์|แสดงสด|เวอร์ชั่นแสดง"),
            QRegularExpression::CaseInsensitiveOption);
        const qint64 limit = QDateTime::currentMSecsSinceEpoch() - 4LL * 3600 * 1000;
        QVector<YtItem> ok;
        for (const YtItem &it : list) {
            if (bad.contains(it.id) || played.value("yt:" + it.id, 0) > limit)
                continue;
            if (it.seconds > 0 && (it.seconds < 90 || it.seconds > 480))
                continue;
            if (notSong.match(it.title).hasMatch())
                continue;
            ok << it;
        }
        if (shuffle)
            for (int i = ok.size() - 1; i > 0; --i)
                std::swap(ok[i], ok[QRandomGenerator::global()->bounded(i + 1)]);
        if (ok.size() > n)
            ok.resize(n);
        return ok;
    }

    // ---- library lookup & direct context query (worker thread) --------------------------------------
    static QString norm(const QString &s)
    {
        QString o;
        for (const QChar c : s.toLower())
            if (!c.isSpace() && c != '.' && c != '-' && c != '\'' && c != '"' && c != ',')
                o += c;
        return o;
    }
    // "ARTIST - Song (English) [Official MV]" -> { "ARTIST", "Song" }
    static QStringList titleParts(const QString &title)
    {
        static const QRegularExpression brackets(QString::fromUtf8("[\\[\\(【「（『].*?[\\]\\)】」）』]"));
        static const QRegularExpression feat("\\s(ft\\.?|feat\\.?|x)\\s.*?(?=\\s[-–]\\s|$)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression split(QString::fromUtf8("\\s[-–:]+\\s|\\s//\\s|\\|"));
        static const QRegularExpression noise(
            QString::fromUtf8("official|music video|lyrics? video|lyric|visuali[sz]er|audio|\\bm/?v\\b|cover|\\blive\\b|version|\\bfull\\b|\\b4k\\b|\\bhd\\b|ต้นฉบับ|เพลงจากละคร.*"),
            QRegularExpression::CaseInsensitiveOption);
        QString t = title;
        t.replace(brackets, " ");
        t.replace(feat, " ");
        QStringList out;
        for (QString s : t.split(split)) {
            s.replace(noise, " ");
            s = s.simplified();
            while (!s.isEmpty() && QString("-'\" ").contains(s.at(0))) s.remove(0, 1);
            while (!s.isEmpty() && QString("-'\" ").contains(s.at(s.size() - 1))) s.chop(1);
            if (s.size() >= 2)
                out << s;
        }
        return out;
    }
    // a library song found for a clip or direct context query, as plain values
    struct Found {
        YtItem yt;
        bool local = false;
        QString id, name, artist, key, type, lyrics, path;
        int tempo = 0;
        int contextScore = 0;
        QString contextTag;
    };

    static bool findInLibrary(QSqlDatabase &db, const YtItem &it, Found *out)
    {
        const QStringList parts = titleParts(it.title);
        for (const QString &p : parts) {
            const QString np = norm(p);
            QStringList hints;
            for (const QString &h : parts)
                if (norm(h) != np)
                    hints << norm(h);
            hints << norm(it.channel);
            for (int pass = 0; pass < 2; ++pass) {
                QSqlQuery q(db);
                q.prepare(pass == 0 ? "SELECT id, name, artist, keyname, tempo, songtype, lyrics, path FROM songs "
                                      "WHERE name = ? COLLATE NOCASE LIMIT 30"
                                    : "SELECT id, name, artist, keyname, tempo, songtype, lyrics, path FROM songs "
                                      "WHERE name LIKE ? LIMIT 30");
                q.bindValue(0, pass == 0 ? p : p + " (%");
                if (!q.exec())
                    continue;
                while (q.next()) {
                    if (q.value(5).toString() == "YOUTUBE")
                        continue;
                    const QString a = norm(q.value(2).toString());
                    if (a.isEmpty())
                        continue;
                    for (const QString &h : hints) {
                        if (h.isEmpty())
                            continue;
                        // same artist: one inside the other, or the same first 4 letters
                        if (a.contains(h) || h.contains(a) || (a.size() >= 4 && h.size() >= 4 && a.left(4) == h.left(4))) {
                            out->id = q.value(0).toString();
                            out->name = q.value(1).toString();
                            out->artist = q.value(2).toString();
                            out->key = q.value(3).toString();
                            out->tempo = q.value(4).toInt();
                            out->type = q.value(5).toString();
                            out->lyrics = q.value(6).toString();
                            out->path = q.value(7).toString();
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // Refine ContextProfile on the worker thread using the SQLite library (e.g. when a YouTube or Video
    // song had tempo == 0 or missing artist, look up its MIDI twin in `songs` to get exact BPM & lyrics)
    static void refineContextFromDb(QSqlDatabase &db, const QVector<Seed> &sd, ContextProfile *cp)
    {
        if (!db.isOpen() || sd.isEmpty() || !cp->hasSeed)
            return;
        const Seed &last = sd.first();
        if (last.tempo <= 0 && !last.name.trimmed().isEmpty()) {
            YtItem fakeYt;
            fakeYt.title = last.artist.isEmpty() ? last.name : (last.artist + " - " + last.name);
            fakeYt.channel = last.artist;
            Found f;
            if (findInLibrary(db, fakeYt, &f) && f.tempo > 0) {
                cp->targetBpm = f.tempo;
                cp->tempoBand = classifyTempoBand(f.tempo);
                if (cp->genre == GenreString || cp->genre == GenreUnknown) {
                    Genre g = GenreUnknown;
                    Era e = EraUnknown;
                    classifyGenreAndEra(f.name, f.artist, f.lyrics, f.type, &g, &e);
                    if (g != GenreUnknown) cp->genre = g;
                    if (e != EraUnknown)   cp->era = e;
                }
            }
        }
    }

    // Directly query the local SQLite database for songs that match the 4D ContextProfile
    // (same artist / peer artists of the same genre & era + matching BPM range + media type)
    static QVector<Found> queryLibraryByContext(QSqlDatabase &db, const ContextProfile &cp,
                                                const QSet<QString> &sungKeys, const QSet<QString> &sungTitles,
                                                const QString &st, int maxTake)
    {
        QVector<Found> candidates;
        if (!db.isOpen() || !cp.hasSeed)
            return candidates;

        int minBpm = 40, maxBpm = 220;
        if (cp.tempoBand == TempoSlow) {
            minBpm = 50;
            maxBpm = cp.targetBpm > 0 ? qMin(98, cp.targetBpm + 14) : 92;
        } else if (cp.tempoBand == TempoFast) {
            minBpm = cp.targetBpm > 0 ? qMax(112, cp.targetBpm - 16) : 116;
            maxBpm = 200;
        } else if (cp.tempoBand == TempoMedium) {
            minBpm = cp.targetBpm > 0 ? qMax(78, cp.targetBpm - 15) : 84;
            maxBpm = cp.targetBpm > 0 ? qMin(122, cp.targetBpm + 15) : 118;
        }

        QStringList targetArtists;
        if (!cp.lastArtist.trimmed().isEmpty() && cp.lastArtist != QString::fromUtf8("ไม่ระบุศิลปิน"))
            targetArtists << cp.lastArtist.trimmed();
        QStringList peerList = peerArtistsFor(cp.genre, cp.era);
        for (int i = peerList.size() - 1; i > 0; --i)
            peerList.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
        for (const QString &p : peerList)
            if (!targetArtists.contains(p))
                targetArtists << p;
        const bool firstIsLast = !targetArtists.isEmpty() && !cp.lastArtist.trimmed().isEmpty()
                && targetArtists.first() == cp.lastArtist.trimmed();

        QSet<QString> seenKeys = sungKeys;
        const QString tag = formatContextTag(cp);

        auto collectQuery = [&](QSqlQuery &q, int artistBonus) {
            if (!q.exec())
                return;
            while (q.next()) {
                Found f;
                f.local = true;
                f.id = q.value(0).toString();
                f.name = q.value(1).toString();
                f.artist = q.value(2).toString();
                f.key = q.value(3).toString();
                f.tempo = q.value(4).toInt();
                f.type = q.value(5).toString();
                f.lyrics = q.value(6).toString();
                f.path = q.value(7).toString();
                const QString k = f.type + f.path;
                if (f.type == "YOUTUBE" || seenKeys.contains(k))
                    continue;
                // one copy per title (the library holds many copies under other artist names)
                const QString tk = titleKey(f.name);
                const QString nk = "T:" + (tk.isEmpty() ? norm(f.name) + "|" + norm(f.artist) : tk);
                if (seenKeys.contains(nk) || (!tk.isEmpty() && sungTitles.contains(tk)))
                    continue;
                int sc = scoreCandidate(f.name, f.artist, f.lyrics, f.type, f.tempo, true, cp, st);
                if (sc < 30)
                    continue;
                sc += artistBonus;
                f.contextScore = sc;
                f.contextTag = tag;
                seenKeys.insert(k);
                seenKeys.insert(nk);
                candidates << f;
            }
        };

        // 1. If the customer sang a local VIDEO/MP3, look for matching VIDEO/MP3 files first
        if (cp.mediaKind == MediaLocalAV) {
            QSqlQuery q(db);
            q.prepare("SELECT id, name, artist, keyname, tempo, songtype, lyrics, path FROM songs "
                      "WHERE songtype IN ('VIDEO', 'MP3') LIMIT 80");
            collectQuery(q, 15);
        }

        // 2. Query songs by the last artist and peer artists of the same Genre + Era within BPM range
        int sameArtistTaken = 0;
        for (int i = 0; i < targetArtists.size() && candidates.size() < 40; ++i) {
            const QString &art = targetArtists.at(i);
            QSqlQuery q(db);
            q.prepare("SELECT id, name, artist, keyname, tempo, songtype, lyrics, path FROM songs "
                      "WHERE artist LIKE ? AND (tempo = 0 OR tempo BETWEEN ? AND ?) LIMIT 25");
            q.bindValue(0, "%" + art + "%");
            q.bindValue(1, minBpm);
            q.bindValue(2, maxBpm);
            const int before = candidates.size();
            const bool same = i == 0 && firstIsLast;
            collectQuery(q, same ? 0 : 8);
            // variety: one more song of the singer just sung, two of each other singer
            const int keep = same ? 1 : 2;
            if (candidates.size() - before > keep) {
                for (int j = candidates.size() - 1; j > before; --j)
                    std::swap(candidates[j], candidates[before + QRandomGenerator::global()->bounded(j - before + 1)]);
                candidates.resize(before + keep);
                sameArtistTaken += keep;
            }
            Q_UNUSED(sameArtistTaken);
        }

        // 3. Also check shop's play history for songs matching this exact BPM & Genre/Era
        if (candidates.size() < maxTake && cp.targetBpm > 0) {
            QSqlQuery q(db);
            q.prepare("SELECT s.id, s.name, s.artist, s.keyname, s.tempo, s.songtype, s.lyrics, s.path "
                      "FROM (SELECT songtype, path, COUNT(*) AS c FROM history GROUP BY songtype, path ORDER BY c DESC LIMIT 120) h "
                      "JOIN songs s ON s.songtype = h.songtype AND s.path = h.path "
                      "WHERE s.tempo = 0 OR s.tempo BETWEEN ? AND ?");
            q.bindValue(0, minBpm);
            q.bindValue(1, maxBpm);
            collectQuery(q, 10);
        }

        // Shuffle slightly among top candidates, then sort by contextScore descending
        for (int i = candidates.size() - 1; i > 0; --i)
            std::swap(candidates[i], candidates[QRandomGenerator::global()->bounded(i + 1)]);
        std::stable_sort(candidates.begin(), candidates.end(), [](const Found &a, const Found &b) {
            return a.contextScore > b.contextScore;
        });
        if (candidates.size() > maxTake)
            candidates.resize(maxTake);
        return candidates;
    }

    static bool hasThai(const QString &s)
    {
        for (const QChar c : s)
            if (c.unicode() >= 0x0E00 && c.unicode() <= 0x0E7F)
                return true;
        return false;
    }

    // clips -> songs to play (library + YouTube scored by 4D context), on a worker thread
    void prepare(const QVector<YtItem> &clips, const QVector<Seed> &sd, const QString &modeHint)
    {
        const ContextProfile baseCp = buildContext(sd);
        currentCtx = baseCp;
        int thai = 0;
        for (const Seed &s : sd)
            thai += hasThai(s.name + s.artist) ? 1 : 0;
        const int lang = baseCp.lang != 0 ? baseCp.lang
                       : (sd.size() < 3 ? 0 : (thai * 10 >= sd.size() * 8 ? 1 : (thai * 10 <= sd.size() * 2 ? 2 : 0)));
        QSet<QString> sungKeys;     // not the songs the customers sang tonight
        QSet<QString> sungTitles;   // ... nor another copy of them (same title, other file / artist)
        for (const Seed &s : sd) {
            if (!s.key.isEmpty())
                sungKeys.insert(s.key);
            const QString k = titleKey(s.name);
            if (!k.isEmpty())
                sungTitles.insert(k);
        }
        const bool fastLocalOnly = (modeHint == QLatin1String("local-fast"));
        if (clips.isEmpty() && !fastLocalOnly && !baseCp.hasSeed)
            say("nothing found");
        const QString file = dbFile, st = style;
        std::weak_ptr<int> guard = alive;
        QPointer<QObject> owner = ctx;
        IdleSmart *self = this;
        QThreadPool::globalInstance()->start([clips, sd, baseCp, fastLocalOnly, file, st, lang, sungKeys, sungTitles, guard, owner, self]() {
            QVector<Found> found;
            ContextProfile cp = baseCp;
            {
                const QString conn = "idle-smart-" + QUuid::createUuid().toString();
                {
                    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
                    db.setDatabaseName(file);
                    db.setConnectOptions("QSQLITE_OPEN_READONLY");
                    const bool open = !file.isEmpty() && db.open();
                    if (open && cp.hasSeed)
                        refineContextFromDb(db, sd, &cp);

                    const QString tag = formatContextTag(cp);

                    // Process YouTube clips and cross-match with library + 4D context score
                    for (const YtItem &it : clips) {
                        if (lang == 1 && !hasThai(it.title))
                            continue;
                        if (lang == 2 && hasThai(it.title))
                            continue;
                        if (sungKeys.contains("YOUTUBE" + it.id))
                            continue;
                        Found f;
                        f.yt = it;
                        f.contextTag = tag;
                        if (open && findInLibrary(db, it, &f)) {
                            f.local = true;
                            // If customer sang YouTube clip, keep YouTube playback if library twin is MIDI and they prefer MV
                            if (cp.mediaKind == MediaYouTube && classifyMediaKind(f.type) == MediaMidi)
                                f.local = false;
                        }
                        if (f.local && sungKeys.contains(f.type + f.path))
                            continue;
                        if (titleClash(f.local ? f.name : it.title, !f.local, sungTitles))
                            continue;

                        const QString evalName = f.local ? f.name : it.title;
                        const QString evalArtist = f.local ? f.artist : it.channel;
                        const QString evalType = f.local ? f.type : QStringLiteral("YOUTUBE");
                        const int sc = scoreCandidate(evalName, evalArtist, f.lyrics, evalType, f.tempo, f.local, cp, st);
                        if (sc <= -500)             // a clear clash (tempo jump / wrong genre family)
                            continue;
                        f.contextScore = sc + 10;   // slight boost for fresh YouTube/cross-matched hits
                        found << f;
                    }

                    // Also query context-matched library songs (for immediate availability, offline resilience,
                    // and exact BPM/Genre/Era matching when customer sang MIDI or local files)
                    if (open && cp.hasSeed && st == "auto") {
                        const int wantLib = fastLocalOnly ? 8 : (cp.mediaKind == MediaMidi ? 6 : 4);
                        const QVector<Found> libMatches = queryLibraryByContext(db, cp, sungKeys, sungTitles, st, wantLib);
                        found += libMatches;
                    }
                    db.close();
                }
                QSqlDatabase::removeDatabase(conn);
            }

            // Sort by 4D context score (highest match first)
            std::stable_sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
                return a.contextScore > b.contextScore;
            });

            QMetaObject::invokeMethod(qApp, [found, cp, fastLocalOnly, guard, owner, self]() {
                if (guard.expired() || !owner)
                    return;
                if (!fastLocalOnly)
                    self->busy = false;
                if (cp.hasSeed)
                    self->currentCtx = cp;
                QVector<IdlePick> picks;
                int fromLib = 0;
                QSet<QString> existingKeys;
                for (const IdlePick &r : self->ready) {
                    if (!r.libKey.isEmpty()) existingKeys.insert(r.libKey);
                    if (!r.yt.id.isEmpty())  existingKeys.insert("YT:" + r.yt.id);
                    const QString t = titleKey(pickTitle(r));
                    if (!t.isEmpty()) existingKeys.insert("T:" + t);
                }
                for (const Found &f : found) {
                    IdlePick p;
                    p.yt = f.yt;
                    p.local = f.local;
                    p.contextTag = f.contextTag;
                    if (!f.path.isEmpty()) {
                        p.song.setId(f.id);
                        p.song.setName(f.name);
                        p.song.setArtist(f.artist);
                        p.song.setKey(f.key);
                        p.song.setTempo(f.tempo);
                        p.song.setSongType(f.type);
                        p.song.setLyrics(f.lyrics);
                        p.song.setPath(f.path);
                        p.song.setBpmSpeed(0);
                        p.song.setTranspose(0);
                        p.libKey = f.type + f.path;
                    }
                    if (!p.libKey.isEmpty() && existingKeys.contains(p.libKey))
                        continue;
                    if (!p.yt.id.isEmpty() && existingKeys.contains("YT:" + p.yt.id))
                        continue;
                    const QString pt = titleKey(pickTitle(p));
                    if (!pt.isEmpty() && existingKeys.contains("T:" + pt))
                        continue;
                    if (!pt.isEmpty()) existingKeys.insert("T:" + pt);
                    if (!p.libKey.isEmpty()) existingKeys.insert(p.libKey);
                    if (!p.yt.id.isEmpty())  existingKeys.insert("YT:" + p.yt.id);
                    fromLib += p.local ? 1 : 0;
                    picks << p;
                }
                self->ready = picks + self->ready;      // highest-scoring context matches first
                if (self->ready.size() > 30)
                    self->ready.resize(30);
                self->say(QString("ready %1 songs (%2 from library) [%3]")
                              .arg(picks.size()).arg(fromLib).arg(formatContextTag(cp)));
            }, Qt::QueuedConnection);
        });
    }
};

#endif // IDLESMART_H
