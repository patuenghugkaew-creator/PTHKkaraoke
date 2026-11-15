# AGENTS.md: start here (any AI assistant: Claude, Codex, Gemini, Cursor, Copilot ...)

PTHK Karaoke is a Windows karaoke player and mixer in C++ / Qt 5.15.2 (MSVC 2022, x64) with the BASS audio libraries.
The interface text is Thai. The owner speaks Thai: **always answer the owner in Thai**, simply, result first.

Read these two files before changing anything:

1. `CLAUDE.md`: the map of the code (which file does what), how to build, and the rules for changes.
2. `HANDOFF.md`: the current state, what is done, what is open and not yet verified, the owner's preferences, and the bugs not to repeat.
   Section 0 is the latest status and the next tasks.

The most important rules, in short (details in the two files):

- Big features: propose 2-3 designs with a recommendation and wait for the owner's choice. Clear bugs: fix them.
- Build only the program (`D:\REA\build-pthk.ps1`, output in `D:\REA\PTHK-Build\deploy`). Never build the installer (Setup.exe) unless the owner asks.
- After a build you may close the running program yourself, copy the new build into `deploy`, and then always start the program again.
- Never send key presses or clicks into the program while the owner is using it.
- Never put third-party songs, sound fonts or sound files (eXtreme Karaoke, SIBSIB) into the installer or this repository.
- Save source files as UTF-8 without a BOM. Do not block the UI thread. Report honestly what was tested and what was not.
- Keep `HANDOFF.md` up to date when you finish a piece of work, so the next assistant can continue.
