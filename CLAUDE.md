# PTHK Karaoke: map for AI assistants and developers

A Windows karaoke player and mixer. It plays MIDI karaoke (with SoundFonts and VST), EMK, SIB, video, MP3 and YouTube files. It has AI vocal removal, medley (non-stop) mode, a song database with search, remote song requests from phones, and DMX lights.

The program is written in C++ with Qt 5.15.2 (MSVC 2022, x64) and uses the BASS audio libraries. The interface text is Thai.

**Read this file first, then `HANDOFF.md` (current work, open items, lessons learned), then open only the files for the topic you are working on.**
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

Usual way: `D:\REA\build-pthk.ps1` (builds PTHKCore, VSTChecker and the program, copies everything into
`D:\REA\PTHK-Build\deploy`, no installer unless `-BuildInstaller`). Run it in a child PowerShell
(`powershell -NoProfile -ExecutionPolicy Bypass -File D:\REA\build-pthk.ps1`): qmake writes notes to stderr,
which stops a script started with `ErrorActionPreference Stop` in the same shell. It currently ends with
exit code 1 at the `onnxruntime.dll` copy (the DLLs are missing from `3rdParty/onnxruntime/lib`); the
exe is already in deploy by then. Close a running `PTHKKaraoke.exe` first (the copy fails while it runs).

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
| `MainWindow_YouTube.cpp` | YouTube search, queue, `playYouTube()`; with the AI cut, a YouTube song plays online while a copy downloads, then goes on from the copy (`playYouTubeDownloaded`, `youtube-local.json`) |
| `MainWindow_Playback.cpp` | Video/MP3 playback, pause/resume/stop, next/previous, recording, score, DMX, window events |
| `MainWindow_Keys.cpp` | Keyboard shortcuts (`keyPressEvent`), search (`searchWithThaiFallback`, fuzzy), event filter |
| `MainWindow_Playlist.cpp` | Medley dialogs, playlist editing, on-screen frames |
| `MainWindow_Menu.cpp` | Right-click menu (`showContextMenuAt`), config save/load, effect dialogs, language |
| `MainWindow_Update.cpp` | Automatic update (see "Update" below) |
| `MainWindow_Midi.cpp` | MIDI routing by port name (`applyMidiRouting`, every 2 s): main out, MIDI in, channels 1-16; instruments via `MidiSynthesizer::reconnectExt`. Never sends to the MIDI-in port (loop) |
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
| `YtAudio.h` | Live YouTube audio with vocal cut (online play: AI with a delayed picture) |
| `AiStems.h` | AI stems of video / MP3 files (`<file>.ai.wav`), made window by window from the play position, resumable (`.ai.part.wav` + `.ai.part.map`); `StemMixer` plays AI parts and the fast cut elsewhere |
| `MidiFile.*`, `HNKFile.h` | MIDI and NCN/HNK file readers |
| `AudioBandMeter.h` | Audio songs on the synth mixer: strip meters by frequency band and an 18-band EQ driven by a separate audio-song fader set (`SynthMixerDialog::setAudioMode`, switched by the meter timer in `MainWindow.cpp`) |
| `SongLevel.h` | Same loudness for every song: BS.1770 meter + one steady gain per song, a DSP on the main mixer before the master chain (`MainWindow::startSongLevel`, Alt+Up/Down) |
| `SoundPads.h` | Sound pads (Alt+1..9, Alt+0 next bank, Alt+Backspace stop): built-in sounds on their own BASSMIDI stream, sound files, banks from folders (`<data>\SoundFx` = the shop's own, `<exe>\Sounds` = PTHK's, other karaoke programs' `SoundFx`/`FX` found on the disks) |
| `MasterDsp.h` | Header-only mastering chain / auto level / limiter, compiled into PTHKCore |

### Songs and search

| File | What it does |
|---|---|
| `SongDatabase.*` | SQLite song database: insert, search (`SearchType`, its order = the mode buttons), remove, `song_prefs` (per-song key / speed / loudness / own dB), list results (`showRows`, `resultWindow`, `currentRows`) |
| `SongFormats.h` | The song file types PTHK plays, one list for the file picker, adding songs (files / folders / drop), the library scan and the idle music folder. Tested: sound mp3 wav flac m4a aac ogg wma; video mp4 mkv avi mpg mpeg wmv vob dat mov m4v ts (not webm / flv / opus) |
| `SongFuzzy.h` | Typo-tolerant search: Thai/English key normalization and edit distance |
| `SibBackfill.h` | Background fill of lyrics preview and tempo for SIB songs |
| `LyricsIndex.h` | Full-lyrics index `<data>\Lyrics.db3` (NCN/EMK/SIB, ~321k songs, ~200 MB), built in the background, searched with `instr()` on one-byte (TIS-620) bodies |
| `Widgets/SearchPanel.h` | Search mode buttons (`SearchModeChips`) and the results list under the search bar (`SearchResultsList`) |
| `MedleyLoader.*` | Loads the next medley song in the background |
| `IdleSmart.h` | Smart idle music: follows tonight's customer songs (YouTube Mix via yt-dlp) + YouTube's Thai chart; library song if name + artist match, else plays from YouTube (`MainWindow::pickIdleSong`, source 4) |
| `PTHKCore/PTHKCore.cpp` | EMK and SIB decoding, protected SoundFonts, master DSP, licence (PRO) |

### Other

| Folder or file | What it does |
|---|---|
| `Dialogs/MidiMapperDialog.h` | "แยกเสียงมิดี้ (MIDI Mapper)": per instrument / drum piece / channel to the SoundFont or a port (loopMIDI), send-only or send + SoundFont, activity lights, sets as `.pthkmap` (Config/MidiMaps) |
| `Dialogs/MultiSoundfontDialog.h` | Multi soundfont: per mixer preset and instrument the soundfont, the sound in it and its level; preview; export / import a set (`.pthksf` + the .sf2 files). Engine: `MidiSynthesizer::setMapSoundExtras` / `setSoundfontPresets` |
| `Dialogs/EventSoundsDialog.h` | "เสียงพิธีการ / เอฟเฟกต์" (the bar's "อื่น ๆ" button): category folders under `<data>\..\เสียงพิธีการ`, plus every sound pad bank's files grouped by kind (`groupOf`), dark card UI |
| `Dialogs/PortableDriveDialog.h` | "สร้างไดรฟ์พกพา": copies the program, settings, databases (`VACUUM INTO`), song libraries, sound fonts, banks and VSTs to `<drive>\PTHK Karaoke` and points the copied settings into the drive; a second run copies only changed files |
| `Dialogs/` | Dialogs: `MedleyDialog`, `YouTube`, `DuplicateSongsDialog`, `SynthMixerDialog` (mixer and meters), effects, `SoundPadsDialog.h` (pad settings + on-screen pad panel), `SoundFxManager.h` ("เพิ่มเสียงเอฟเฟกต์"), `TickerDialog.h` |
| `Widgets/` | Custom widgets: `LyricsWidget` (karaoke lyrics; `applyBrand`: the karaoke maker's name at the start of a song shown as PTHK KARAOKE, phone numbers hidden), `MenuIcons.h` (drawn menu icons), playlist, mixer strips, `TickerBar.h` (scrolling text on both screens) |
| `Sounds/` | PTHK's own sound effects (shipped), made by `tools/make_soundfx.cpp` (synthesized, no third-party audio) |
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
- Save source files as UTF-8 **without** a BOM. The build has no `/utf-8`: with a BOM, MSVC converts the Thai text to code page 874 and `tr()` / `fromUtf8` show garbage (warning C4566).
- Random numbers: `QRandomGenerator::global()`, not `qrand()` (never seeded here: the same order every start).
- Shortcuts with digits / letters: read the key from `event->nativeVirtualKey()`, not `event->key()`. With the Thai keyboard layout the 1 key gives "ๅ", so `Qt::Key_1` never comes.
- Qt 5.15's SQLite returns too many rows for `LIMIT n OFFSET m` on the `UNION ALL` search queries. Use `LIMIT` alone and skip / count rows in C++ (see `SongDatabase::resultWindow`).
- New small dialogs and widgets are header-only classes without `Q_OBJECT` (lambdas and `std::function` callbacks), like `MasteringDialog.h`.
- Never ship third-party audio (eXtreme / SIBSIB sound files, their soundfonts) in deploy or the installer.
- When suggesting a fix, name the file and function and keep the change small. Only one person or assistant should edit a file at a time.
