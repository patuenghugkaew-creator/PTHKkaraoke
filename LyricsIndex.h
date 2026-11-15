// Full lyrics of every NCN / EMK / SIB song, for "ค้นจากเนื้อร้อง" (Tab in the song search): find a
// song by any line the singer remembers, e.g. the chorus. The song database only keeps the first
// ~80 characters of each song's lyrics.
//
// Lyrics.db3 (next to Database.db3):
//   texts(id, md5, body)  - the lyrics with spaces / punctuation taken out, one byte a character
//                           (Thai as TIS-620), each different text once (the same song often comes
//                           as NCN, EMK and SIB)
//   songs(songtype, path, text_id)
// Built in the background (several files read at a time, committed in batches, resumes where it
// stopped); each start adds only the songs that are new. Searching scans the bodies with instr().
#ifndef LYRICSINDEX_H
#define LYRICSINDEX_H

#include "PTHKCore.h"

#include <QTextCodec>
#include <QCryptographicHash>
#include <QFile>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <QVariant>
#include <QVector>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

class LyricsIndex
{
public:
    ~LyricsIndex() { stop(); }

    // called from the worker thread
    std::function<void(int done, int total)> onProgress;
    std::function<void(int added)> onFinished;

    bool running() const { return worker.joinable() && !finished.load(); }
    int progressPercent() const { return pct.load(); }

    // songDb: Database.db3; the song folders as the song database has them
    void start(const QString &lyricsDb, const QString &songDb, const QString &ncnDir, const QString &emkDir, const QString &sibDir)
    {
        if (worker.joinable())
            return;
        cancel = false;
        finished = false;
        worker = std::thread([=]() { run(lyricsDb, songDb, ncnDir, emkDir, sibDir); finished = true; });
    }
    void stop()
    {
        cancel = true;
        if (worker.joinable())
            worker.join();
    }
    void wait()                 // until it is done (without stopping it)
    {
        if (worker.joinable())
            worker.join();
    }

    // text as stored and searched: letters, digits and Thai marks only, Latin in lower case,
    // one byte each (Thai U+0E01..U+0E5B -> TIS-620 0xA1..0xFB)
    static QByteArray normalize(const QString &text)
    {
        QByteArray out;
        out.reserve(text.size());
        for (const QChar c : text) {
            const ushort u = c.unicode();
            if (u >= 0x0E01 && u <= 0x0E5B)
                out += char(0xA0 + (u - 0x0E00));
            else if (u < 0x80 && c.isLetterOrNumber())
                out += char(c.toLower().unicode());
        }
        return out;
    }

    // songs whose lyrics have this text: (songtype, path), at most `limit` different lyrics.
    // Any thread (its own connection). Empty when the index is not there yet.
    static QVector<QPair<QString, QString>> find(const QString &lyricsDb, const QString &text, int limit = 300)
    {
        QVector<QPair<QString, QString>> out;
        const QByteArray key = normalize(text);
        if (key.size() < 3)
            return out;
        const QString conn = "lyrfind-" + QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
            db.setDatabaseName(lyricsDb);
            db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=3000");
            if (db.open()) {
                QStringList ids;
                {
                    QSqlQuery q(db);
                    q.prepare("SELECT id FROM texts WHERE instr(body, ?) > 0 LIMIT ?");
                    q.bindValue(0, key);
                    q.bindValue(1, limit);
                    if (q.exec())
                        while (q.next())
                            ids << q.value(0).toString();
                }
                if (!ids.isEmpty()) {
                    QSqlQuery q(db);
                    if (q.exec("SELECT songtype, path FROM songs WHERE text_id IN (" + ids.join(',') + ")"))
                        while (q.next())
                            out.append({ q.value(0).toString(), q.value(1).toString() });
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        return out;
    }

    // how many songs are in the index (0 = not built yet)
    static int songCount(const QString &lyricsDb)
    {
        if (!QFile::exists(lyricsDb))
            return 0;
        int n = 0;
        const QString conn = "lyrcount-" + QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
            db.setDatabaseName(lyricsDb);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            if (db.open()) {
                QSqlQuery q(db);
                if (q.exec("SELECT count(*) FROM songs") && q.next())
                    n = q.value(0).toInt();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        return n;
    }

private:
    struct Job { QString type, path; QByteArray body; };

    static QByteArray readLyrics(const Job &j, const QString &ncnDir, const QString &emkDir, const QString &sibDir)
    {
        auto firstThere = [](const QStringList &c) {
            for (const QString &p : c)
                if (QFile::exists(p))
                    return p;
            return QString();
        };
        QByteArray raw;
        if (j.type == "NCN") {
            // <NCN>/Song/x.mid -> <NCN>/Lyrics/x.lyr (any case of the extension)
            QString lyr = ncnDir + j.path;
            lyr.replace("/Song/", "/Lyrics/");
            lyr.chop(4);
            const QString f = firstThere({ lyr + ".LYR", lyr + ".lyr", lyr + ".Lyr" });
            QFile file(f);
            if (!f.isEmpty() && file.open(QIODevice::ReadOnly))
                raw = file.readAll();
        } else if (j.type == "EMK") {
            const QString f = firstThere({ emkDir + j.path, emkDir + "/" + j.path });
            if (!f.isEmpty()) {
                const PTHKCore::EmkSong s = PTHKCore::readEmk(f);
                if (s.valid)
                    raw = s.lyrData;
            }
        } else if (j.type == "SIB") {
            const QString f = firstThere({ sibDir + j.path, sibDir + "/" + j.path });
            if (!f.isEmpty()) {
                const PTHKCore::SibSong s = PTHKCore::readSib(f);
                if (s.valid)
                    raw = s.lyrData;
            }
        }
        return raw.isEmpty() ? QByteArray() : normalize(decodeThai(raw));
    }

    // lyrics files are TIS-620 / Windows-874, or UTF-8 in newer packs (as Utils::decodeThaiText)
    static QString decodeThai(const QByteArray &data)
    {
        if (data.startsWith("\xEF\xBB\xBF"))
            return QString::fromUtf8(data.mid(3));
        QTextCodec::ConverterState state;
        const QString utf8 = QTextCodec::codecForName("UTF-8")->toUnicode(data.constData(), data.size(), &state);
        if (state.invalidChars == 0)
            return utf8;
        QString out;
        out.reserve(data.size());
        for (const char ch : data) {
            const uchar b = uchar(ch);
            out += b < 0x80 ? QChar(b) : (b >= 0xA1 ? QChar(0x0E00 + (b - 0xA0)) : QChar(' '));
        }
        return out;
    }

    void run(const QString &lyricsDb, const QString &songDb, const QString &ncnDir, const QString &emkDir, const QString &sibDir)
    {
        const QString connL = "lyrindex-" + QUuid::createUuid().toString();
        const QString connS = "lyrsongs-" + QUuid::createUuid().toString();
        int added = 0;
        {
            QSqlDatabase ldb = QSqlDatabase::addDatabase("QSQLITE", connL);
            ldb.setDatabaseName(lyricsDb);
            ldb.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000");
            QSqlDatabase sdb = QSqlDatabase::addDatabase("QSQLITE", connS);
            sdb.setDatabaseName(songDb);
            sdb.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=10000");
            if (ldb.open() && sdb.open()) {
                QSqlQuery q(ldb);
                q.exec("PRAGMA journal_mode=WAL");      // the search can read while this writes
                q.exec("CREATE TABLE IF NOT EXISTS texts (id INTEGER PRIMARY KEY, md5 BLOB UNIQUE, body BLOB)");
                q.exec("CREATE TABLE IF NOT EXISTS songs (songtype TEXT, path TEXT, text_id INTEGER, PRIMARY KEY (songtype, path))");

                QSet<QString> have;                     // songs already in the index
                q.exec("SELECT songtype, path FROM songs");
                while (q.next())
                    have.insert(q.value(0).toString() + '|' + q.value(1).toString());

                std::vector<Job> todo;
                {
                    QSqlQuery s(sdb);
                    s.exec("SELECT DISTINCT songtype, path FROM songs WHERE songtype IN ('NCN', 'EMK', 'SIB')");
                    while (s.next() && !cancel) {
                        Job j{ s.value(0).toString(), s.value(1).toString(), QByteArray() };
                        if (!have.contains(j.type + '|' + j.path))
                            todo.push_back(j);
                    }
                }
                const int total = int(todo.size());
                const int nThreads = qBound(2, int(std::thread::hardware_concurrency()) / 2, 6);
                const int batch = 500;
                for (int from = 0; from < total && !cancel; from += batch) {
                    const int n = qMin(batch, total - from);
                    std::atomic<int> next{ from };
                    std::vector<std::thread> readers;
                    for (int t = 0; t < nThreads; ++t)
                        readers.emplace_back([&]() {
                            for (int i = next++; i < from + n && !cancel; i = next++)
                                todo[size_t(i)].body = readLyrics(todo[size_t(i)], ncnDir, emkDir, sibDir);
                        });
                    for (std::thread &r : readers)
                        r.join();
                    if (cancel)
                        break;

                    ldb.transaction();
                    QSqlQuery addText(ldb), findText(ldb), addSong(ldb);
                    addText.prepare("INSERT OR IGNORE INTO texts (md5, body) VALUES (?, ?)");
                    findText.prepare("SELECT id FROM texts WHERE md5 = ?");
                    addSong.prepare("INSERT OR REPLACE INTO songs (songtype, path, text_id) VALUES (?, ?, ?)");
                    for (int i = from; i < from + n; ++i) {
                        Job &j = todo[size_t(i)];
                        qint64 textId = 0;              // 0 = no lyrics (instrumental, unreadable): not tried again
                        if (j.body.size() >= 8) {
                            const QByteArray md5 = QCryptographicHash::hash(j.body, QCryptographicHash::Md5);
                            addText.bindValue(0, md5);
                            addText.bindValue(1, j.body);
                            addText.exec();
                            findText.bindValue(0, md5);
                            if (findText.exec() && findText.next())
                                textId = findText.value(0).toLongLong();
                            findText.finish();
                        }
                        addSong.bindValue(0, j.type);
                        addSong.bindValue(1, j.path);
                        addSong.bindValue(2, textId);
                        addSong.exec();
                        j.body.clear();
                        ++added;
                    }
                    ldb.commit();
                    pct.store(total > 0 ? int(100.0 * (from + n) / total) : 100);
                    if (onProgress)
                        onProgress(from + n, total);
                }
                if (total == 0)
                    pct.store(100);
            }
            ldb.close();
            sdb.close();
        }
        QSqlDatabase::removeDatabase(connL);
        QSqlDatabase::removeDatabase(connS);
        if (onFinished)
            onFinished(added);
    }

    std::thread worker;
    std::atomic<bool> cancel{ false };
    std::atomic<bool> finished{ false };
    std::atomic<int> pct{ 0 };
};

#endif // LYRICSINDEX_H
