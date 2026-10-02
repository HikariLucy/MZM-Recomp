# MZM Recompiled Beta 3 — Public Runtime Test

*Metroid: Zero Mission* (USA) as a native, statically recompiled program. This is a
**beta test build**, published so more people can try it and tell us what breaks.
Windows in particular needs real-world validation.

| Platform | Status |
|---|---|
| Windows x64 | Windows x64 test build available — **community validation requested** |
| Linux x86_64 | Tested |

## What's new

- Everything from Beta 2, plus a new launcher flow that cannot silently fall back to a
  launcher-less program: ROM, BIOS, **PLAY**.
- **Enhancements menu (ESC)** in game: integer scaling, nearest / bilinear filtering, CRT
  presets (Off / Lite / Soft / Strong / Custom), an approximate GBA-like colour profile,
  EMU / PRESENT FPS readouts.
- **Borderless fullscreen**, toggled with **Alt+Enter**.
- **Controller navigation** of the Enhancements menu.
- **Native / Monitor Refresh presentation.** The game stays at its original ~59.73 Hz;
  Monitor Refresh re-presents the current frame on high-refresh displays. It does not create
  new gameplay frames (no interpolation), so "144 FPS" here means presents, not gameplay.
- **Original Metroid (NES)** integration and a **NES performance fix** (full-speed NES).
- A readable `latest.log` for every run (versions, validation results, renderer, display
  refresh, errors) and a crash marker if the program dies.
- The window and **About** page show **Beta 3** and the build's commit.

## How to run

1. Extract the archive to a writable folder.
2. Run `MZMRecomp` (`MZMRecomp.exe` on Windows).
3. Select your legally obtained **Metroid: Zero Mission (USA)** ROM
   (SHA-1 `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8`).
4. Select your own **GBA BIOS** (SHA-1 `300c20df6731a33952ded8c436f7f186d25d3492`).
5. **CONTINUE**, then **PLAY**.

No ROM or BIOS is included or linked. Linux needs `libSDL2-2.0-0` and OpenGL.
Windows: the program is **unsigned**; SmartScreen may warn. Read the antivirus note in `README.md`.
Saves (`.sav`) are written beside your ROM — back them up.

## What to test

Please try, in roughly this order, and mark each PASS / FAIL in `FEEDBACK.md`:

1. Launcher: both files show **Valid**, PLAY starts the game.
2. Title screen and a few minutes of gameplay; audio; controller/keyboard.
3. Save in game, close, reopen: the save is still there.
4. ESC → Enhancements menu; try scaling, filtering, CRT, Monitor Refresh.
5. Alt+Enter fullscreen toggle; navigate the menu with a gamepad.
6. *Optional:* the NES game, if your own save has it unlocked.

## Known limitations

- Windows runtime has not been validated by the developers — that is what this beta is for.
- Audio accuracy is not verified against a reference.
- GBA-like colour mode is approximate.
- Presentation refresh repeats frames; it does not interpolate.
- The NES game unlocks through the original game's progression (or your own compatible save).
- Not every part of the game has been covered yet.

Full list: `KNOWN-ISSUES.md`.

## How to report a bug

Fill in `FEEDBACK.md` and attach `latest.log`
(Windows `%LOCALAPPDATA%\MZMRecompiled\logs\`, Linux `~/.local/state/MZMRecompiled/logs/`;
**About → Open Logs Folder** opens it). Nothing is sent automatically: no telemetry,
no crash upload. Check the log for personal paths before sharing.
If your antivirus reacts, include the product, exact message, a screenshot and the
SHA-256 of the ZIP and of `MZMRecomp.exe` — please do not disable your antivirus.
