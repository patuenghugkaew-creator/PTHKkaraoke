#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QKeyEvent>
#include <QLocale>
#include <QSettings>
#include <QPointer>
#include <QDialog>
#include <QTranslator>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QLabel>
#include <QPushButton>
#include <QElapsedTimer>
#include <functional>
#include <deque>
#include <QImage>

#ifdef _WIN32
#include <QWinTaskbarButton>
#include <QWinTaskbarProgress>
#endif

#include <Background.h>
#include <LyricsWidget.h>
#include <Detail.h>
#include <ChannelMixer.h>

#include "SongDatabase.h"

#include "Midi/MidiPlayer.h"

#include "Dialogs/SynthMixerDialog.h"
#include "Dialogs/SecondMonitorDialog.h"
#include "Widgets/VideoFrameSurface.h"
#include "Dialogs/YouTube.h"


class MedleyLoader;
class RemoteServer;
class SongRequestServer;
class QFrame;
class MixerAutoHide;
class DmxLights;
class YtAudio;
class AiVocalEngine;


namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    SongDatabase* database() { return db; }
    MidiPlayer* midiPlayer() { return player; }
    Background *backgroundWidget() { return bgWidget; }
    LyricsWidget* lyricsWidget() { return lyrWidget; }
    LyricsWidget* secondLyrics() { return secondLyr; }
    SecondMonitorDialog* secondMonitorDlg() { return secondMonitor; }

    bool removeFromPlaylist() { return remove_playlist; }
    bool autoPlayNext() { return auto_playnext; }
    int searchTimeout() { return search_timeout / 1000; }
    int playlistTimout() { return playlist_timeout / 1000; }
    void setRemoveFromPlaylist(bool r) { remove_playlist = r; }
    void setAutoPlayNext(bool p) { auto_playnext = p; }
    void setSearchTimeout(int s) { search_timeout = s*1000; }
    void setPlaylistTimeout(int s) { playlist_timeout = s*1000; }

    // Synth Mixer
    SynthMixerDialog* synthMixerDialog() { return synthMix; }
    // Auto-hide of the mixer while the karaoke program is used (Settings); delay in seconds
    void setMixerAutoHide(bool on, double delaySec, const QString &program);
    QWidget* dialogParent();
    void showContextMenuAt(const QPoint &globalPos);
    void saveConfigFile();
    void openConfigFile();
    void applyConfigLive();   // apply the saved config files to the running program (no restart)
    void showSavedWindowState();   // show the player normal / maximized / full screen as last closed
    void playVideo(const QString &path, bool isVideo = true);   // songtype "VIDEO", or "MP3" (+ .lrc lyrics)
    QString searchWithThaiFallback(const QString &text);   // returns the text actually searched

    // MP3 songs played through the synth's main mixer (BASS)
    bool startMp3Stream(const QString &path);
    bool startMp3Crossfade(const QString &nextPath, double durationSec = 3.0, int curve = 0);
    void stopMp3Stream();
    void setMp3Volume();
    qint64 mp3PositionMs();

    // Karaoke recording (music + microphone, F12) and vocal score
    void showHelpDialog();     // F1: shortcut keys and how to use the player
    QStringList importSongs(const QStringList &files, bool quiet = false);
    void showImportDialog();
    void showYouTubeDownload(const QString &url = QString());
    bool isSongPlaying();
    void updateNewSongs();
    void showArtistCodeDialog();
    void refreshArtistCodes();          // new songs get their codes by artist (when codes are in use)
    void showYouTubeSearch();
    void addYouTubeToQueue(const YtItem &item, bool playNow);
    void playYouTube();
    void showDmxDialog();
    void toggleRecording();
    void stopRecording();
    void setVocalScore(bool on);
    void updateMicUse();
    void resetScore();
    void showScore();

    // Song transitions (settings: "การต่อเพลง"): fade in / out, and end a song some seconds after
    // its last lyric; the next song then starts at once.
    int effectiveVolume() const;        // volume slider x fade (0 when muted)
    void applyVolume();
    void startFade(double to, int ms, std::function<void()> done = nullptr);
    void songFinished();                // a song came to its end: the next one
    void endSongSmoothly();             // End key / "จบเพลง": fade out first
    void checkEndAfterLyrics(qint64 pos, qint64 remainingMs);
    void loadTransitionSettings();
    void showTransitionDialog();

public slots:
    void play(int index, int position = 0);
    void pause();
    void resume();
    void stop();
    void playNext();
    void playPrevious();

signals:
    void resized(const QSize &s);

protected:
    void showEvent(QShowEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void resizeEvent(QResizeEvent *event);
    void closeEvent(QCloseEvent *event);
    void keyPressEvent(QKeyEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    bool eventFilter(QObject *watched, QEvent *event);   // shortcuts from the mixer window
    void dragEnterEvent(QDragEnterEvent *event);     // drop song files / folders to add them
    void dropEvent(QDropEvent *event);

private:
    void loadNextMedley(Song *song);
    void openMedleySettings();
    void addToPlaylist(Song *song);
    void removeFromPlaylist(int index);
    void swapInPlaylist(int index, int toIndex);
    static void updateShutdownRequest();

private slots:
    void showFrameSearch();
    void showPlaylist();
    void showSongDetail();
    void showChMix();
    void onChMixMouseLeaved();
    void onChMixLockChanged(bool lock);
    void hideUIFrame();

    void showCurrentTime();
    void setFrameSearch(Song* s);

    void showContextMenu(const QPoint &pos);
    void showSettingsDialog();
    void showRemoteDialog();
    void showEqDialog();
    void showChorusDialog();
    void showReverbDialog();
    void showMapMidiChannelDialog();
    void minimizeWindow();
    void showMapSFDialog();
    void showSecondMonitor();
    void showFullScreenOrNormal();
    void setThaiLang();
    void setEngLang();
    void showCheckUpdateDialog();
    void showAboutDialog();
    void showAboutQtDialog();

    void showBusGroupDialog();
    void showSpeakerDialog();
    void showVSTDirDialog();

    void onPositiomTimerTimeOut();
    void onLyricsTimerTimeOut();
    void onPlayerDurationMSChanged(qint64 d);
    void onPlayerPositionMSChanged(qint64 p);
    void onPlayerDurationTickChanged(int d);

    void onSliderPositionPressed();
    void onSliderPositionReleased();

    void on_btnVolumeMute_clicked();
    void on_btnPlay_clicked();
    void onSliderVolumeValueChanged(int value);

    void switchMedleyLyrics();
    void switchMedleyLyrics2();
    void onNextMedleyStarted();
    void onNextMedleyAfterStarted();
    void onMedleyCountIn(int beat, int beatsPerBar, int bar, int totalBars);
    void onPlayerThreadFinished();

    void onDbUpdateChanged(int v);
    void onDetailTimerTimeout();

    void addBpmSpeed(int speed);
    void preSetBpmSpeed(int speed);
    void preSetTranspose(int transpose);

    void sendDrumPads(QKeyEvent *key, bool noteOn);

    void savePlaylist();
    void loadPlaylist();

private:
    Ui::MainWindow *ui;
    QSettings *settings;
    bool skipSaveOnExit = false;
    bool startMaximized = false;
    bool startFullScreen = false;
    QMediaPlayer *videoPlayer;
    FrameVideoWidget *videoWidget;
    bool videoActive = false;   // a video or MP3 song is loaded (playing or paused)
    bool mediaIsAudio = false;  // ... and it is an MP3 song (lyrics from its .lrc)
    DWORD mp3Stream = 0;        // BASS decode stream of the MP3 song in the main mixer, 0 = none
    DWORD mp3StreamNext = 0;    // Next MP3 stream for dual-deck crossfade
    QTimer *mp3Timer;
    QTimer *mp3CrossfadeTimer = nullptr;
    qint64 mp3CrossfadeElapsedMs = 0;
    int    mp3CrossfadeTotalMs = 3000;
    int    mp3CrossfadeCurve = 0;

    bool vocalScore = false;
    int melodyNote = -1;        // sounding note of the vocal melody (channel 9), -1 = none
    int scoreFrames = 0;        // 50 ms steps with a melody note
    int scoreHits = 0;          // ... of which the singer was on pitch
    QTimer *scoreTimer;
    QLabel *scoreLabel;
    DmxLights *dmxLights;
    bool ytActive = false;      // a YouTube song is playing (online, in ytPlayer)
    int ytSilenceCount = 0;
#ifdef PTHK_WEBENGINE
    YouTubePlayer *ytPlayer = nullptr;
    QTimer *ytMirrorTimer = nullptr;

    // YouTube sound through the main mixer (vocal cut, key, EQ); off where Windows can't do it
    YtAudio *ytAudio = nullptr;
    QElapsedTimer ytAudioTry;           // looking for the browser's audio process: not too often
    QElapsedTimer ytQuietClock;
    bool ytAudioWarned = false;
    bool ytAudioLive() const;           // captured, muted at the speakers, playing in the mixer
    void updateYtAudio();

    // YouTube vocal cut: AI (separation, sound and picture a few seconds late) or fast
    AiVocalEngine *aiEngine = nullptr;
    bool ytAiLatched = false;           // AI was used: it keeps running, so the cut key works at once
    void applyYtVocalCut();             // after the cut key / settings changed
    void showYtVocalDialog();
    struct DelayedFrame { qint64 t; QImage img; };
    std::deque<DelayedFrame> delayedFrames;     // the YouTube picture kept for the AI delay
    QElapsedTimer frameClock;
    void changeYtKey(int step);

    // song transitions
    int fadeInMs = 1000;
    int fadeOutMs = 3000;
    bool endAfterLyrics = true;
    int endAfterLyricsSec = 8;
    QTimer *fadeTimer = nullptr;
    double fadeGain = 1.0, fadeFrom = 1.0, fadeTo = 1.0;
    int fadeMs = 0;
    QElapsedTimer fadeClock;
    std::function<void()> fadeDone;
    bool endingSong = false;            // fading out to the end of the song
    long lastLyricPos = -2;             // -2 = not looked up yet, -1 = song without lyrics
    bool afterLyrics = false;
    QElapsedTimer afterLyricsClock;
    int midiBaseVolume = -1;
#endif
    QPushButton *btnMuteVocal;
    QPushButton *btnMedley = nullptr;
    QPushButton *btnSecondScreen = nullptr;
    SongDatabase *db;
    QTimer *timer1, *timer2, *positionTimer, *lyricsTimer;
    QTimer *detailTimer;

    QList<Song*> playlist;
    MidiPlayer *player;
    Song playingSong;
    int playingIndex = -1;
    bool playAfterSeek = false;
    bool searchBoxChangeBpm = false;

    bool nextMedleyRequested = false;

    MedleyLoader *medleyLoader = nullptr;
    RemoteServer *remoteServer = nullptr;

    // song requests from phones (customers ask, the owner approves)
    SongRequestServer *reqServer = nullptr;
    QFrame *reqToast = nullptr;         // the requests waiting for approval, top right on the PC
    void ensureRequestServer();
    void showSongRequestDialog();
    void updateRequestToast();
    QPointer<QDialog> remoteDialog;

    Background *bgWidget = nullptr;
    LyricsWidget *lyrWidget, *secondLyr = nullptr;
    Detail *updateDetail;

//    int bgType = 0;
//    QString bgImg = "", bgColor = "#525252";
    bool remove_playlist = true;
    bool auto_playnext = true;
    int search_timeout = 5000;
    int playlist_timeout = 5000;
    int songDetail_timeout = 4000;

    QLocale locale;

    SecondMonitorDialog *secondMonitor = nullptr;

    // Synth Mixer
    SynthMixerDialog *synthMix;
    MixerAutoHide *autoHide = nullptr;

    #ifdef _WIN32
    QWinTaskbarButton *taskbarButton;
    #endif

    bool firstShow = true;

    QString currentLang = "th";
    QTranslator translator;

};

#endif // MAINWINDOW_H
