#ifndef YOUTUBE_H
#define YOUTUBE_H

// YouTube support:
//  - YouTubeDownloadDialog: downloads a clip from a link with yt-dlp (+ ffmpeg) into the
//    VIDEO folder, so it plays like any other video song.
//  - YouTubeBrowser: plays YouTube online in an embedded browser (needs Qt WebEngine).
// yt-dlp / ffmpeg are not shipped; they are fetched from their official GitHub releases
// on first use (after asking) into %LOCALAPPDATA%\PTHK Karaoke\tools.

#include <QDialog>
#include <QProcess>
#include <QPointer>
#include <QVector>
#include <QStringList>

class QLineEdit;
class QCheckBox;
class QProgressBar;
class QLabel;
class QPushButton;
class QNetworkAccessManager;
class QFile;

namespace YtTools {
QString toolsDir();
QString ytdlpPath();        // empty when not found
QString ffmpegPath();       // empty when not found
bool isYouTubeUrl(const QString &url);
}

class YouTubeDownloadDialog : public QDialog
{
    Q_OBJECT
public:
    YouTubeDownloadDialog(const QString &outDir, const QString &url, QWidget *parent = nullptr);
    ~YouTubeDownloadDialog() override;

signals:
    void downloaded(const QString &file, bool enqueue);

protected:
    void reject() override;

private:
    void start();
    void fetchTools();
    void fetchNext();
    void runYtdlp();
    void setBusy(bool busy);

    QString outDir;
    QLineEdit *edUrl;
    QCheckBox *chkQueue;
    QProgressBar *progress;
    QLabel *lbStatus;
    QPushButton *btnStart;

    QNetworkAccessManager *net = nullptr;
    QFile *dlFile = nullptr;
    QStringList pendingTools;
    QPointer<QProcess> proc;
    QString printFile;
    int part = 0;
    int lastPct = 0;
};

// One search result / queue entry from YouTube
struct YtItem {
    QString id;         // video id
    QString title;
    QString channel;
    int seconds = 0;
};

// YouTube songs sung on this computer (youtube-history.json next to the tools folder)
namespace YtHistory {
struct Entry {
    YtItem item;
    int count = 0;
    qint64 last = 0;        // msecs since epoch
};
void record(const YtItem &item);                // one more time sung
void setDuration(const QString &id, int seconds);
QVector<Entry> all();
QStringList genres();                           // the genre buttons of the home page
void setGenres(const QStringList &list);
}

class QListWidget;
class QListWidgetItem;
class QGridLayout;

// Search YouTube by song name (yt-dlp "ytsearch", no API key) and pick songs from a list.
// Opens on a home page: songs sung lately / most, YouTube's "Mix" after the current (or last)
// song, genre buttons and a random pick.
class YouTubeSearchDialog : public QDialog
{
    Q_OBJECT
public:
    // seed: the YouTube song the "next songs" follow (the playing one); empty = the last sung
    explicit YouTubeSearchDialog(QWidget *parent = nullptr, const YtItem &seed = YtItem());
    ~YouTubeSearchDialog() override;

signals:
    void playNow(const YtItem &item);
    void addToQueue(const YtItem &item);
    void downloadRequested(const QString &url);

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void search();
    void showHome();
    void loadSuggestions();
    void pickRandom();
    void buildGenreButtons();
    void editGenres();
    bool ensureYtdlp();
    YtItem currentItem() const;
    void addHeader(const QString &text);
    QListWidgetItem *addSong(const YtItem &it, const QString &extra = QString());
    void loadThumb(const QString &id);
    void pumpThumbs();
    QProcess *startYtdlp(const QStringList &args);

    QLineEdit *edSearch;
    QCheckBox *chkKaraoke;
    QListWidget *list;
    QLabel *lbStatus;
    QWidget *genreBox;
    QGridLayout *genreGrid;
    QPointer<QProcess> proc;            // search
    QPointer<QProcess> mixProc;         // suggestions
    QString thumbDir;
    YtItem seed;
    QVector<YtItem> suggestions;        // the "next songs" of the home page
    bool homeMode = false;
    QStringList pendingThumbs;
    int activeThumbs = 0;
};

#ifdef PTHK_WEBENGINE
#include <QWebEngineView>
#include <QTimer>

// Plays one YouTube clip with YouTube's own embedded player, filling the karaoke screen.
// Interface like the media player: position / duration in ms, ended, failed.
class YouTubePlayer : public QWebEngineView
{
    Q_OBJECT
public:
    explicit YouTubePlayer(QWidget *parent);

    void playVideo(const QString &videoId, int volume);
    void pause();
    void resume();
    void stopVideo();
    void seek(qint64 ms, bool allowSeekAhead = true);
    void setVolume(int volume);
    bool isPaused() const { return paused; }
    // the clip of the last playVideo() has started (until then the view may still show the previous one)
    bool isStarted() const { return active && started; }

signals:
    void durationChanged(qint64 ms);
    void positionChanged(qint64 ms);
    void ended();
    void failed(const QString &message, bool canDownload);

private:
    void poll();

    QTimer pollTimer;
    bool paused = false;
    bool active = false;
    bool started = false;
    qint64 duration = 0;
    int waitedMs = 0;
    bool pageReady = false;
};
#endif

#endif // YOUTUBE_H
