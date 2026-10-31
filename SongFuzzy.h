// Approximate song search, used when the exact search finds nothing.
// Names and artists are reduced to a "key": no spaces, punctuation, bracketed parts, Thai tone
// marks / thanthakhat, and sound-alike letters folded (ใ=ไ, ศ/ษ=ส, ณ=น, ธ=ท, ภ=พ ...).
// A query matches when its key is inside a song's key, or - for typos - when it differs from some
// part of the song's key by a few letters (edit distance, the match may start anywhere).
// The index (row id + keys) is built from the database in a background thread at start-up.
#ifndef SONGFUZZY_H
#define SONGFUZZY_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QUuid>
#include <QVariant>
#include <QVector>
#include <QReadWriteLock>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

class SongFuzzy
{
public:
    ~SongFuzzy() { if (worker.joinable()) worker.join(); }

    static QString key(const QString &s)
    {
        QString out;
        out.reserve(s.size());
        int depth = 0;
        for (QChar c : s) {
            ushort u = c.unicode();
            if (u == '(' || u == '[' || u == '{') { ++depth; continue; }
            if (u == ')' || u == ']' || u == '}') { if (depth) --depth; continue; }
            if (depth)
                continue;
            if (c.isSpace() || c.isPunct() || c.isSymbol())
                continue;
            if ((u >= 0x0E47 && u <= 0x0E4E) || u == 0x0E3A)   // tone marks, ็ ์ ํ ๆ ๎ ฺ
                continue;
            switch (u) {                                         // sound-alike letters
            case 0x0E43: u = 0x0E44; break;                      // ใ -> ไ
            case 0x0E28: case 0x0E29: u = 0x0E2A; break;         // ศ ษ -> ส
            case 0x0E13: u = 0x0E19; break;                      // ณ -> น
            case 0x0E20: u = 0x0E1E; break;                      // ภ -> พ
            case 0x0E18: case 0x0E11: case 0x0E12: u = 0x0E17; break;   // ธ ฑ ฒ -> ท
            case 0x0E0E: u = 0x0E14; break;                      // ฎ -> ด
            case 0x0E0F: u = 0x0E15; break;                      // ฏ -> ต
            case 0x0E10: u = 0x0E16; break;                      // ฐ -> ถ
            case 0x0E2C: u = 0x0E25; break;                      // ฬ -> ล
            case 0x0E0C: u = 0x0E0A; break;                      // ฌ -> ช
            case 0x0E06: u = 0x0E04; break;                      // ฆ -> ค
            case 0x0E1D: u = 0x0E1F; break;                      // ฝ -> ฟ
            default: break;
            }
            out += QChar(u).toLower();
        }
        return out;
    }

    void build(const QString &dbFile)
    {
        if (worker.joinable())
            worker.join();
        worker = std::thread([this, dbFile]() {
            std::vector<Entry> list;
            const QString conn = "fuzzy-" + QUuid::createUuid().toString();
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
                db.setDatabaseName(dbFile);
                db.setConnectOptions("QSQLITE_OPEN_READONLY");
                if (db.open()) {
                    QSqlQuery q(db);
                    q.setForwardOnly(true);
                    q.exec("SELECT rowid, name, artist FROM songs");
                    while (q.next()) {
                        Entry e;
                        e.rowid = q.value(0).toLongLong();
                        e.name = key(q.value(1).toString());
                        e.artist = key(q.value(2).toString());
                        charMask(e.name, e.mName);
                        charMask(e.artist, e.mArtist);
                        if (!e.name.isEmpty())
                            list.push_back(std::move(e));
                    }
                    q.finish();
                    db.close();
                }
            }
            QSqlDatabase::removeDatabase(conn);
            QWriteLocker lock(&rw);
            entries.swap(list);
            ready = true;
        });
    }

    bool isReady() const { return ready.load(); }

    // row ids of the best matches (best first), empty if nothing is close enough
    QVector<qint64> find(const QString &text, int maxResults = 300) const
    {
        QVector<qint64> res;
        const QString q = key(text);
        if (q.size() < 2 || !ready)
            return res;
        QReadLocker lock(&rw);

        struct Hit { qint64 rowid; int score; int lenDiff; };
        std::vector<Hit> hits;

        // 1. the key is inside the name (score 0 at the start, 1 elsewhere) or the artist (2)
        for (const Entry &e : entries) {
            const int at = e.name.indexOf(q);
            if (at >= 0)
                hits.push_back({ e.rowid, at == 0 ? 0 : 1, int(e.name.size()) - int(q.size()) });
            else if (e.artist.contains(q))
                hits.push_back({ e.rowid, 2, int(e.artist.size()) - int(q.size()) });
        }

        // 2. typos: a few letters different from some part of the name (or the artist)
        // (very short texts would match almost anything, so they need an exact part)
        if (hits.empty() && q.size() >= 4) {
            const int k = q.size() <= 6 ? 1 : (q.size() <= 10 ? 2 : 3);
            std::vector<int> prev(size_t(q.size()) + 1), cur(size_t(q.size()) + 1);
            auto dist = [&](const QString &t) {
                // semi-global edit distance: q may match any part of t
                const int m = q.size();
                for (int i = 0; i <= m; ++i) prev[size_t(i)] = i;
                int best = prev[size_t(m)];
                const QChar *qp = q.constData(), *tp = t.constData();
                for (int j = 1; j <= t.size(); ++j) {
                    cur[0] = 0;
                    int rowMin = 0;
                    for (int i = 1; i <= m; ++i) {
                        const int sub = prev[size_t(i - 1)] + (qp[i - 1] == tp[j - 1] ? 0 : 1);
                        const int del = prev[size_t(i)] + 1, ins = cur[size_t(i - 1)] + 1;
                        cur[size_t(i)] = (std::min)(sub, (std::min)(del, ins));
                        rowMin = (std::min)(rowMin, cur[size_t(i)]);
                    }
                    best = (std::min)(best, cur[size_t(m)]);
                    std::swap(prev, cur);
                    Q_UNUSED(rowMin);
                }
                return best;
            };
            quint64 qm[2];
            charMask(q, qm);
            const int need = popcnt(qm[0]) + popcnt(qm[1]) - k;    // letters that must be there
            auto enough = [&](const quint64 m[2]) {
                return popcnt(qm[0] & m[0]) + popcnt(qm[1] & m[1]) >= need;
            };
            for (const Entry &e : entries) {
                if (e.name.size() + k < q.size() && e.artist.size() + k < q.size())
                    continue;
                const bool nameOk = enough(e.mName), artistOk = enough(e.mArtist);
                if (!nameOk && !artistOk)
                    continue;               // too few of the letters: can't be within k edits
                int d = nameOk ? dist(e.name) : k + 1;
                int lenDiff = int(e.name.size()) - int(q.size());
                if (d > k && artistOk && !e.artist.isEmpty()) {
                    const int da = dist(e.artist);
                    if (da < d) { d = da + 1; lenDiff = int(e.artist.size()) - int(q.size()); }
                }
                if (d <= k)
                    hits.push_back({ e.rowid, 10 + d, lenDiff });
            }
        }

        std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
            if (a.score != b.score) return a.score < b.score;
            return std::abs(a.lenDiff) < std::abs(b.lenDiff);
        });
        for (const Hit &h : hits) {
            res.append(h.rowid);
            if (res.size() >= maxResults)
                break;
        }
        return res;
    }

private:
    struct Entry { qint64 rowid = 0; QString name, artist; quint64 mName[2] = {0, 0}, mArtist[2] = {0, 0}; };

    // which letters occur (Thai block and ASCII share the low 7 bits; a superset is fine)
    static void charMask(const QString &s, quint64 m[2])
    {
        m[0] = m[1] = 0;
        for (QChar c : s) {
            const int b = c.unicode() & 0x7F;
            m[b >> 6] |= quint64(1) << (b & 63);
        }
    }
    static int popcnt(quint64 v) { int n = 0; while (v) { v &= v - 1; ++n; } return n; }
    std::vector<Entry> entries;
    mutable QReadWriteLock rw;
    std::atomic<bool> ready{false};
    std::thread worker;
};

#endif // SONGFUZZY_H
