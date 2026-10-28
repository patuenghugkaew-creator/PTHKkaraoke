#ifndef CONFIG_H
#define CONFIG_H

#include <QDir>


#ifdef Q_PROCESSOR_X86_64
#define VST_CHECKER_NAME "VSTChecker_x64.exe"
#else
#define VST_CHECKER_NAME "VSTChecker_x86.exe"
#endif


#define TEMP_DIR_PATH           QDir::tempPath() + "/PTHKKaraoke"
#define TEMP_MIDI_DIR_PATH      TEMP_DIR_PATH + "/temp.mid"

#define ALL_DATA_DIR_PATH       QDir::homePath() + "/.PTHKKaraoke"


#define APP_PLAYLIST_FILTER_TEXT  "Playlist file (*.bmpl)"
#define APP_PLAYLIST_HEADTEXT     "PTHK Karaoke playlist file"
#define APP_PLAYLIST_OLD_HEADTEXT "KaraMixer playlist file"     // playlists saved before the rename still open
#define APP_PLAYLIST_VERSION      "Version="
#define APP_PLAYLIST_VERSION_1    "Version=1"
#define APP_PLAYLIST_VERSION_2    "Version=2"
#define APP_PLAYLIST_NAME         "Name="
#define APP_PLAYLIST_COUNT        "Count="
#define APP_PLAYLIST_SONGLIST     "[Song list]"
#define APP_PLAYLIST_SONG         "Song="
#define APP_PLAYLIST_END_SONGLIST "[End song list]"


class Config
{
public:
    static QString DATABASE_DIR_PATH;
    static QString DATABASE_FILE_PATH;

    static QString CONFIG_DIR_PATH;
    static QString CONFIG_APP_FILE_PATH;
    static QString CONFIG_SYNTH_FILE_PATH;

    static void initConfigDataPath();
    static bool isPortableMode();
    static QString relocatePath(const QString &savedPath);
};

#endif // CONFIG_H
