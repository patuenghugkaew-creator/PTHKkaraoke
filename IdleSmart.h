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
};

class IdleSmart
{
public:
    struct Seed { QString name, artist, ytId, key; };     // a song the customers sang

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
        return { QObject::tr("อัตโนมัติ: เพลงติดกระแสทุกแนว (ลูกทุ่ง หมอลำ เพื่อชีวิต สตริง)"),
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

    // A customer song started: the next refill follows it (its Mix).
    void customerSang() { stale = true; }

    // Starts fetching when the prepared list is short or old (cheap to call every second).
    void refill()
    {
        if (busy || ytdlp().isEmpty())
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastRefill < (ready.size() < 3 ? 20000 : 120000))
            return;                                     // not too often (a refill is 1-3 YouTube requests)
        if (ready.size() >= 6 && !stale)
            return;
        lastRefill = now;
        stale = false;
        busy = true;
        startRefill();
    }

    // The next song to play; false when none is ready (the caller plays a library song instead).
    // fileOk: does the library song's file exist (MainWindow::songFilePath).
    // allowYouTube false (no internet): library songs only; the YouTube ones stay for later.
    bool next(IdlePick *out, const std::function<bool(Song)> &fileOk, bool allowYouTube = true)
    {
        if (!allowYouTube) {
            for (int i = 0; i < ready.size(); ++i) {
                IdlePick p = ready.at(i);
                if (!p.local)
                    continue;
                ready.removeAt(i--);
                if (wasPlayed(p) || !fileOk(p.song))
                    continue;
                markPlayed(p);
                *out = p;
                return true;
            }
            return false;
        }
        while (!ready.isEmpty()) {
            // not always the first: a little variety, mostly in the order YouTube gave
            const int i = QRandomGenerator::global()->bounded(qMin(3, ready.size()));
            IdlePick p = ready.takeAt(i);
            if (wasPlayed(p))
                continue;
            if (p.local && !fileOk(p.song)) {
                if (p.yt.id.isEmpty())
                    continue;
                p.local = false;                        // the file is gone: from YouTube
            }
            markPlayed(p);
            *out = p;
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

private:
    QObject *ctx;
    std::shared_ptr<int> alive;         // the worker's result is dropped once this object is gone
    QString style = "auto";
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

    bool wasPlayed(const IdlePick &p) const
    {
        const qint64 limit = QDateTime::currentMSecsSinceEpoch() - 4LL * 3600 * 1000;   // 4 hours
        return (!p.libKey.isEmpty() && played.value("lib:" + p.libKey, 0) > limit)
            || (!p.yt.id.isEmpty() && played.value("yt:" + p.yt.id, 0) > limit);
    }
    void markPlayed(const IdlePick &p)
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!p.libKey.isEmpty())
            played.insert("lib:" + p.libKey, now);
        if (!p.yt.id.isEmpty())
            played.insert("yt:" + p.yt.id, now);
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
            const bool trendAll = (style == "auto");
            auto afterChart = [this, sd, trendAll]() {
                if (!trendAll) {
                    say(QString("chart only (%1 clips)").arg(chart.size()));
                    prepare(sample(chart, 12), {}, QString());
                    return;
                }
                trending(sd);
            };
            if (chartOld) {
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

    // Songs in fashion now, of every style the shop sings (the owner's choice, 2026-11-08): YouTube
    // searches limited to this year (this month for the newest luk thung), sorted by views, plus
    // YouTube's Thai chart for string / pop. Each search is kept 12 hours. (Searching the customers'
    // singers gave their old hits; the owner wants what is popular now.)
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

    // ---- library lookup (worker thread) -------------------------------------------------------------
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
    // a library song found for a clip, as plain values (a Song is a QObject: made on the UI thread)
    struct Found {
        YtItem yt;
        bool local = false;
        QString id, name, artist, key, type, lyrics, path;
        int tempo = 0;
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

    static bool hasThai(const QString &s)
    {
        for (const QChar c : s)
            if (c.unicode() >= 0x0E00 && c.unicode() <= 0x0E7F)
                return true;
        return false;
    }

    // clips -> songs to play (library first), on a worker thread; then into the prepared list
    void prepare(const QVector<YtItem> &clips, const QVector<Seed> &sd, const QString &)
    {
        // the customers sing Thai songs (or not): keep to that (the chart has K-pop / English too)
        int thai = 0;
        for (const Seed &s : sd)
            thai += hasThai(s.name + s.artist) ? 1 : 0;
        const int lang = sd.size() < 3 ? 0 : (thai * 10 >= sd.size() * 8 ? 1 : (thai * 10 <= sd.size() * 2 ? 2 : 0));
        QSet<QString> sungKeys;     // not the songs the customers sang tonight
        for (const Seed &s : sd)
            if (!s.key.isEmpty())
                sungKeys.insert(s.key);
        if (clips.isEmpty())
            say("nothing found");
        const QString file = dbFile, st = style;
        std::weak_ptr<int> guard = alive;
        QPointer<QObject> owner = ctx;
        IdleSmart *self = this;
        QThreadPool::globalInstance()->start([clips, file, st, lang, sungKeys, guard, owner, self]() {
            QVector<Found> found;
            {
                const QString conn = "idle-smart-" + QUuid::createUuid().toString();
                {
                    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
                    db.setDatabaseName(file);
                    db.setConnectOptions("QSQLITE_OPEN_READONLY");
                    const bool open = !file.isEmpty() && db.open();
                    for (const YtItem &it : clips) {
                        if (lang == 1 && !hasThai(it.title))
                            continue;
                        if (lang == 2 && hasThai(it.title))
                            continue;
                        Found f;
                        f.yt = it;
                        if (open && findInLibrary(db, it, &f)) {
                            f.local = true;
                            // a slow / fast style: a library song of the wrong tempo plays from YouTube
                            if (f.tempo > 0 && ((st == "slow" && f.tempo >= 105) || (st == "fast" && f.tempo < 110)))
                                f.local = false;
                        }
                        if (f.local && sungKeys.contains(f.type + f.path))
                            continue;
                        if (sungKeys.contains("YOUTUBE" + it.id))
                            continue;
                        found << f;
                    }
                    db.close();
                }
                QSqlDatabase::removeDatabase(conn);
            }
            QMetaObject::invokeMethod(qApp, [found, guard, owner, self]() {
                if (guard.expired() || !owner)
                    return;
                self->busy = false;
                QVector<IdlePick> picks;
                int fromLib = 0;
                for (const Found &f : found) {
                    IdlePick p;
                    p.yt = f.yt;
                    p.local = f.local;
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
                    fromLib += p.local ? 1 : 0;
                    picks << p;
                }
                self->ready = picks + self->ready;      // the newest (tonight's style) first
                if (self->ready.size() > 30)
                    self->ready.resize(30);
                self->say(QString("ready %1 songs (%2 from the library)").arg(picks.size()).arg(fromLib));
            }, Qt::QueuedConnection);
        });
    }
};

#endif // IDLESMART_H
