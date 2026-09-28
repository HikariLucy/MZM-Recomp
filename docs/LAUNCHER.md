# MZM Recompiled launcher

## Architecture

`src/main.cpp` calls the optional MZM launcher before `gbarecomp::run_game()`.
The launcher lives in `src/game_launcher_boot.cpp` and uses the pinned
`recomp-ui` dependency for Dear ImGui, SDL2/OpenGL, and the native file picker.
It does not modify the recompiled guest code or the GBARecomp CPU backend.
`src/launcher_state.cpp` owns path persistence and file identity checks.

The CMake project version is the single version source. The launcher and log
read `MZM_VERSION` from CMake. `src/mzm_theme.h` centralizes the MZM palette,
spacing and controls. Home loads the approved Helm Core helmet and orbit image
from `assets/icons/mzm-brand-helm-core.png`. The compact concept-board crop in
`assets/icons/mzm-recompiled-source.png` generates the native icon sizes, BMP,
and SVG wrapper through `scripts/render-icon.py`.

## First start and Play

Start `MZMRecomp` without arguments. Setup asks for the player's USA revision 0
game file and a canonical 16 KiB GBA BIOS. Both are checked by SHA-1. Native
file pickers are used when available; the path fields accept manual paste as a
fallback. `Continue` saves paths and opens Home. Later runs open Home directly
while both files still validate. `Game Data` changes either path and
`Revalidate` checks files again.

Home links to Enhancements, which summarizes verified host features: nine save
state slots, about 15 seconds of rewind history, 4x default fast-forward,
resizable/fullscreen display, input bindings, and launch-time color models.
The in-game menu manages save states, rewind, fast-forward, and display options.
Input bindings load from host configuration files; color models are selected
at launch. The showcase has no inactive controls.

`PLAY` sets `GBARECOMP_STRICT_STATIC=1` and calls the existing runtime in the
same process with `--bios`, `--rom`, and `--config configs/mzm-us.toml`. The
config is resolved beside the executable in a beta package, or one directory
above a development build. Explicit `--rom` and headless runtime options keep
the CLI path. `--no-launcher` skips the UI; `--launcher` forces it.

The current BIOS source is an external file. The path and identity check are
in the launcher data layer, so a future compatible built-in implementation can
replace that source without changing the main navigation. No replacement BIOS
is included.

## User files

On Linux, path references are stored at
`$XDG_CONFIG_HOME/MZMRecompiled/launcher.ini`, or
`~/.config/MZMRecompiled/launcher.ini`. Logs append to
`$XDG_STATE_HOME/MZMRecompiled/logs/mzm-recompiled.log`, or
`~/.local/state/MZMRecompiled/logs/mzm-recompiled.log`. On Windows the future
port uses `%APPDATA%/MZMRecompiled` for both. Logs include version, start/end,
strict-static result, and game-data validation results; they omit local file
paths. A successful strict-static run is recorded as `static-recompiled`;
runtime stdout/stderr is not captured in this beta.

GBARecomp saves to `<game-file>.sav` unless `[save].path` in a runtime config
overrides it. The visual Home keeps save details off the main screen. Keep the
game file somewhere writable if saving is needed.

## Build and limitations

Run `scripts/build-m1.sh` after generating the local ROM-derived source as
described in `docs/BUILD-LINUX.md`. The optional UI requires the initialized
`recomp-ui` submodule, SDL2, OpenGL, and a desktop display. CMake can build the
CLI host with `-DMZM_RECOMP_UI=OFF`; it will still accept explicit ROM/BIOS
arguments. The launcher currently leaves Video, Audio, and Controls editing to
the runtime UI; Advanced has no active settings. Full-screen and controller
navigation polish and Windows packaging remain future work. The launcher uses
`SDL_SetWindowIcon`; a short-lived SDL event watch applies the same icon when
GBARecomp creates the game window. Desktop association uses the included
`.desktop` template. The bundled binary depends on compatible system SDL2/OpenGL
libraries.

For a local visual capture, set `MZM_LAUNCHER_CAPTURE=/tmp/mzm-home.bmp` and,
optionally, `MZM_LAUNCHER_PREVIEW_PAGE=home|enhancements|data|settings|about` and
`MZM_LAUNCHER_WINDOW_SIZE=720x480` before running
`MZMRecomp --launcher`. Capture exits without saving configuration or launching
the game. Preview pages do not change validation or enable PLAY.
