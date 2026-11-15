#ifndef MAINWINDOW_H
#define MAINWINDOW_H

class VocalRemover;

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
#include <QSet>
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
namespace Updater { struct Release; }


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

    // MIDI routing by port name (MainWindow_Midi.cpp). Applied now and every 2 s: ports that show up
    // later (loopMIDI started after PTHK) are connected then, and a missing port is never forgotten.
    struct MidiRoute { QString port; bool dual = false; };   // port: "" = follow the main out, midiSynthPortKey() = SoundFont
    static QString midiSynthPortKey();
    QString midiOutWant() const;
    void setMidiOutWant(const QString &name);
    QString midiInWant() const;
    void setMidiInWant(const QString &name);
    MidiRoute midiChannelWant(int ch) const;
    void setMidiChannelWant(int ch, const MidiRoute &r);
    void applyMidiRouting();
    void showMidiMapper();              // "แยกเสียงมิดี้ (MIDI Mapper)" window
    bool isMidiRoutedOut();             // anything set to go to an external port
    void resetMidiRoutingToSoundfont(); // everything back to the SoundFont (MIDI in is kept)
    void saveSynthMixerSettings();
    MidiPlayer* midiPlayer() { return player; }
    Background *backgroundWidget() { return bgWidget; }
    void showSlideshowDialog();         // background slideshow settings (MainWindow_Menu.cpp)
    // medley sets: named song lists + their medley tempo / bridge (MainWindow_Playlist.cpp)
    QStringList medleySetNames() const;
    void saveMedleySet();
    void loadMedleySet(const QString &name);
    void manageMedleySets();
    bool editMedleySet(const QString &name);    // empty name = a new set; true when saved
    void applyMedleyToPlayingSong();
    void applySlideshowFromSettings(Background *bg);
    void setBackgroundVideoFolder(const QString &dir);   // video background on both screens
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

    // Video songs (e.g. downloaded YouTube clips) audio through BASS main mixer for Vocal Cut & Pitch
    bool startVideoAudioStream(const QString &path);
    void stopVideoAudioStream();
    void setVideoAudioVolume();
    void applyVideoVocalCut();
    void enqueueAiStems(const QString &video);  // AI separation of a song file, in the background
    void ensureAiStems();                       // the separation thread (made on first use)
    void updateAiStemsBackground();             // background separation only while no video / YouTube plays
    void resumeAiStems();                       // at start: go on with downloaded clips not separated yet
    bool playYouTubeDownloaded();               // AI cut + "download first": play a local copy of the YouTube song
    void playLocalCopy(const QString &file, qint64 atMs = 0, bool paused = false);
    void cancelYtFetch();
    class QProcess *ytFetch = nullptr;          // downloading the YouTube song about to play
    QString ytFetchId;                          // the YouTube song being downloaded
    bool ytFetchOnline = false;                 // playing online while its copy downloads (no cut until the AI is ready)
    int ytFetchPct = 0;                         // download progress of that copy
    // Picture size of the copy by the internet speed (the AI needs the sound only; a 720p picture
    // took 1.5-3 min at 150-280 KB/s, measured 2026-11-14): the speed of each download is
    // remembered (setting YtNetSpeedKB) and the next copy is 720p / 480p / 360p by it.
    double ytFetchSpeedSum = 0;                 // speed samples of the download (bytes/s)
    int ytFetchSpeedN = 0;
    // waiting = the song waits for this copy (or plays online meanwhile): a small picture, there
    // in ~15 s; not waiting = a queued song with time to spare: a better picture
    int ytCopyHeight(bool waiting);
    int ytFetchHeight = 0;                      // picture height of the copy downloading now
    void ytRememberSpeed(double kbps);
    bool ytFetchConverting = false;             // its sound is being made into AAC (YtTools::convertForWindows)
    QString ytFetchTemp;                        // the .mkv being converted (removed when cancelled)
    void ytCopyReady(const QString &id, const QString &file);
    // The copy is ready but the song goes on online (no break): the AI separates the copy from where
    // the song is, and playing moves to the copy only when the cut is on and the AI is ready there.
    QString ytCopyPending;
    QString ytFetchError;                       // yt-dlp's last ERROR line (for the log)
    void ytFetchGaveUp(const QString &why);     // no copy for this song: the online cut works again
    // yt-dlp + sound conversion of one clip into the YouTube folder (one at a time: ytFetch); done ->
    // ytCopyReady, failed -> ytFetchGaveUp. Used for the playing song and for songs in the queue.
    void startYtFetch(const QString &id, bool waiting = true);
    // "Wait until ready": a YouTube song with the cut on and no AI sound yet does not play online; it
    // waits (progress on the cut button and in the detail box) for its copy and the AI's first seconds,
    // then plays from the copy, cut from the first note. Too slow (2 min) / failed: online as before.
    QString ytPrepId, ytPrepFile;
    QElapsedTimer ytPrepClock;
    int ytPrepPct = 0;
    bool ytSkipPrepOnce = false;
    void startYtPrep(const QString &id, const QString &file);
    void ytPrepTick();
    void ytPrepGiveUp(const QString &why);
    bool aiReadyAtStart(const QString &file);
    // songs in the queue are prepared while others play: their copies downloaded, their AI made
    QSet<QString> ytPrefetchFailed;
    void ytQueueTick();
    bool ytCopyAiReadyAt(qint64 ms, int *percent);  // the AI of that copy at a play position
    void ytCopyPendingTick();
    bool aiPrepareVideo() const;                // the AI separates the playing video song now
    void updateCutButton();                     // the cut button shows whether the AI cut is ready
    void applyCornerDisplay();                  // clock / beat box shown or hidden (settings)
    void styleSearchBar();                      // the song search bar: see-through, rounded, at the left
    void fitSearchBar();                        // its width follows the song shown
    void fitQueue();                            // the queue: at the left, as tall as its songs
    void updateBarStatus();                     // the bar: song line, play / pause, key, drum hold, AI ring
    void changeSongKey(int step);               // key down / up for any kind of song
    // per-song key / speed, remembered automatically (song_prefs in the song database)
    void applyRememberedPrefs(Song *s);         // a song entering the queue: its remembered key / speed
    int  rememberedKey();                       // the playing song's key: its own, else the remembered one
    void rememberPlayingKey(int transpose);
    void rememberPlayingSpeed(int bpmSpeed);
    // same loudness for every song (Midi/SongLevel.h; MainWindow_Audio.cpp)
    void startSongLevel();                      // a song starts: its gain from its measured loudness + its own dB
    void finishSongLevel();                     // it ends: a first measurement during play is remembered
    void changeSongLevel(double stepDb);        // Alt + Up / Down: this song louder / quieter (remembered)
    void measureSongFile(Song *s);              // a video / MP3 song queued: measured in the background
    void onSongFileMeasured(const QString &type, const QString &path, double lufs);
    void applySongLevelGain(double glideMs);
    double autoLevelDbFor(double lufs) const;
    void setSongLevelOn(bool on);
    void setSongLevelTarget(double lufs);
    // sound pads ("แป้นเสียงเชียร์", Midi/SoundPads.h): Alt + 1..9 and the on-screen panel
    void ensureSoundPads();
    void triggerSoundPad(int index);
    void nextSoundBank();                       // Alt + 0
    void stopAllSoundPads();                    // Alt + Backspace
    void showSoundPadsPanel(bool show);
    void openSoundPadsSettings();
    void openSoundFxManager();                  // "เพิ่มเสียงเอฟเฟกต์": the shop's own sound files
    // scrolling text ("ข้อความวิ่ง", Widgets/TickerBar.h): the shop's messages + the next songs
    void updateTicker();
    void openTickerSettings();
    QString tickerText();
    bool songIsRunning();                       // a song plays (not paused, not stopped)
    void startSibBackfill();
    // automatic update (GitHub Releases)
    void checkForUpdate(bool manual);
    void offerUpdate(const Updater::Release &r, bool manual);
    void installUpdate(const Updater::Release &r);
    bool updateChecking = false;
    QString nextMedleyTitle;            // "name - artist" of the medley song loaded next
    // drum hold: blinking banner on screen while the band waits (see showDrumHoldBanner)
    QLabel *drumHoldBanner = nullptr;
    QTimer *drumHoldBlink = nullptr;
    void showDrumHoldBanner(int state);
    // control bar: buttons that do not fit go into the "more" button's menu
    QList<QAbstractButton *> barButtons;       // bar buttons that go into "more" when the window is narrow
    class BarButton *btnBarMore = nullptr;
    class BarButton *btnQueue = nullptr;
    bool fitBarQueued = false;
    void fitControlBar();
    void toggleDrumHold();
    void songUnplayable(const QString &reason);
    // deleting a song (a wrong copy among same-named songs): out of the song list and the queue,
    // its files to the Recycle Bin if asked
    void deletePlayingSong();                   // the song playing (right-click menu, Ctrl+Delete); the next one plays
    void deleteSearchSong();                    // the song shown in the search bar (Ctrl+Delete)
    bool songLoaded();                          // a song is playing or paused
    QString songFilePath(Song s);               // the file a song in the list plays from ("" if not found)
    bool confirmDeleteSong(Song s, const QString &file, bool *toTrash);
    void trashSongFiles(Song s, const QString &file);
    void trashFilesRetry(QStringList left, int tries);
    QString ytCopyOf(const QString &id);        // the downloaded copy of a YouTube song ("" if none)
    void ytForgetCopy(const QString &id);
    void changeVideoKey(int step);
    qint64 videoAudioPositionMs();
    static void CALLBACK videoVocalDspCallback(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user);
    static void CALLBACK videoSetPosSync(HSYNC handle, DWORD channel, DWORD data, void *user);

    // Karaoke recording (music + microphone, F12) and vocal score
    void showHelpDialog();     // F1: shortcut keys and how to use the player
    QStringList importSongs(const QStringList &files, bool quiet = false);
    QStringList importSongsImpl(const QStringList &files, bool quiet);
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
    void openAutoMedley();
    void openEasyMedley();
    void turnOffMedley();
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

    DWORD videoAudioStream = 0;
    DWORD videoAudioTempo = 0;
    class StemMixer *videoStemMixer = nullptr;  // vocal cut of video / MP3 songs: AI parts + fast cut (AiStems.h)
    QString videoAudioFile;                     // the file videoAudioStream plays
    bool aiCutNoticeShown = false;              // "AI is preparing" shown for this song
    class AiStemMaker *aiStems = nullptr;       // background AI separation of video songs
    class SibBackfill *sibFill = nullptr;
    // "ค้นจากเนื้อร้อง" (LyricsIndex.h): the index builder, and the search running in the background
    class LyricsIndex *lyricsIndex = nullptr;
    QTimer *lyricsSearchTimer = nullptr;        // waits for a pause in the typing
    class QThreadPool *lyricsPool = nullptr;
    int lyricsSearchGen = 0;                    // only the newest search's answer is shown
    QString lyricsSearchText;
    class SearchModeChips *searchChips = nullptr;
    class SearchResultsList *searchResults = nullptr;
    QString searchFooter;
    void setSearchMode(SearchType mode);
    void updateSearchResults();
    void startLyricsIndex();
    void searchLyricsLater(const QString &text, bool quiet = false);
    void onLyricsFound(const QString &text, const QVector<qint64> &rows, qint64 ms);
    void showSearchMessage(const QString &msg);
    QString lyricsDbPath() const;
    class AudioBandMeter *audioMeter = nullptr;  // audio songs' levels for the synth mixer       // fills SIB lyrics preview / tempo in the background
    int   videoPitch = 0;

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
    int ytAiOverload = 0;               // 2-second periods in a row the AI was busy all the time
    bool ytAiLatched = false;           // AI was used: it keeps running, so the cut key works at once
    void applyYtVocalCut();             // after the cut key / settings changed
    void showYtVocalDialog();
    struct DelayedFrame { qint64 t; QPixmap img; };   // QPixmap: no copy/convert per frame
    std::deque<DelayedFrame> delayedFrames;     // the YouTube picture kept for the AI delay
    QElapsedTimer frameClock;
    bool ytInputGateOpen = true;               // false while switching YouTube clips (see playYouTube)
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
    // same loudness for every song (see startSongLevel)
    bool songLevelOn = true;                // settings "SongLevelOn"
    double songLevelTarget = -16.0;         // LUFS, settings "SongLevelTarget"
    QTimer *songLevelTimer = nullptr;       // first play: sets the gain once ~10 s of music was heard
    QString levelType, levelPath;           // the song being measured
    bool levelKnown = false;                // its loudness came from the database / its file
    bool levelAdjusted = false;             // measured while playing, gain set
    double levelAutoDb = 0.0;               // from its loudness
    double levelOffsetDb = 0.0;             // the operator's own (Alt + Up / Down)
    QSet<QString> levelMeasuring;           // "type|path" files being measured
    class QThreadPool *levelPool = nullptr;
    class SoundPads *soundPads = nullptr;
    class SoundPadsPanel *padsPanel = nullptr;
    QPointer<class TickerBar> tickerMain, tickerSecond;    // the 2nd one goes with the 2nd screen
    QTimer *tickerTimer = nullptr;
    class BarButton *btnMuteVocal = nullptr;
    class BarButton *btnPlayPause = nullptr;
    class BarButton *btnDrumHold = nullptr;
    class KeyControl *keyControl = nullptr;
    class KeyControl *tempoControl = nullptr;    // "ความเร็ว −/+" (MIDI songs)
    void changeSongTempo(int step);
    void openSearchBox();                        // the search button / arrow keys: open an empty search
    void pickSearchRow(int row, bool add);       // a click on the search results list
    int searchListFrom = 0;                      // result index of the list's first row
    void openPortableDrive();                    // "สร้างไดรฟ์พกพา" (Dialogs/PortableDriveDialog.h)
    void openEventSounds();                      // "เสียงพิธีการ / เอฟเฟกต์"
    void showEventSoundsQuickMenu(const QPoint &globalPos); // quick popup menu on the bar's "เอฟเฟกต์" button
    void showFirstRunGuide();                    // no songs yet: where to start
    int settingsStartTab = -1;                   // the next settings dialog opens on this tab
    bool simpleMenu() const;                     // right-click menu without the sound engineer's tools
    QLabel *barTitle = nullptr;                  // the bar's song line
    QLabel *barMeta = nullptr;
    bool cutWaiting = false;                     // the AI cut is not ready yet (ring on the cut button)
    class BarButton *btnMedley = nullptr;
    class BarButton *btnSecondScreen = nullptr;
    class BarButton *btnSubtitles = nullptr;
    class BarButton *btnShopLock = nullptr;

    // Screen lock for shops + automatic idle background music (see Dialogs/ShopModeDialog.h)
    QTimer *idleTimer = nullptr;
    QElapsedTimer idleClock;
    bool idlePlaying = false;
    bool idleSwitching = false;
    QString lastIdlePath;
    class IdleSmart *idleSmart = nullptr;       // smart idle music (IdleSmart.h, IdleMusic/Source 4)
    QString idleSeedKey;                        // the customer song the smart idle music follows now
    QString idleContextTag;                     // the context the idle song was picked for (shown on screen)
    // idle music keeps its singer (IdleMusic/KeepVocal) until the operator presses the vocal cut
    // during it: then the cut button rules again (until the next song)
    bool idleVocalByUser = false;
    bool keepIdleVocal() const;
    int importSkippedOther = 0;         // non-song files of a chosen / dropped folder (MainWindow_Import.cpp)
    QString sibImportFolder();
    // Is YouTube reachable? Checked in the background (checkYouTubeOnline: every minute, every 20 s
    // while it is not). Without it idle music plays library songs: a YouTube clip would wait 25 s on a
    // black screen before it is skipped.
    bool ytOnline = true;
    bool ytOnlineChecking = false;
    QElapsedTimer ytOnlineClock;
    void checkYouTubeOnline();
    // medley rules (locked tempo, intro / outro cut, bridge) for the song playing now: not idle music,
    // which plays at its own tempo, whole, with a fade-in
    bool songInMedley();
    bool isShopLocked() const;
    bool isShopStrictLocked() const;
    bool requireShopUnlock(const QString &actionName = QString());
    void notifyShopStrictLocked(const QString &actionName);
    void toggleShopLock();
    void openShopModeSettings();
    void updateShopLockUi();
    void checkIdleMusic();
    bool pickIdleSong(Song *out);
    void startNextIdleSong();
    SongDatabase *db;
    QTimer *timer1, *timer2, *positionTimer, *lyricsTimer;
    QTimer *detailTimer;
    QTimer *medleyRetempoTimer = nullptr;   // re-prepares the next medley song after a live tempo change

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
    int search_timeout = 15000;
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
