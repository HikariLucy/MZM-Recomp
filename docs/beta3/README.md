# MZM Recompiled — Beta 3 (Public Runtime Test)

An independent fan/research project: *Metroid: Zero Mission* (USA) statically
recompiled to a native program. Not affiliated with or endorsed by Nintendo.
This is a **test build** for community feedback.

| Platform | Status |
|---|---|
| Windows x64 | **Windows x64 test build available — COMMUNITY VALIDATION REQUESTED.** It was cross-built and statically inspected, but has not been run on Windows by the developers yet. |
| Linux x86_64 | **TESTED** (see KNOWN-ISSUES.md for scope). |

## Quick start

1. **Extract** the archive to a folder you can write to.
2. **Run `MZMRecomp`** (`MZMRecomp.exe` on Windows, `./MZMRecomp` on Linux).
3. **Select your legally obtained, supported *Metroid: Zero Mission* (USA, revision 0) ROM.**
   Expected SHA-1: `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8`
4. **Select your own GBA BIOS.** Expected SHA-1: `300c20df6731a33952ded8c436f7f186d25d3492`
5. Press **CONTINUE**, then **PLAY**.

No ROM, no BIOS and no game data are included, and none is offered here.
Both files must show **Valid** in the launcher before PLAY is enabled.

Press **ESC** while playing to open the Enhancements menu.

Linux needs SDL2 (`libSDL2-2.0-0`), OpenGL and a desktop session. Windows needs
nothing extra: the required DLLs are in the folder.

## What is in Beta 3

- Native static recompilation of *Metroid: Zero Mission* (no interpreter fallback in the game path).
- Gameplay, save persistence (the game's own `.sav`) and the in-game Original Metroid (NES) integration.
- NES performance fix: the NES section runs at full speed.
- **Enhancements menu (ESC)**: integer scaling, nearest / bilinear filtering,
  CRT presets (Off / Lite / Soft / Strong / Custom), an *approximate* GBA-like
  colour profile, EMU / PRESENT FPS readouts, borderless fullscreen.
- **Alt+Enter** toggles windowed / borderless fullscreen.
- Controller navigation of the Enhancements menu.
- **Native** or **Monitor Refresh** presentation. The game itself always runs at
  its original ~59.73 Hz. Monitor Refresh shows the current frame again on a
  high-refresh display; it does **not** produce extra unique gameplay frames
  and there is no interpolation. `EMU 59.73 / PRESENT 144` means the game runs
  at its own rate while the window was presented at the monitor's rate.

## Original Metroid (NES)

The NES game is unlocked **through the original game's progression**
(finishing *Zero Mission*). There is no unlock button in the launcher and no
completed save is included. If you already have your own compatible save file
that has it unlocked, you may place it beside your ROM (see *Your data*).
NES testing is optional for this beta.

## Your data: where things are stored

The locations below were checked on Linux by running the packaged build with a clean home
folder. The Windows column follows the program's code (it redirects its settings to the
user's AppData folders) and is **not yet verified on a real Windows machine**.

| What | Windows (expected) | Linux (verified) |
|---|---|---|
| Launcher settings: ROM/BIOS *paths* (`launcher.ini`) | `%APPDATA%\MZMRecompiled\` | `$XDG_CONFIG_HOME/MZMRecompiled/` (default `~/.config/MZMRecompiled/`) |
| In-game settings and bindings (`config.ini`, `keybinds.ini`), last-used ROM/BIOS path cache (`rom.cfg`, `bios.cfg`) | `%APPDATA%\MZMRecompiled\` | **in the program folder, next to `MZMRecomp`** (so keep it somewhere writable) |
| **Log of the latest run** (`latest.log`, overwritten every run) | `%LOCALAPPDATA%\MZMRecompiled\logs\latest.log` | `$XDG_STATE_HOME/MZMRecompiled/logs/latest.log` (default `~/.local/state/MZMRecompiled/logs/`) |
| Launcher event history (`mzm-recompiled.log`, appended) | same folder as `latest.log` | same folder as `latest.log` |
| **Save file** (`<ROM name>.sav`) | **beside your ROM file** | **beside your ROM file** |

Your ROM and BIOS are never copied: only their paths are remembered. The launcher's
**About → Open Logs Folder** button opens the logs folder. Back up your `.sav` before testing.
Deleting `launcher.ini` (and `rom.cfg`/`bios.cfg`) makes the launcher ask for the files again.

## Logs and privacy

- `latest.log` records: release, MZM and GBARecomp commit, OS, startup, ROM and
  BIOS validation *results*, strict-static state, and the runtime's own output
  (renderer, display mode / refresh, errors). It does not contain ROM or BIOS
  contents. The runtime may print file paths of your ROM/BIOS; look at the
  file and remove anything you do not want to share before attaching it.
- There is **no analytics, no telemetry, no automatic crash upload, and no
  network access needed to play.** Reports are sent only if you send them.
- If the program crashes, a marker line is added to `latest.log`.

## Antivirus / SmartScreen note (Windows)

- `MZMRecomp.exe` is **not code-signed**. Windows may show *"Windows protected
  your PC"* / *unknown publisher*, and Beta 3 has no download reputation with
  SmartScreen yet. That is expected for a new unsigned build; it is **not**
  automatically a false positive, and we are not claiming it is one.
- **Do not disable your antivirus and do not add global exclusions** on our
  account. If you are not comfortable running an unsigned test build, please
  don't — that is a perfectly good decision.
- If something blocks or flags the program, tell us (see `FEEDBACK.md`):
  antivirus product and version, the **exact detection name/message**, a
  screenshot, the **SHA-256 of the ZIP and of `MZMRecomp.exe`** (compare with
  `SHA256SUMS.txt`; `Get-FileHash` in PowerShell, or `sha256sum` on Linux), and
  your Windows version.
- On a computer managed by an employer, school or institute, use an
  authorised personal PC instead of working around its security policy.

## Reporting a bug

Fill in `FEEDBACK.md` and attach `latest.log` (see above). Include the
*Release/MZM build* line shown in **About** or at the top of `latest.log`.

## Licence and credits

Noncommercial use. Third-party licences are in `THIRD-PARTY-LICENSES/`.
See `KNOWN-ISSUES.md` for current limitations.
