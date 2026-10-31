#include <QTextCodec>
#include "PTHKCore.h"
#include "SongDatabase.h"
#include "SongFuzzy.h"

#include "Midi/MidiFile.h"
#include "Midi/HNKFile.h"
#include "Config.h"
#include "Utils.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSqlQuery>
#include <algorithm>
#include <QLocale>
#include <QHash>
#include <QCollator>
#include <QSet>
#include <QFileInfo>
#include <QTextStream>

void setSong(Song *s, QSqlQuery *qry);


SongDatabase::SongDatabase()
{
    song = new Song();
    searchType = SearchType::ByAll;

    bool hasDb = false;

    if (QFile::exists(Config::DATABASE_FILE_PATH)) {
        hasDb = true;
    } else {
        QDir dir(Config::DATABASE_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(Config::DATABASE_DIR_PATH);
    }

    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(Config::DATABASE_FILE_PATH);

    if (db.open()) {

        if (hasDb == false) {
            QString sql =
                    "CREATE TABLE songs ("
                        "id       TEXT    COLLATE NOCASE,"
                        "name     TEXT    COLLATE NOCASE,"
                        "artist   TEXT    COLLATE NOCASE,"
                        "keyname  TEXT,"
                        "tempo    INTEGER,"
                        "songtype TEXT,"
                        "lyrics   TEXT,"
                        "path     TEXT"
                    ")";

            QSqlQuery query(db);
            query.exec(sql);
            query.finish();
            query.clear();

            sql = "CREATE TABLE IF NOT EXISTS miscellaneous ("
                    "name TEXT,"
                    "value_str TEXT,"
                    "value_num INTEGER"
                  ")";

            query.exec(sql);
            query.finish();
            query.clear();

            createIndex();
        }
        db.close();
    }

    db.open();
    createListTables();
}

void SongDatabase::createListTables()
{
    if (!db.isOpen())
        return;

    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS favorites ("
           "songtype TEXT, path TEXT, added INTEGER, PRIMARY KEY (songtype, path))");
    q.exec("CREATE TABLE IF NOT EXISTS history ("
           "songtype TEXT, path TEXT, played INTEGER)");
    q.exec("CREATE INDEX IF NOT EXISTS history_idx ON history(songtype, path)");
    q.exec("CREATE INDEX IF NOT EXISTS type_path_idx ON songs(songtype, path)");
    q.finish();
}

void SongDatabase::removeSongFile(const QString &type, const QString &path)
{
    QSqlQuery q(db);
    q.prepare("DELETE FROM songs WHERE songtype = ? AND path = ?");
    q.bindValue(0, type);
    q.bindValue(1, path);
    q.exec();
    q.finish();
}

QString SongDatabase::songIdFor(const QString &type, const QString &path)
{
    QSqlQuery q(db);
    q.prepare("SELECT id, name FROM songs WHERE songtype = ? AND path = ?");
    q.bindValue(0, type);
    q.bindValue(1, path);
    QString id;
    if (q.exec() && q.next())
        id = q.value(0).toString() == "-------" ? q.value(1).toString() : q.value(0).toString();
    q.finish();
    return id;
}

void SongDatabase::addHistory(Song *s)
{
    if (!db.isOpen() || s->path().isEmpty())
        return;

    QSqlQuery q(db);
    q.prepare("INSERT INTO history VALUES (?, ?, ?)");
    q.bindValue(0, s->songType());
    q.bindValue(1, s->path());
    q.bindValue(2, QDateTime::currentSecsSinceEpoch());
    q.exec();
    q.finish();
}

bool SongDatabase::isFavorite(Song *s)
{
    QSqlQuery q(db);
    q.prepare("SELECT 1 FROM favorites WHERE songtype = ? AND path = ?");
    q.bindValue(0, s->songType());
    q.bindValue(1, s->path());
    bool fav = q.exec() && q.next();
    q.finish();
    return fav;
}

bool SongDatabase::toggleFavorite(Song *s)
{
    if (!db.isOpen() || s->path().isEmpty())
        return false;

    bool fav = isFavorite(s);
    QSqlQuery q(db);
    if (fav) {
        q.prepare("DELETE FROM favorites WHERE songtype = ? AND path = ?");
    } else {
        q.prepare("INSERT INTO favorites VALUES (?, ?, ?)");
        q.bindValue(2, QDateTime::currentSecsSinceEpoch());
    }
    q.bindValue(0, s->songType());
    q.bindValue(1, s->path());
    q.exec();
    q.finish();
    return !fav;
}

void SongDatabase::removeFromList(ListKind kind, Song s)
{
    if (!db.isOpen() || s.path().isEmpty())
        return;

    QSqlQuery q(db);
    if (kind == ListKind::Favorites) {
        q.prepare("DELETE FROM favorites WHERE songtype = ? AND path = ?");
    } else {
        q.prepare("DELETE FROM history WHERE songtype = ? AND path = ?");
    }
    q.bindValue(0, s.songType());
    q.bindValue(1, s.path());
    q.exec();
    q.finish();
}

void SongDatabase::clearList(ListKind kind)
{
    if (!db.isOpen())
        return;

    QSqlQuery q(db);
    if (kind == ListKind::Favorites) {
        q.exec("DELETE FROM favorites");
    } else {
        q.exec("DELETE FROM history");
    }
    q.finish();
}

QList<Song> SongDatabase::songList(ListKind kind, int limit)
{
    QString sql;
    switch (kind) {
    case ListKind::Favorites:
        sql = "SELECT s.* FROM favorites f JOIN songs s ON s.songtype = f.songtype AND s.path = f.path "
              "ORDER BY f.added DESC LIMIT ?";
        break;
    case ListKind::History:
        sql = "SELECT s.* FROM (SELECT songtype, path, MAX(played) AS p FROM history GROUP BY songtype, path) h "
              "JOIN songs s ON s.songtype = h.songtype AND s.path = h.path ORDER BY h.p DESC LIMIT ?";
        break;
    case ListKind::Top:
        sql = "SELECT s.* FROM (SELECT songtype, path, COUNT(*) AS c FROM history GROUP BY songtype, path) h "
              "JOIN songs s ON s.songtype = h.songtype AND s.path = h.path ORDER BY h.c DESC LIMIT ?";
        break;
    }

    QList<Song> list;
    QSqlQuery q(db);
    q.prepare(sql);
    q.bindValue(0, limit);
    if (q.exec()) {
        while (q.next()) {
            Song s;
            setSong(&s, &q);
            list.append(s);
        }
    }
    q.finish();
    return list;
}

QList<Song> SongDatabase::findSongs(const QString &text, int limit)
{
    QList<Song> list;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return list;
    Song byCode;
    if (songByCode(t, &byCode))
        list << byCode;
    QSqlQuery q(db);
    // exact code first, then names / artists starting with the text, then containing it
    q.prepare("SELECT * FROM songs WHERE id = :t OR name LIKE :c OR artist LIKE :c "
              "ORDER BY (id = :t) DESC, (name LIKE :s) DESC, (artist LIKE :s) DESC, name LIMIT :n");
    q.bindValue(":t", t);
    q.bindValue(":c", "%" + t + "%");
    q.bindValue(":s", t + "%");
    q.bindValue(":n", limit);
    if (q.exec()) {
        while (q.next()) {
            Song s;
            setSong(&s, &q);
            list.append(s);
        }
    }
    q.finish();
    return list;
}

bool SongDatabase::songByPath(const QString &type, const QString &path, Song *out)
{
    QSqlQuery q(db);
    q.prepare("SELECT * FROM songs WHERE songtype = ? AND path = ? LIMIT 1");
    q.bindValue(0, type);
    q.bindValue(1, path);
    bool ok = q.exec() && q.next();
    if (ok)
        setSong(out, &q);
    q.finish();
    return ok;
}

// ---- codes by artist ----------------------------------------------------------------------------

static void ensureCodeTables(QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS artist_codes (code INTEGER PRIMARY KEY, songtype TEXT, path TEXT, akey TEXT)");
    q.exec("CREATE INDEX IF NOT EXISTS artist_codes_song ON artist_codes(songtype, path)");
    q.exec("CREATE TABLE IF NOT EXISTS code_blocks (akey TEXT, start INTEGER, size INTEGER)");
}

static QString artistKey(const QString &artist)
{
    const QString a = artist.simplified().toLower();
    return a.isEmpty() ? QString::fromUtf8("ไม่ระบุศิลปิน") : a;
}

SongDatabase::CodeStats SongDatabase::artistCodeStats()
{
    ensureCodeTables(db);
    CodeStats st;
    QSqlQuery q(db);
    if (q.exec("SELECT COUNT(*), COUNT(DISTINCT akey), MIN(code), MAX(code) FROM artist_codes") && q.next()) {
        st.songs = q.value(0).toInt();
        st.artists = q.value(1).toInt();
        st.first = q.value(2).toLongLong();
        st.last = q.value(3).toLongLong();
    }
    return st;
}

SongDatabase::CodeStats SongDatabase::assignArtistCodes(qint64 start, int reserve, bool fresh)
{
    ensureCodeTables(db);
    CodeStats st;
    QSqlQuery q(db);
    db.transaction();
    if (fresh) {
        q.exec("DELETE FROM artist_codes");
        q.exec("DELETE FROM code_blocks");
    }

    // every song of the library: type|path -> (name, artist)
    struct S { QString type, path, name, artist; };
    QHash<QString, S> songs;
    q.exec("SELECT songtype, path, name, artist FROM songs");
    while (q.next()) {
        S s{ q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString() };
        songs.insert(s.type + "|" + s.path, s);
    }

    // codes given before; the ones whose song is gone are freed
    QSet<qint64> used;
    QSet<QString> coded;
    QList<qint64> gone;
    q.exec("SELECT code, songtype, path FROM artist_codes");
    while (q.next()) {
        const QString k = q.value(1).toString() + "|" + q.value(2).toString();
        if (songs.contains(k)) {
            used.insert(q.value(0).toLongLong());
            coded.insert(k);
        } else {
            gone << q.value(0).toLongLong();
        }
    }
    QSqlQuery del(db);
    del.prepare("DELETE FROM artist_codes WHERE code = ?");
    for (qint64 c : gone) {
        del.bindValue(0, c);
        del.exec();
    }
    st.removed = gone.size();

    // the runs of codes of every artist
    QHash<QString, QList<QPair<qint64, int>>> blocks;
    qint64 nextFree = start;
    q.exec("SELECT akey, start, size FROM code_blocks ORDER BY start");
    while (q.next()) {
        blocks[q.value(0).toString()] << qMakePair(q.value(1).toLongLong(), q.value(2).toInt());
        nextFree = qMax(nextFree, q.value(1).toLongLong() + q.value(2).toInt());
    }

    // songs without a code, by artist
    QHash<QString, QList<S>> todo;
    QHash<QString, QString> shownName;
    for (auto it = songs.constBegin(); it != songs.constEnd(); ++it)
        if (!coded.contains(it.key())) {
            const QString k = artistKey(it.value().artist);
            todo[k] << it.value();
            if (!shownName.contains(k))
                shownName.insert(k, it.value().artist.simplified());
        }

    QCollator thai(QLocale(QLocale::Thai, QLocale::Thailand));
    thai.setCaseSensitivity(Qt::CaseInsensitive);
    QStringList artists = todo.keys();
    std::sort(artists.begin(), artists.end(), [&](const QString &a, const QString &b) {
        const bool ha = blocks.contains(a), hb = blocks.contains(b);
        if (ha != hb) return ha;                    // artists with codes first (they fill their gaps)
        const QString unknown = artistKey(QString());
        if ((a == unknown) != (b == unknown)) return b == unknown;      // songs without an artist last
        return thai.compare(shownName.value(a), shownName.value(b)) < 0;
    });

    QSqlQuery ins(db), blk(db);
    ins.prepare("INSERT INTO artist_codes (code, songtype, path, akey) VALUES (?, ?, ?, ?)");
    blk.prepare("INSERT INTO code_blocks (akey, start, size) VALUES (?, ?, ?)");
    auto roundUp10 = [](int n) { return (n + 9) / 10 * 10; };
    for (const QString &a : artists) {
        QList<S> list = todo.value(a);
        std::sort(list.begin(), list.end(), [&](const S &x, const S &y) { return thai.compare(x.name, y.name) < 0; });
        // free numbers in this artist's runs first (the spare numbers kept for new songs)
        QList<qint64> free;
        for (const auto &b : blocks.value(a))
            for (qint64 c = b.first; c < b.first + b.second && free.size() < list.size(); c++)
                if (!used.contains(c))
                    free << c;
        if (free.size() < list.size()) {            // a new run (new artist, or the old run is full)
            const int need = list.size() - free.size();
            const int size = roundUp10(need + reserve);
            for (int i = 0; i < need; i++)
                free << nextFree + i;
            blk.bindValue(0, a);
            blk.bindValue(1, nextFree);
            blk.bindValue(2, size);
            blk.exec();
            blocks[a] << qMakePair(nextFree, size);
            nextFree += size;
        }
        for (int i = 0; i < list.size(); i++) {
            ins.bindValue(0, free[i]);
            ins.bindValue(1, list[i].type);
            ins.bindValue(2, list[i].path);
            ins.bindValue(3, a);
            if (ins.exec()) {
                used.insert(free[i]);
                st.added++;
            }
        }
    }
    db.commit();

    const CodeStats now = artistCodeStats();
    st.songs = now.songs;
    st.artists = now.artists;
    st.first = now.first;
    st.last = now.last;
    return st;
}

bool SongDatabase::songByCode(const QString &code, Song *out)
{
    const QString c = code.trimmed();
    if (c.length() != 7)
        return false;
    bool ok = false;
    const qint64 n = c.toLongLong(&ok);
    if (!ok)
        return false;
    ensureCodeTables(db);
    QSqlQuery q(db);
    q.prepare("SELECT s.* FROM artist_codes c JOIN songs s ON s.songtype = c.songtype AND s.path = c.path WHERE c.code = ? LIMIT 1");
    q.bindValue(0, n);
    ok = q.exec() && q.next();
    if (ok)
        setSong(out, &q);
    q.finish();
    return ok;
}

QString SongDatabase::codeOf(const QString &type, const QString &path)
{
    ensureCodeTables(db);
    QSqlQuery q(db);
    q.prepare("SELECT code FROM artist_codes WHERE songtype = ? AND path = ? LIMIT 1");
    q.bindValue(0, type);
    q.bindValue(1, path);
    return (q.exec() && q.next()) ? q.value(0).toString() : QString();
}

bool SongDatabase::exportSongBook(const QString &file)
{
    ensureCodeTables(db);
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    auto cell = [](QString v) {
        v.replace('"', "\"\"");
        return "\"" + v + "\"";
    };
    QByteArray out = "\xEF\xBB\xBF";                // UTF-8 BOM: Excel shows the Thai text
    out += QString::fromUtf8("รหัส,ศิลปิน,ชื่อเพลง,รหัสเดิม,ชนิด\r\n").toUtf8();
    QSqlQuery q(db);
    q.exec("SELECT c.code, s.artist, s.name, s.id, s.songtype FROM artist_codes c "
           "JOIN songs s ON s.songtype = c.songtype AND s.path = c.path ORDER BY c.code");
    while (q.next())
        out += (q.value(0).toString() + "," + cell(q.value(1).toString()) + "," + cell(q.value(2).toString()) + ","
                + cell(q.value(3).toString()) + "," + q.value(4).toString() + "\r\n").toUtf8();
    f.write(out);
    return true;
}

QString SongDatabase::kedmaneeToThai(const QString &s)
{
    static const QString eng = QString::fromUtf8(
        "1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./"
        "!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?");
    static const QString tha = QString::fromUtf8(
        "ๅ/-ภถุึคตจขชๆไำพะัีรนยบลฃฟหกดเ้่าสวงผปแอิืทมใฝ"
        "+๑๒๓๔ู฿๕๖๗๘๙๐\"ฎฑธํ๊ณฯญฐ,ฅฤฆฏโฌ็๋ษศซ.()ฉฮฺ์?ฒฬฦ");

    QString out;
    for (const QChar &c : s) {
        int i = eng.indexOf(c);
        out += (i >= 0 && i < tha.length()) ? tha[i] : c;
    }
    return out;
}

SongDatabase::~SongDatabase()
{
    delete fuzzy;           // waits for a running index build
    fuzzy = nullptr;
    if (db.isOpen()) {
        db.close();
    }
    delete song;
}

bool SongDatabase::isNewVersion()
{
    QSqlQuery q(db);
    bool rs = q.exec("Select * from miscellaneous");
    q.finish();
    q.clear();

    return rs;
}

void SongDatabase::updateToNewVersion()
{
    QFile file(":/update-database.sql");
    file.open(QIODevice::ReadOnly);

    QTextStream in(&file);
    QString sqlString = in.readAll();

    file.close();

    QSqlQuery q(db);

    QStringList sqlList = sqlString.split(QChar(';'));
    for (const QString &sql : sqlList)
    {
        q.exec(sql);
        q.finish();
        q.clear();
    }

    q.exec("vacuum");
    q.finish();
    q.clear();
}

int SongDatabase::count()
{
    if (!db.isOpen())
        return 0;

    int c = 0;
    QSqlQuery q(db);
    q.exec("SELECT Count(*) FROM songs");
    if (q.next()) {
        c = q.value(0).toInt();
    }
    q.finish();
    q.clear();

    return c;
}

bool SongDatabase::isNCNPath(const QString &dir)
{
    QDir d;
    QString cur = dir + "/Cursor";
    QString lyr = dir + "/Lyrics";
    QString mid = dir + "/Song";

    if (d.exists(cur) && d.exists(lyr) && d.exists(mid)) {
        return true;
    } else {
        return false;
    }
}

QString SongDatabase::getCurFilePath(const QString &midFilePath)
{
    QString cur = midFilePath;

    cur = cur.replace("/Song/", "/Cursor/");

    cur = cur.replace(cur.length()-4, 4, ".CUR");
    if (QFile::exists(cur))
        return cur;

    cur = cur.replace(cur.length()-4, 4, ".cur");
    if (QFile::exists(cur))
        return cur;

    cur = cur.replace(cur.length()-4, 4, ".Cur");
    if (QFile::exists(cur))
        return cur;

    return "";
}

QString SongDatabase::getLyrFilePath(const QString &midFilePath)
{
    QString lyr = midFilePath;

    lyr = lyr.replace("/Song/", "/Lyrics/");

    lyr = lyr.replace(lyr.length()-4, 4, ".LYR");
    if (QFile::exists(lyr))
        return lyr;

    lyr = lyr.replace(lyr.length()-4, 4, ".lyr");
    if (QFile::exists(lyr))
        return lyr;

    lyr = lyr.replace(lyr.length()-4, 4, ".Lyr");
    if (QFile::exists(lyr))
        return lyr;

    return "";
}

bool SongDatabase::setNcnPath(const QString &dir)
{
    if (isNCNPath(dir)) {
        _ncnPath = dir;
        return true;
    } else {
        return false;
    }
}

void SongDatabase::setUpdateType(UpdateType type)
{
    upType = type;
}

bool SongDatabase::insertNCN(const QString &ncnPath, const QString &songId, const QString &midFilePath)
{
    QString id = songId;

    QString curFilePath = getCurFilePath(midFilePath);
    QString lyrFilePath = getLyrFilePath(midFilePath);

    if (curFilePath == "" || lyrFilePath == "")
        return false;

    // Check mid file
    int bpm = MidiFile::firstBpm(midFilePath);
    if (bpm == 0)
        return false;


    // Read .LYR file (TIS-620 or UTF-8): name, artist, key, a spare line, then the lyrics
    QFile file(lyrFilePath);
    if (!file.open(QFile::ReadOnly))
        return false;
    QStringList lines = Utils::decodeThaiText(file.read(4096)).split(QRegExp("\r\n|\n|\r"));
    file.close();
    while (lines.count() < 8)
        lines << "";

    QString name = lines[0].trimmed();
    QString artist = lines[1].trimmed();
    QString key = lines[2].trimmed();
    QString type = "NCN";

    QString lyr = lines[4] + " " + lines[5] + " " + lines[6] + " " + lines[7];

    QString path = midFilePath;
    path = path.replace(ncnPath, "");

    // Insert to database
    QSqlQuery query(db);
    query.prepare("INSERT INTO songs VALUES "
                  "(?, ?, ?, ?, ?, ?, ?, ?);");
    query.bindValue(0, id);
    query.bindValue(1, name);
    query.bindValue(2, artist);
    query.bindValue(3, key);
    query.bindValue(4, bpm);
    query.bindValue(5, type);
    query.bindValue(6, lyr);
    query.bindValue(7, path);

    query.exec();
    query.finish();
    query.clear();

    return true;
}

bool SongDatabase::insertHNK(const QString &hnkPath, const QString &songId, const QString &hnkFilePath)
{
    QString id = songId;

    int bpm = HNKFile::bpm(hnkFilePath);
    if (bpm == 0)
        return false;

    // Read Lyrics
    QByteArray lyrData = HNKFile::lyrData(hnkFilePath);

    QTextStream textStream(&lyrData);
    textStream.setCodec("TIS-620");

    QString name = textStream.readLine();
    QString artist = textStream.readLine();
    QString key = textStream.readLine();
    QString type = "HNK";

    textStream.readLine();

    QString lyr = textStream.readLine() + " ";
    lyr += textStream.readLine() + " ";
    lyr += textStream.readLine() + " ";
    lyr += textStream.readLine();

    QString path = hnkFilePath;
    path = path.replace(hnkPath, "");

    // Insert to database
    QSqlQuery query(db);
    query.prepare("INSERT INTO songs VALUES "
                  "(?, ?, ?, ?, ?, ?, ?, ?);");
    query.bindValue(0, id);
    query.bindValue(1, name);
    query.bindValue(2, artist);
    query.bindValue(3, key);
    query.bindValue(4, bpm);
    query.bindValue(5, type);
    query.bindValue(6, lyr);
    query.bindValue(7, path);

    query.exec();
    query.finish();
    query.clear();

    return true;
}

bool SongDatabase::insertEMK(const QString &basePath, const QString &songId, const QString &emkFilePath)
{
    PTHKCore::EmkSong s = PTHKCore::readEmk(emkFilePath);
    if (!s.valid || s.midData.isEmpty())
        return false;

    QString id = songId;
    if (id.isEmpty())
        id = s.code.isEmpty() ? QFileInfo(emkFilePath).completeBaseName() : s.code;

    QString name = s.title.isEmpty() ? QFileInfo(emkFilePath).completeBaseName() : s.title;
    QString artist = s.artist;
    QString key = s.key;
    int bpm = s.tempo > 0 ? s.tempo : 120;
    QString type = "EMK";

    QTextCodec *tis620 = QTextCodec::codecForName("TIS-620");
    if (!tis620) tis620 = QTextCodec::codecForName("Windows-874");
    QString lyrText = tis620 ? tis620->toUnicode(s.lyrData) : QString::fromLatin1(s.lyrData);
    QStringList lines = lyrText.split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
    QString lyrSample;
    for (int j = 0; j < qMin(5, lines.size()); ++j) {
        lyrSample += lines[j] + " ";
    }
    lyrSample = lyrSample.trimmed();

    QString path = emkFilePath;
    path = path.replace(basePath, "");

    QSqlQuery query(db);
    query.prepare("INSERT INTO songs VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    query.bindValue(0, id);
    query.bindValue(1, name);
    query.bindValue(2, artist);
    query.bindValue(3, key);
    query.bindValue(4, bpm);
    query.bindValue(5, type);
    query.bindValue(6, lyrSample);
    query.bindValue(7, path);

    bool ok = query.exec();
    query.finish();
    query.clear();
    return ok;
}

bool SongDatabase::insertSIB(const QString &basePath, const QString &songId, const QString &sibFilePath)
{
    PTHKCore::SibSong s = PTHKCore::readSib(sibFilePath);
    if (!s.valid || s.midData.isEmpty())
        return false;

    QString id = songId;
    if (id.isEmpty())
        id = s.code.isEmpty() ? QFileInfo(sibFilePath).completeBaseName() : s.code;

    QString name = s.title.isEmpty() ? QFileInfo(sibFilePath).completeBaseName() : s.title;
    QString artist = s.artist;
    QString key = s.key;
    int bpm = s.tempo > 0 ? s.tempo : 120;
    QString type = "SIB";

    QTextCodec *tis620 = QTextCodec::codecForName("TIS-620");
    if (!tis620) tis620 = QTextCodec::codecForName("Windows-874");
    QString lyrText = tis620 ? tis620->toUnicode(s.lyrData) : QString::fromLatin1(s.lyrData);
    QStringList lines = lyrText.split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
    QString lyrSample;
    for (int j = 0; j < qMin(5, lines.size()); ++j) {
        lyrSample += lines[j] + " ";
    }
    lyrSample = lyrSample.trimmed();

    QString path = sibFilePath;
    if (!basePath.isEmpty() && path.startsWith(basePath))
        path = path.mid(basePath.length());

    QSqlQuery query(db);
    query.prepare("INSERT INTO songs VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    query.bindValue(0, id);
    query.bindValue(1, name);
    query.bindValue(2, artist);
    query.bindValue(3, key);
    query.bindValue(4, bpm);
    query.bindValue(5, type);
    query.bindValue(6, lyrSample);
    query.bindValue(7, path);

    bool ok = query.exec();
    query.finish();
    query.clear();
    return ok;
}

int SongDatabase::importSibDatabase(const QString &datPath, const QString &sibBasePath)
{
    QFile file(datPath);
    if (!file.open(QIODevice::ReadOnly))
        return 0;

    const QByteArray data = file.readAll();
    file.close();
    if (data.size() < 494)
        return 0;

    const int count = (int)(*(const quint32*)data.constData());
    if (count <= 0)
        return 0;

    QTextCodec *cp874 = QTextCodec::codecForName("TIS-620");
    if (!cp874) cp874 = QTextCodec::codecForName("Windows-874");

    QSet<QString> existingKeys = songNameArtistKeys();
    int added = 0;
    db.transaction();
    QSqlQuery query(db);
    query.prepare("INSERT INTO songs VALUES (?, ?, ?, ?, ?, ?, ?, ?);");

    const int recSize = 490;
    for (int i = 0; i < count; ++i) {
        const int off = 4 + i * recSize;
        if (off + recSize > data.size())
            break;

        const char *rec = data.constData() + off;
        const QString id = QString::fromLatin1(rec, qstrnlen(rec, 20)).trimmed();
        if (id.isEmpty())
            continue;

        const QString title = cp874 ? cp874->toUnicode(rec + 20, qstrnlen(rec + 20, 100)).trimmed()
                                    : QString::fromLatin1(rec + 20, qstrnlen(rec + 20, 100)).trimmed();
        const QString artist = cp874 ? cp874->toUnicode(rec + 120, qstrnlen(rec + 120, 100)).trimmed()
                                     : QString::fromLatin1(rec + 120, qstrnlen(rec + 120, 100)).trimmed();
        const QString key = QString::fromLatin1(rec + 480, qstrnlen(rec + 480, 6)).trimmed();
        const QString songType = QString::fromLatin1(rec + 220, qstrnlen(rec + 220, 30)).trimmed();

        const QString songKey = makeSongKey(title, artist);
        if (!songKey.isEmpty() && existingKeys.contains(songKey))
            continue;

        QString relPath;
        const QChar firstChar = id.isEmpty() ? QChar('0') : id.at(0).toUpper();
        if (songType == "SIBKLDEMO") {
            relPath = QStringLiteral("/SIBKLDEMO/") + QString(firstChar) + QStringLiteral("/") + id + QStringLiteral(".sibkldemo");
        } else {
            relPath = QStringLiteral("/") + QString(firstChar) + QStringLiteral("/") + id + QStringLiteral(".sib");
        }

        query.bindValue(0, id);
        query.bindValue(1, title);
        query.bindValue(2, artist);
        query.bindValue(3, key);
        query.bindValue(4, 120);
        query.bindValue(5, QStringLiteral("SIB"));
        query.bindValue(6, QString());
        query.bindValue(7, relPath);

        if (query.exec()) {
            added++;
            if (!songKey.isEmpty())
                existingKeys.insert(songKey);
        }
    }

    db.commit();
    return added;
}

bool SongDatabase::insertKAR(const QString &karPath, const QString &songId, const QString &karFilePath, const QString &fileName)
{
    QString id = songId;

    MidiFile mid;
    if (!mid.read(karFilePath))
        return false;

    int bpm = 120;
    if (mid.tempoEvents().size() > 0)
        bpm = mid.tempoEvents()[0]->bpm();

    QString name = fileName.section(".", 0, 0);
    QString type = "KAR";

    QString path = karFilePath;
    path = path.replace(karPath, "");

    QString lyr = "";
    QStringList lyrics = mid.lyrics().split(QRegExp("\n|\r\n|\r"));
    if (lyrics.size() > 0)
        lyr += lyrics[0] + " ";
    if (lyrics.size() > 1)
        lyr += lyrics[1] + " ";
    if (lyrics.size() > 2)
        lyr += lyrics[2] + " ";
    if (lyrics.size() > 3)
        lyr += lyrics[3];

    QSqlQuery query(db);
    query.prepare("INSERT INTO songs VALUES "
                  "(?, ?, ?, ?, ?, ?, ?, ?);");
    query.bindValue(0, id);
    query.bindValue(1, name);
    query.bindValue(2, "");
    query.bindValue(3, "");
    query.bindValue(4, bpm);
    query.bindValue(5, type);
    query.bindValue(6, lyr);
    query.bindValue(7, path);

    query.exec();
    query.finish();
    query.clear();

    return true;
}

QStringList SongDatabase::videoFilters()
{
    // (a named list: a range-for over "QStringList() << ..." would iterate a destroyed temporary)
    const QStringList exts = { "mp4", "mkv", "avi", "mpg", "mpeg", "wmv", "vob", "dat" };
    QStringList f;
    for (const QString &ext : exts)
        f << "*." + ext << "*." + ext.toUpper();
    return f;
}

bool SongDatabase::insertVideo(const QString &videoPath, const QString &videoFilePath, const QString &fileName,
                               const QString &type, const QString &idPrefix)
{
    // File name patterns: "12345 Name - Artist.mp4", "Name - Artist.mp4" or just "Name.mp4".
    // Ids get a prefix ("V" video, "M" MP3) so they never collide with NCN song numbers.
    QString base = fileName.left(fileName.lastIndexOf('.'));
    QString id = idPrefix + "-------";

    QRegExp numPrefix("^(\\d+)[\\s_\\-.]+(.*)$");
    if (numPrefix.indexIn(base) == 0 && !numPrefix.cap(2).trimmed().isEmpty()) {
        id = idPrefix + numPrefix.cap(1);
        base = numPrefix.cap(2);
    } else {
        // no number in the file name (e.g. YouTube downloads): take the first free number from 50001
        QSet<qint64> used;
        QSqlQuery q(db);
        q.prepare("SELECT id FROM songs WHERE id LIKE ?;");
        q.bindValue(0, idPrefix + "%");
        if (q.exec())
            while (q.next())
                used.insert(q.value(0).toString().mid(idPrefix.length()).toLongLong());
        q.finish();
        qint64 n = 50001;
        while (used.contains(n))
            n++;
        id = idPrefix + QString::number(n);
    }

    QString name = base.trimmed();
    QString artist = "";
    int sep = base.indexOf(" - ");
    if (sep > 0) {
        name = base.left(sep).trimmed();
        artist = base.mid(sep + 3).trimmed();
    }

    QString path = videoFilePath;
    path = path.replace(videoPath, "");

    QSqlQuery query(db);
    query.prepare("INSERT INTO songs VALUES "
                  "(?, ?, ?, ?, ?, ?, ?, ?);");
    query.bindValue(0, id);
    query.bindValue(1, name);
    query.bindValue(2, artist);
    query.bindValue(3, "");
    query.bindValue(4, 0);
    query.bindValue(5, type);
    query.bindValue(6, "");
    query.bindValue(7, path);

    bool ok = query.exec();
    query.finish();
    query.clear();

    return ok;
}

Song* SongDatabase::nextType(const QString &s)
{
    Song *sg = song;
    switch (searchType) {
    case SearchType::ByAll:
        searchType = SearchType::ById;
        sg = search(s);
        break;
    case SearchType::ById:
        searchType = SearchType::ByName;
        sg = search(_searchText);
        break;
    case SearchType::ByName:
        searchType = SearchType::ByArtist;
        sg = search(_searchText);
        break;
    case SearchType::ByArtist:
        searchType = SearchType::ByVideo;
        sg = search(_searchText);
        break;
    case SearchType::ByVideo:
        searchType = SearchType::ByAll;
        sg = search(_searchText);
        break;
    }
    return sg;
}

void setSong(Song *s, QSqlQuery *qry) {
    s->setId(qry->value(0).toString());
    s->setName(qry->value(1).toString());
    s->setArtist(qry->value(2).toString());
    s->setKey(qry->value(3).toString());
    s->setTempo(qry->value(4).toInt());
    s->setSongType(qry->value(5).toString());
    s->setLyrics(qry->value(6).toString());
    s->setPath(qry->value(7).toString());

    s->setBpmSpeed(0);
    s->setTranspose(0);
}

// "Search all": video / MP3 songs (the user's own files, few) whose name or artist
// contains the text come first - downloaded clip names usually start with words like
// "คาราโอเกะ", so a prefix match would never find them. Then id, name and artist prefix
// matches, then names containing the text in the middle, then KAR files by name.
static QString byAllSql()
{
    return "SELECT * FROM (SELECT * FROM songs WHERE songtype IN ('VIDEO','MP3') AND (name LIKE ? OR artist LIKE ?) ORDER BY name, artist, id) "
           "UNION ALL "
           "SELECT * FROM (SELECT * FROM songs WHERE id LIKE ? AND songtype NOT IN ('KAR','VIDEO','MP3') ORDER BY id, name, artist) "
           "UNION ALL "
           "SELECT * FROM (SELECT * FROM songs WHERE name LIKE ? AND songtype NOT IN ('KAR','VIDEO','MP3') ORDER BY name, artist, id) "
           "UNION ALL "
           "SELECT * FROM (SELECT * FROM songs WHERE artist LIKE ? AND songtype NOT IN ('KAR','VIDEO','MP3') ORDER BY artist, name, id) "
           "UNION ALL "
           "SELECT * FROM (SELECT * FROM songs WHERE name LIKE ? AND name NOT LIKE ? AND songtype NOT IN ('KAR','VIDEO','MP3') ORDER BY name, artist, id) "
           "UNION ALL "
           "SELECT * FROM (SELECT * FROM songs WHERE name LIKE ? AND songtype = 'KAR' ORDER BY name, artist, id) ";
}

static void bindByAll(QSqlQuery &q, const QString &s)
{
    q.bindValue(0, "%" + s + "%");
    q.bindValue(1, "%" + s + "%");
    q.bindValue(2, s + "%");
    q.bindValue(3, s + "%");
    q.bindValue(4, s + "%");
    q.bindValue(5, "%" + s + "%");
    q.bindValue(6, s + "%");
    q.bindValue(7, "%" + s + "%");
}

static const char *kByVideoSql =
    "SELECT * FROM songs WHERE songtype IN ('VIDEO','MP3') AND (name LIKE ? OR artist LIKE ?) "
    "ORDER BY name, artist, id";

static void bindSearch(QSqlQuery &q, SearchType t, const QString &s)
{
    if (t == SearchType::ByAll) {
        bindByAll(q, s);
    } else if (t == SearchType::ByVideo) {
        q.bindValue(0, "%" + s + "%");
        q.bindValue(1, "%" + s + "%");
    } else {
        q.bindValue(0, s + "%");
    }
}

void SongDatabase::startFuzzyIndex()
{
    if (!fuzzy)
        fuzzy = new SongFuzzy();
    fuzzy->build(db.databaseName());
}

bool SongDatabase::loadRow(qint64 rowid)
{
    QSqlQuery q(db);
    q.prepare("SELECT * FROM songs WHERE rowid = ?");
    q.bindValue(0, rowid);
    const bool ok = q.exec() && q.next();
    if (ok)
        setSong(song, &q);
    q.finish();
    return ok;
}

Song *SongDatabase::searchFuzzy(const QString &s)
{
    fuzzyMode = false;
    fuzzyRows.clear();
    fuzzyPos = 0;
    if (!fuzzy || !fuzzy->isReady())
        return song;
    const QVector<qint64> rows = fuzzy->find(s);
    for (int i = 0; i < rows.size(); ++i) {
        if (loadRow(rows[i])) {             // skips rows deleted since the index was built
            fuzzyRows = rows.mid(i);
            fuzzyMode = true;
            lastFound = true;
            _searchText = s;
            break;
        }
    }
    return song;
}

Song *SongDatabase::search(const QString &s)
{
    QString sql = "";
    fuzzyMode = false;
    _searchText = s;
    currentResultIndex = 0;
    lastFound = false;

    if (songByCode(s, song)) {          // a code by artist (see assignArtistCodes)
        lastFound = true;
        return song;
    }

    switch (searchType) {
    case SearchType::ByAll:
        sql = byAllSql() + "LIMIT 1";
        break;
    case SearchType::ById:
        sql = "SELECT * FROM songs WHERE id LIKE ? "
              "ORDER BY id, name, artist LIMIT 1";
        break;
    case SearchType::ByName:
        sql = "SELECT * FROM songs WHERE name LIKE ? "
              "ORDER BY name, artist, id LIMIT 1";
        break;
    case SearchType::ByArtist:
        sql = "SELECT * FROM songs WHERE artist LIKE ? "
              "ORDER BY artist, name, id LIMIT 1";
        break;
    case SearchType::ByVideo:
        sql = QString(kByVideoSql) + " LIMIT 1";
        break;
    }

    QSqlQuery query(db);
    query.prepare(sql);
    bindSearch(query, searchType, s);

    if (query.exec()) {
        if (query.next()) {
            setSong(song, &query);
            lastFound = true;
        }
    }
    query.finish();
    query.clear();

    return song;
}

Song *SongDatabase::searchNext()
{
    if (fuzzyMode) {
        while (fuzzyPos + 1 < fuzzyRows.size()) {
            ++fuzzyPos;
            if (loadRow(fuzzyRows[fuzzyPos]))
                break;
        }
        return song;
    }
    QString sql = "";

    switch (searchType) {
    case SearchType::ByAll:
        sql = byAllSql();
        break;
    case SearchType::ById:
        sql = "SELECT * FROM songs WHERE id LIKE ? "
              "ORDER BY id, name, artist";
        break;
    case SearchType::ByName:
        sql = "SELECT * FROM songs WHERE name LIKE ? "
              "ORDER BY name, artist, id";
        break;
    case SearchType::ByArtist:
        sql = "SELECT * FROM songs WHERE artist LIKE ? "
              "ORDER BY artist, name, id";
        break;
    case SearchType::ByVideo:
        sql = kByVideoSql;
        break;
    }

    QSqlQuery q(db);
    q.prepare(sql);

    bindSearch(q, searchType, _searchText);

    if (q.exec() && q.seek(currentResultIndex + 1)) {
        currentResultIndex++;
        setSong(song, &q);
    }

    q.finish();
    q.clear();

    return song;
}

Song *SongDatabase::searchPrevious()
{
    if (fuzzyMode) {
        while (fuzzyPos > 0) {
            --fuzzyPos;
            if (loadRow(fuzzyRows[fuzzyPos]))
                break;
        }
        return song;
    }
    QString sql = "";

    switch (searchType) {
    case SearchType::ByAll:
        sql = byAllSql();
        break;
    case SearchType::ById:
        sql = "SELECT * FROM songs WHERE id LIKE ? "
              "ORDER BY id, name, artist";
        break;
    case SearchType::ByName:
        sql = "SELECT * FROM songs WHERE name LIKE ? "
              "ORDER BY name, artist, id";
        break;
    case SearchType::ByArtist:
        sql = "SELECT * FROM songs WHERE artist LIKE ? "
              "ORDER BY artist, name, id";
        break;
    case SearchType::ByVideo:
        sql = kByVideoSql;
        break;
    }

    QSqlQuery q(db);
    q.prepare(sql);

    bindSearch(q, searchType, _searchText);

    if (q.exec() && q.seek(currentResultIndex - 1)) {
        currentResultIndex--;
        setSong(song, &q);
    }

    q.finish();
    q.clear();

    return song;
}

bool SongDatabase::removeSong(Song &s)
{
    QSqlQuery q(db);
    q.prepare("DELETE FROM songs WHERE id = ? AND songtype = ? AND path = ?");
    q.bindValue(0, s.id());
    q.bindValue(1, s.songType());
    q.bindValue(2, s.path());
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    q.finish();
    return ok;
}

bool SongDatabase::removeCurrentSong(bool removeFromStorage)
{
   QString sql = "DELETE FROM songs WHERE id = ? AND name = ? AND songtype = ? AND path = ?";

   QSqlQuery q(db);
   q.prepare(sql);
   q.bindValue(0, song->id());
   q.bindValue(1, song->name());
   q.bindValue(2, song->songType());
   q.bindValue(3, song->path());

   if (!q.exec())
       return false;

   q.finish();
   q.clear();

   currentResultIndex--;

   if (removeFromStorage)
   {
       if (song->songType() == "NCN")
       {
           QString midFilePath = _ncnPath + song->path();
           QString curFilePath = getCurFilePath(midFilePath);
           QString lyrFilePath = getLyrFilePath(midFilePath);

           QFile f(midFilePath);
           f.remove();

           f.setFileName(curFilePath);
           f.remove();

           f.setFileName(lyrFilePath);
           f.remove();
       }
       else if (song->songType() == "HNK")
       {
           QString path = _hnkPath + song->path();
           QFile f(path);
           f.remove();
       }
       else if (song->songType() == "KAR")
       {
           QString path = _karPath + song->path();
           QFile f(path);
           f.remove();
       }
   }

   return true;
}

void SongDatabase::run()
{
    if (!db.isOpen()) {
        return;
    }
    if (upType == UpdateType::UpdateNewSongs) {
        runNewSongs();
        return;
    }

    // UpdateMedia: only KAR, video and MP3 are scanned again (seconds instead of the
    // half hour a full NCN library takes); the NCN / HNK songs stay as they are
    const bool mediaOnly = (upType == UpdateType::UpdateMedia);

    if (!mediaOnly && !isNCNPath(_ncnPath)) {
        return;
    }

    int count = 0;

    // Count mid file in NCN
    QDir dir(_ncnPath + "/Song");
    QDir hnkDir(_hnkPath);
    QDir karDir(_karPath);

    if (!mediaOnly) {
        QDirIterator iter1(dir.path() ,QStringList() << "*.mid" << "*.MID",
                        QDir::Files, QDirIterator::Subdirectories);
        while (iter1.hasNext()) {
            iter1.next();
            count++;
        }

        QDirIterator iter2(hnkDir.path() ,QStringList() << "*.hnk" << "*.HNK",
                        QDir::Files, QDirIterator::Subdirectories);
        while (iter2.hasNext()) {
            iter2.next();
            count++;
        }
        QDirIterator iterEmk((_hnkPath.isEmpty() ? QFileInfo(_ncnPath).dir().absoluteFilePath("EMK") : _hnkPath), QStringList() << "*.emk" << "*.EMK",
                             QDir::Files, QDirIterator::Subdirectories);
        while (iterEmk.hasNext()) {
            iterEmk.next();
            count++;
        }
    }

    QDirIterator iter3(karDir.path() ,QStringList() << "*.kar" << "*.KAR" << "*.mid" << "*.MID",
                    QDir::Files, QDirIterator::Subdirectories);
    while (iter3.hasNext()) {
        iter3.next();
        count++;
    }

    QDir videoDir(_videoPath);
    if (!_videoPath.isEmpty()) {
        QDirIterator iter4(videoDir.path(), videoFilters(), QDir::Files, QDirIterator::Subdirectories);
        while (iter4.hasNext()) {
            iter4.next();
            count++;
        }
    }

    const QStringList mp3Filters = QStringList() << "*.mp3" << "*.MP3";
    QDir mp3Dir(_mp3Path);
    if (!_mp3Path.isEmpty()) {
        QDirIterator iter5(mp3Dir.path(), mp3Filters, QDir::Files, QDirIterator::Subdirectories);
        while (iter5.hasNext()) {
            iter5.next();
            count++;
        }
    }

    upCount = count;
    emit updateCountChanged(count);

    upTing = true;

    QSqlQuery q(db);
    if (mediaOnly) {
        q.exec("DELETE FROM songs WHERE songtype IN ('KAR', 'VIDEO', 'MP3')");
    } else {
        q.exec("DELETE FROM songs");
        q.exec("vacuum");
    }
    q.finish();
    q.clear();

    db.transaction();

    if (!mediaOnly)
        dropIndex();


    // Update NCN
    int i = 0;
    int erCount = 0;
    QDirIterator it(dir.path() ,QStringList() << "*.mid" << "*.MID",
                     QDir::Files, QDirIterator::Subdirectories);
    while (!mediaOnly && it.hasNext()) {

        it.next();

        i++;
        emit updatePositionChanged(i);
        emit updateSongNameChanged(it.fileName());

        QString id = it.fileName().section(".", 0, 0);

        bool result = insertNCN(_ncnPath, id, it.filePath());

        if (!result)
            erCount ++;
    }


    // Update HNK
    QDirIterator it2(hnkDir.path() ,QStringList() << "*.hnk" << "*.HNK",
                     QDir::Files, QDirIterator::Subdirectories);
    while (!mediaOnly && it2.hasNext()) {

        it2.next();

        i++;
        emit updatePositionChanged(i);
        emit updateSongNameChanged(it2.fileName());

        QString hnkId = it2.fileName().section(".", 0, 0);

        bool result = insertHNK(_hnkPath, hnkId, it2.filePath());

        if (!result)
            erCount ++;
    }

    // Update KAR
    QDirIterator it3(karDir.path() ,QStringList() << "*.kar" << "*.KAR" << "*.mid" << "*.MID",
                     QDir::Files, QDirIterator::Subdirectories);
    while (it3.hasNext()) {

        it3.next();

        i++;
        emit updatePositionChanged(i);
        emit updateSongNameChanged(it3.fileName());

        QString karId = "-------";

        bool result = insertKAR(_karPath, karId, it3.filePath(), it3.fileName());

        if (!result)
            erCount ++;
    }

    // Update video
    if (!_videoPath.isEmpty()) {
        QDirIterator it4(videoDir.path(), videoFilters(), QDir::Files, QDirIterator::Subdirectories);
        while (it4.hasNext()) {

            it4.next();

            i++;
            emit updatePositionChanged(i);
            emit updateSongNameChanged(it4.fileName());

            if (!insertVideo(_videoPath, it4.filePath(), it4.fileName()))
                erCount ++;
        }
    }

    // Update MP3 (+ .lrc lyrics)
    if (!_mp3Path.isEmpty()) {
        QDirIterator it5(mp3Dir.path(), mp3Filters, QDir::Files, QDirIterator::Subdirectories);
        while (it5.hasNext()) {

            it5.next();

            i++;
            emit updatePositionChanged(i);
            emit updateSongNameChanged(it5.fileName());

            if (!insertVideo(_mp3Path, it5.filePath(), it5.fileName(), "MP3", "M"))
                erCount ++;
        }
    }


    db.commit();

    createIndex();

    upTing = false;
}

// Only what changed in the NCN / HNK folders: new files are read and added, rows of deleted files
// are removed. The ~90,000 songs already in the database are not read again, so adding a few
// new songs takes seconds instead of the half hour of a full update.
void SongDatabase::runNewSongs()
{
    newAdded = 0;
    newRemoved = 0;
    if (!isNCNPath(_ncnPath))
        return;

    // what the database has: "TYPE|path" (paths as insertNCN / insertHNK store them)
    QSet<QString> known;
    {
        QSqlQuery q(db);
        q.exec("SELECT songtype, path FROM songs WHERE songtype IN ('NCN', 'HNK', 'EMK')");
        while (q.next())
            known.insert(q.value(0).toString() + "|" + q.value(1).toString().toLower());
        q.finish();
    }

    struct NewFile { QString type, id, file; };
    QVector<NewFile> todo;
    QSet<QString> seen;

    QDir dir(_ncnPath + "/Song");
    QDirIterator it(dir.path(), QStringList() << "*.mid" << "*.MID", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QString rel = it.filePath();
        rel = rel.replace(_ncnPath, "");
        const QString key = "NCN|" + rel.toLower();
        seen.insert(key);
        if (!known.contains(key))
            todo.append({ "NCN", it.fileName().section(".", 0, 0), it.filePath() });
    }
    if (!_hnkPath.isEmpty() && QDir(_hnkPath).exists()) {
        QDir hnkDir(_hnkPath);
        QDirIterator it2(hnkDir.path(), QStringList() << "*.hnk" << "*.HNK", QDir::Files, QDirIterator::Subdirectories);
        while (it2.hasNext()) {
            it2.next();
            QString rel = it2.filePath();
            rel = rel.replace(_hnkPath, "");
            const QString key = "HNK|" + rel.toLower();
            seen.insert(key);
            if (!known.contains(key))
                todo.append({ "HNK", it2.fileName().section(".", 0, 0), it2.filePath() });
        }
        QDirIterator itEmk((_hnkPath.isEmpty() ? QFileInfo(_ncnPath).dir().absoluteFilePath("EMK") : _hnkPath), QStringList() << "*.emk" << "*.EMK", QDir::Files, QDirIterator::Subdirectories);
        while (itEmk.hasNext()) {
            itEmk.next();
            QString rel = itEmk.filePath();
            rel = rel.replace(_ncnPath, "");
            const QString key = "EMK|" + rel.toLower();
            seen.insert(key);
            if (!known.contains(key))
                todo.append({ "EMK", itEmk.fileName().section(".", 0, 0), itEmk.filePath() });
        }
    } else {
        for (const QString &k : known)      // no HNK folder set: keep its songs as they are
            if (k.startsWith("HNK|"))
                seen.insert(k);
    }

    upCount = todo.size();
    emit updateCountChanged(upCount);
    upTing = true;
    db.transaction();

    for (const QString &k : known) {
        if (seen.contains(k))
            continue;
        QSqlQuery q(db);
        q.prepare("DELETE FROM songs WHERE songtype = ? AND lower(path) = ?");
        q.bindValue(0, k.section('|', 0, 0));
        q.bindValue(1, k.section('|', 1));
        if (q.exec())
            newRemoved++;



    }



    newSkippedDuplicate = 0;
    QSet<QString> existingKeys = songNameArtistKeys();

    int i = 0;
    for (const NewFile &f : todo) {
        i++;
        emit updatePositionChanged(i);
        emit updateSongNameChanged(QFileInfo(f.file).fileName());

        QString sTitle, sArtist;
        if (f.type == "NCN") {
            QString lyr = getLyrFilePath(f.file);
            QFile lyrF(lyr);
            if (lyrF.open(QFile::ReadOnly)) {
                QStringList lines = Utils::decodeThaiText(lyrF.read(4096)).split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
                lyrF.close();
                if (lines.size() > 0) sTitle = lines[0].trimmed();
                if (lines.size() > 1) sArtist = lines[1].trimmed();
            }
        } else if (f.type == "HNK") {
            QByteArray lyrData = HNKFile::lyrData(f.file);
            QTextStream textStream(&lyrData);
            textStream.setCodec("TIS-620");
            sTitle = textStream.readLine().trimmed();
            sArtist = textStream.readLine().trimmed();
        } else if (f.type == "EMK") {
            PTHKCore::EmkSong s = PTHKCore::readEmk(f.file);
            sTitle = s.title.trimmed();
            sArtist = s.artist.trimmed();
        }
        const QString songKey = makeSongKey(sTitle, sArtist);
        if (!songKey.isEmpty() && existingKeys.contains(songKey)) {
            newSkippedDuplicate++;
            continue;
        }

        const bool ok = (f.type == "NCN") ? insertNCN(_ncnPath, f.id, f.file)
                                          : (f.type == "HNK") ? insertHNK(_hnkPath, f.id, f.file)
                                          : insertEMK((_hnkPath.isEmpty() ? QFileInfo(_ncnPath).dir().absoluteFilePath("EMK") : _hnkPath), f.id, f.file);
        if (ok) {
            if (!songKey.isEmpty())
                existingKeys.insert(songKey);
            newAdded++;
        }
    }

    db.commit();
    upTing = false;
}

QSet<QString> SongDatabase::songPaths(const QString &type)
{
    QSet<QString> out;
    QSqlQuery q(db);
    q.prepare("SELECT path FROM songs WHERE songtype = ?");
    q.bindValue(0, type);
    if (q.exec())
        while (q.next())
            out.insert(q.value(0).toString().toLower());
    return out;
}

void SongDatabase::beginBatch() { db.transaction(); }
void SongDatabase::endBatch()   { db.commit(); }

QString SongDatabase::makeSongKey(const QString &name, const QString &artist)
{
    const QString n = name.simplified().toLower();
    const QString a = artist.simplified().toLower();
    if (n.isEmpty())
        return QString();
    return n + "|" + a;
}

QSet<QString> SongDatabase::songNameArtistKeys()
{
    QSet<QString> keys;
    QSqlQuery q(db);
    if (q.exec("SELECT name, artist FROM songs")) {
        while (q.next()) {
            const QString k = makeSongKey(q.value(0).toString(), q.value(1).toString());
            if (!k.isEmpty())
                keys.insert(k);
        }
    }
    return keys;
}

void SongDatabase::createIndex()
{
    QSqlQuery query(db);
    QString sql;

    sql = "CREATE INDEX id_idx ON songs(id); ";
    query.exec(sql);
    query.finish();
    query.clear();

    sql = "CREATE INDEX name_idx ON songs(name); ";
    query.exec(sql);
    query.finish();
    query.clear();

    sql = "CREATE INDEX artist_idx ON songs(artist); ";
    query.exec(sql);
    query.finish();
    query.clear();

    sql = "CREATE INDEX compound_idx ON songs(id,name,artist); ";
    query.exec(sql);
    query.finish();
    query.clear();
}

void SongDatabase::dropIndex()
{
    QSqlQuery q(db);
    QString sql;

    sql = "DROP INDEX id_idx; ";
    q.exec(sql);
    q.finish();
    q.clear();

    sql = "DROP INDEX name_idx; ";
    q.exec(sql);
    q.finish();
    q.clear();

    sql = "DROP INDEX artist_idx; ";
    q.exec(sql);
    q.finish();
    q.clear();

    sql = "DROP INDEX compound_idx; ";
    q.exec(sql);
    q.finish();
    q.clear();
}
