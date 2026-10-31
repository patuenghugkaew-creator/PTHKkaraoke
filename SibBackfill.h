// Fills in the lyrics preview and the real tempo of SIB songs. The fast SIB import reads the
// collection's index file, which has no lyrics and no tempo (every song got 120 BPM and an
// empty lyrics preview). This reads each .sib in the background (several at a time), with its
// own database connection, committing in batches, so it can stop and resume at any time.
#ifndef SIBBACKFILL_H
#define SIBBACKFILL_H

#include "PTHKCore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTextCodec>
#include <QRegExp>
#include <QUuid>
#include <QVariant>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

class SibBackfill
{
public:
    ~SibBackfill() { stop(); }

    // called from the worker thread: done / total
    std::function<void(int done, int total)> onProgress;
    std::function<void(int updated)> onFinished;

    bool running() const { return worker.joinable() && !finished.load(); }

    void start(const QString &dbFile, const QString &sibDir)
    {
        if (worker.joinable())
            return;
        cancel = false;
        finished = false;
        worker = std::thread([this, dbFile, sibDir]() { run(dbFile, sibDir); finished = true; });
    }

    void stop()
    {
        cancel = true;
        if (worker.joinable())
            worker.join();
    }

    // first lines of the lyrics, as the search box shows them (same as SongDatabase::insertSIB)
    static QString lyricsPreview(const QByteArray &lyr)
    {
        QTextCodec *tis620 = QTextCodec::codecForName("TIS-620");
        if (!tis620) tis620 = QTextCodec::codecForName("Windows-874");
        const QString text = tis620 ? tis620->toUnicode(lyr) : QString::fromLatin1(lyr);
        const QStringList lines = text.split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
        QString s;
        for (int j = 0; j < qMin(5, lines.size()); ++j)
            s += lines[j] + " ";
        return s.trimmed();
    }

    // first Set Tempo of the MIDI (FF 51 03 tt tt tt), 0 if none
    static int midiBpm(const QByteArray &mid)
    {
        const int i = mid.indexOf(QByteArray("\xFF\x51\x03", 3));
        if (i < 0 || i + 6 > mid.size())
            return 0;
        const quint32 us = (quint32(quint8(mid[i + 3])) << 16) | (quint32(quint8(mid[i + 4])) << 8) | quint8(mid[i + 5]);
        return us ? int(60000000.0 / us + 0.5) : 0;
    }

private:
    struct Row { QString path; QString lyr; int bpm = 0; bool ok = false; };

    void run(const QString &dbFile, const QString &sibDir)
    {
        const QString conn = "sibfill-" + QUuid::createUuid().toString();
        int updated = 0;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
            db.setDatabaseName(dbFile);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000");   // wait instead of failing while the player writes
            if (!db.open()) {
                if (onFinished) onFinished(0);
                return;
            }
            QStringList paths;
            {
                QSqlQuery q(db);
                q.exec("SELECT DISTINCT path FROM songs WHERE songtype = 'SIB' AND (lyrics IS NULL OR lyrics = '')");
                while (q.next())
                    paths << q.value(0).toString();
            }
            const int total = paths.size();
            const int nThreads = qBound(2, int(std::thread::hardware_concurrency()) / 2, 8);
            const int batch = 600;
            for (int from = 0; from < total && !cancel; from += batch) {
                const int n = qMin(batch, total - from);
                std::vector<Row> rows(static_cast<size_t>(n));
                std::atomic<int> next{0};
                std::vector<std::thread> pool;
                for (int t = 0; t < nThreads; ++t) {
                    pool.emplace_back([&]() {
                        for (int k = next++; k < n && !cancel; k = next++) {
                            Row &r = rows[size_t(k)];
                            r.path = paths[from + k];
                            const PTHKCore::SibSong s = PTHKCore::readSib(sibDir + r.path);
                            if (!s.valid || s.lyrData.isEmpty())
                                continue;
                            r.lyr = lyricsPreview(s.lyrData);
                            r.bpm = midiBpm(s.midData);
                            r.ok = !r.lyr.isEmpty();
                        }
                    });
                }
                for (std::thread &t : pool)
                    t.join();
                db.transaction();
                QSqlQuery up(db);
                up.prepare("UPDATE songs SET lyrics = ?, tempo = CASE WHEN ? > 0 THEN ? ELSE tempo END "
                           "WHERE songtype = 'SIB' AND path = ?");
                for (const Row &r : rows) {
                    if (r.path.isEmpty()) continue;     // not reached (stopped)
                    // unreadable / missing file: a one-space preview marks it as tried
                    up.bindValue(0, r.ok ? r.lyr : QStringLiteral(" "));
                    up.bindValue(1, r.bpm);
                    up.bindValue(2, r.bpm);
                    up.bindValue(3, r.path);
                    if (up.exec()) ++updated;
                }
                up.finish();
                db.commit();
                if (onProgress) onProgress(from + n, total);
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(conn);
        if (onFinished) onFinished(updated);
    }

    std::thread worker;
    std::atomic<bool> cancel{false}, finished{false};
};

#endif // SIBBACKFILL_H
