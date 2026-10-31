# PTHK Karaoke: map for AI assistants and developers

A Windows karaoke player and mixer. It plays MIDI karaoke (with SoundFonts and VST), EMK, SIB, video, MP3 and YouTube files. It has AI vocal removal, medley (non-stop) mode, a song database with search, remote song requests from phones, and DMX lights.

The program is written in C++ with Qt 5.15.2 (MSVC 2022, x64) and uses the BASS audio libraries. The interface text is Thai.

**Read this file first, then open only the files for the topic you are working on.**
The project is large (about 60,000 lines). Do not try to read everything.

## Build

| Part | Project | Output |
|---|---|---|
| Core library | `PTHKCore/PTHKCore.pro` | `PTHKCore.dll` (built in `../PTHK-Build/build-core`) |
| Program | `PTHKKaraoke.pro` | `PTHKKaraoke.exe` (built in `../PTHK-Build/build-karaoke`, links PTHKCore) |
| Installer | `_iss_setup/pthk-karaoke-x64.iss` (Inno Setup 6) | `dist/PTHK-Karaoke-<ver>-x64-setup.exe` |

```
qmake -o Makefile ..\..\PTHK-Karaoke\PTHKKaraoke.pro "CONFIG+=release"
nmake /nologo -f Makefile.Release
```

Notes:
- Run qmake again after adding or removing source files.
- If a class in PTHKCore changes its layout (for example `AiVocalEngine`), rebuild and ship `PTHKCore.dll` together with the exe.
- After changing `version.h`, delete `MainWindow*.obj` so the new version is compiled in. nmake does not always notice the change.
- Files that are not in the repository and must be downloaded:
  - `3rdParty/onnxruntime/lib/*.dll`: onnxruntime 1.24 for Windows x64 and DirectML.dll. The `.lib` file and headers are in the repository.
  - BASS DLLs for macOS and Linux are not needed.

## Where things are

### Main window (split by topic; all parts share `MainWindow_p.h`)

| File | What it does |
|---|---|
| `MainWindow.cpp` | Constructor (UI setup, timers, connections), destructor, `play()`, help dialog |
| `MainWindow_Import.cpp` | Adding songs: import, .zip/.rar archives (`unpackArchive`), drag & drop |
| `MainWindow_Audio.cpp` | Volume, YouTube audio and AI vocal cut, fades, ending a song smoothly (`checkEndAfterLyrics`), transitions |
| `MainWindow_Remote.cpp` | Song requests from phones, artist codes |
| `MainWindow_YouTube.cpp` | YouTube search, queue, `playYouTube()` |
| `MainWindow_Playback.cpp` | Video/MP3 playback, pause/resume/stop, next/previous, recording, score, DMX, window events |
| `MainWindow_Keys.cpp` | Keyboard shortcuts (`keyPressEvent`), search (`searchWithThaiFallback`, fuzzy), event filter |
| `MainWindow_Playlist.cpp` | Medley dialogs, playlist editing, on-screen frames |
| `MainWindow_Menu.cpp` | Right-click menu (`showContextMenuAt`), config save/load, effect dialogs, language |
| `MainWindow_Update.cpp` | Automatic update (see "Update" below) |
| `MainWindow_Player.cpp` | Player slots and timers, medley switching, tempo/key, drum pads, drum hold, SIB backfill, AI stems for videos |

`MainWindow.h` declares everything. Members are grouped with comments.

### MIDI engine (`Midi/`)

| File | What it does |
|---|---|
| `MidiPlayer.*` | Player facade: load song, play/stop, tempo/key, medley settings (`fitLockedBpm`) |
| `MidiSequencer.*` | Timing thread. It also handles the medley bridge (drum groove between songs), soft stop (`softStopEvents`) and drum hold (`toggleDrumHold`, `runDrumHold`) |
| `MidiSynthesizer.*` | BASSMIDI synth, SoundFonts, mixer, effects, ASIO |
| `RhythmStyleManager.*` | Drum bridge patterns from Yamaha style files (`Rhythms/`) |
| `AiVocal.*` | AI vocal removal (onnxruntime + DirectML, MDX-Net). Compiled into PTHKCore |
| `YtAudio.h`, `AiStems.h` | Live YouTube audio with vocal cut; stems for downloaded videos (`<video>.ai.wav`) |
| `MidiFile.*`, `HNKFile.h` | MIDI and NCN/HNK file readers |

### Songs and search

| File | What it does |
|---|---|
| `SongDatabase.*` | SQLite song database: insert, search (`SearchType`), remove |
| `SongFuzzy.h` | Typo-tolerant search: Thai/English key normalization and edit distance |
| `SibBackfill.h` | Background fill of lyrics preview and tempo for SIB songs |
| `MedleyLoader.*` | Loads the next medley song in the background |
| `PTHKCore/PTHKCore.cpp` | EMK and SIB decoding, protected SoundFonts, master DSP, licence (PRO) |

### Other

| Folder or file | What it does |
|---|---|
| `Dialogs/` | Dialogs: `MedleyDialog`, `YouTube`, `DuplicateSongsDialog`, `SynthMixerDialog` (mixer and meters), effects |
| `Widgets/` | Custom widgets: `LyricsWidget` (karaoke lyrics), `MenuIcons.h` (drawn menu icons), playlist, mixer strips |
| `BASSFX/`, `Vst3/` | Effects and VST2/VST3 hosting |
| `Remote/` | Phone remote and song request web server, QR code |
| `Dmx/` | DMX stage lights |
| `Config.*` | Settings paths and portable mode (settings next to the exe; paths fixed when the drive letter changes) |
| `Updater.h` | Update check and download (WinHTTP, GitHub Releases) |
| `main.cpp` | Startup: Qt Quick software backend, WebEngine flags, splash |

## Settings

INI files (QSettings):
- `PTHKKaraoke.conf`: app settings. Groups include `Medley/`, `Update/`, `Synth/`.
- `SynthMixer.conf`: mixer settings.

Location:
- Normal install: `%USERPROFILE%\.PTHKKaraoke\Config`.
- Portable install: `<program folder>\Config`.

## Update (how releases reach users)

1. Raise the version in `version.h`, build, and copy the files to `../PTHK-Build/deploy`.
2. Run `_iss_setup/publish-release.ps1` with `GITHUB_TOKEN` set. It builds the installer and creates a GitHub Release with the installer and a `SHA256:` line in the notes.
3. Installed programs check `releases/latest`, download the installer, verify SHA-256, run it silently in the same folder (`/update=1`), and the installer opens the program again.

## Rules for changes

- Write user-facing text in Thai. Comments are in English, short and plain.
- Do not block the UI thread. Heavy work (file scans, AI, network) runs on worker threads, and results come back with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- The MIDI sequencer runs on its own thread. Its signals reach `MidiPlayer` across threads, so check `QThread::currentThread()` and do not rely on `sender()`.
- BASSmix matrix size: [mixer outputs] x [input channels]. The main mixer has 8 outputs.
- Debug logs (`medley-timing.log`, `ui-timing.log`, `yt-video.log`, AI log) are written only when a file `debug-logs.txt` is next to the exe.
- Windows `min`/`max` macros: write `(std::min)(a, b)`.
- When suggesting a fix, name the file and function and keep the change small. Only one person or assistant should edit a file at a time.
