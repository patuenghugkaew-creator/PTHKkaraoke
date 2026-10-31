#ifndef SONGDATABASE_H
#define SONGDATABASE_H

#include "Song.h"

#include <QObject>
#include <QSqlDatabase>
#include <QSet>
#include <QThread>


enum class SearchType {
    ByAll,
    ById,
    ByName,
    ByArtist,
    ByVideo         // video / MP3 songs only, the text anywhere in the name or artist
};

enum class UpdateType {
    UpdateAll,
    ImportNCN,
    UpdateMedia,    // KAR / video / MP3 folders only
    UpdateNewSongs  // NCN / HNK: add the files not in the database yet, drop the deleted ones

};

class SongDatabase : public QThread
{
    Q_OBJECT
public:
    SongDatabase();
    ~SongDatabase();

    bool isNewVersion();
    void updateToNewVersion();

    int count();
    Song* currentSong() { return song; }
    QString searchText() { return _searchText; }

    QSqlDatabase* database() { return &db; }

    bool isOpenned() { return db.isOpen(); }
    bool isUpdatting() { return upTing; }
    int updateCount() { return upCount; }
    int lastAdded() const { return newAdded; }
    int lastSkippedDuplicate() const { return newSkippedDuplicate; }
    static QString makeSongKey(const QString &name, const QString &artist);
    QSet<QString> songNameArtistKeys();
    // song files already in the database ("/Song/Z/Za83.mid", lower case), for big imports
    QSet<QString> songPaths(const QString &type);
    void beginBatch();      // many inserts in one transaction (one disk write instead of thousands)
    void endBatch();       // result of the last UpdateNewSongs
    int lastRemoved() const { return newRemoved; }

    static bool isNCNPath(const QString &dir);
    static QString getCurFilePath(const QString &midFilePath);
    static QString getLyrFilePath(const QString &midFilePath);

    QString ncnPath() { return _ncnPath; }
    bool setNcnPath(const QString &dir);

    QString hnkPath() { return _hnkPath; }
    void setHNKPath(const QString &p) { _hnkPath = p; }

    QString karPath() { return _karPath; }
    QString emkPath() const { return _hnkPath; }
    void setEmkPath(const QString &path) { _hnkPath = path; }
    void setKarPath(const QString &p) { _karPath = p; }

    // karaoke video files (mp4, mkv, avi, mpg, wmv, vob); songtype "VIDEO"
    QString videoPath() { return _videoPath; }
    void setVideoPath(const QString &p) { _videoPath = p; }
    static QStringList videoFilters();

    // MP3 songs; lyrics come from a .lrc file with the same name (optional); songtype "MP3"
    QString mp3Path() { return _mp3Path; }
    void setMp3Path(const QString &p) { _mp3Path = p; }

    // SIB songs (SIBSIB Karaoke AES-256 + ZIP); songtype "SIB"
    QString sibPath() { return _sibPath; }
    void setSibPath(const QString &p) { _sibPath = p; }
    bool insertSIB(const QString &basePath, const QString &songId, const QString &sibFilePath);
    int importSibDatabase(const QString &datPath, const QString &sibBasePath);

    // remove the row of one file (before adding it again), songtype + path as stored
    void removeSongFile(const QString &type, const QString &path);
    QString songIdFor(const QString &type, const QString &path);

    // true when the last search() found a song (otherwise currentSong() is unchanged)
    bool lastSearchFound() { return lastFound; }
    // Approximate search (SongFuzzy.h) when the exact search found nothing: typos, missing
    // tone marks / spaces, sound-alike letters. Next / previous then step through its results.
    void startFuzzyIndex();
    Song *searchFuzzy(const QString &s);
    bool isFuzzyResult() const { return fuzzyMode; }
    // text typed while the keyboard was still in English mode -> Thai (Kedmanee layout)
    static QString kedmaneeToThai(const QString &s);

    // Favorites and play history (kept in their own tables, a song-database rebuild does not clear them)
    void addHistory(Song *s);
    bool isFavorite(Song *s);
    bool toggleFavorite(Song *s);           // returns the new state
    enum class ListKind { Favorites, History, Top };
    void removeFromList(ListKind kind, Song s);
    void clearList(ListKind kind);
    QList<Song> songList(ListKind kind, int limit = 200);
    // for the phone song requests: does not touch the search box state of the player
    QList<Song> findSongs(const QString &text, int limit = 30);
    bool songByPath(const QString &type, const QString &path, Song *out);

    // Codes by artist (PTHK's own, beside the codes of the song files): every artist gets a run of
    // 7-digit codes, songs sorted by name, with spare numbers at the end of the run so the artist's
    // new songs get codes next to the old ones. Assigning again keeps every code given before.
    struct CodeStats { int songs = 0, artists = 0, added = 0, removed = 0; qint64 first = 0, last = 0; };
    CodeStats assignArtistCodes(qint64 start, int reserve, bool fresh);
    CodeStats artistCodeStats();
    bool songByCode(const QString &code, Song *out);
    QString codeOf(const QString &type, const QString &path);
    bool exportSongBook(const QString &file);       // CSV (Excel) sorted by code

    UpdateType updateType() { return upType; }
    void setUpdateType(UpdateType type);

public slots:
    bool insertNCN(const QString &ncnPath, const QString &songId, const QString &midFilePath);
    bool insertHNK(const QString &hnkPath, const QString &songId, const QString &hnkFilePath);
    bool insertEMK(const QString &basePath, const QString &songId, const QString &emkFilePath);
    bool insertKAR(const QString &karPath, const QString &songId, const QString &karFilePath, const QString &fileName);
    bool insertVideo(const QString &videoPath, const QString &videoFilePath, const QString &fileName,
                     const QString &type = "VIDEO", const QString &idPrefix = "V");

    void setSearchType(SearchType t) { searchType = t; }
    SearchType currentSearchType() const { return searchType; }
    Song *nextType(const QString &s);
    Song* search(const QString &s);
    Song* searchNext();
    Song* searchPrevious();

    bool removeCurrentSong(bool removeFromStorage = false);
    // remove this song from the song list (the database only; files stay)
    bool removeSong(Song &s);

signals:
    void updateCountChanged(int c);
    void updatePositionChanged(int p);
    void updateSongNameChanged(QString n);

protected:
    void run();

private:
    void createIndex();
    void dropIndex();

private:
    QSqlDatabase db;
    Song *song;
    SearchType searchType;

    QString _searchText = "";

    UpdateType upType = UpdateType::UpdateAll;
    QString _ncnPath = "";
    QString _hnkPath = "";
    QString _karPath = "";
    QString _videoPath = "";
    QString _mp3Path = "";
    QString _sibPath = "";


    int upCount = 0;
    int newAdded = 0;
    int newRemoved = 0;
    int newSkippedDuplicate = 0;
    void runNewSongs();
    bool upTing = false;

    int currentResultIndex = -1;
    bool lastFound = false;
    class SongFuzzy *fuzzy = nullptr;
    bool fuzzyMode = false;
    QVector<qint64> fuzzyRows;
    int fuzzyPos = 0;
    bool loadRow(qint64 rowid);

    void createListTables();
};

#endif // SONGDATABASE_H
