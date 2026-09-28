# MZM Recompiled Private Beta 0.1.0

This independent beta contains no game file, GBA BIOS, or extracted game art.
Supply your own compatible USA revision 0 game file and GBA BIOS at first
start. The launcher stores only their paths in your user configuration.

## Run a packaged build

### Windows

Extract `MZMRecompiled-Beta-Windows.zip` to a folder you can write to. Open
`MZMRecomp.exe`; the Helm Core launcher will ask you to select your own USA
revision 0 game file and GBA BIOS. When both show `Valid`, press `CONTINUE`
and then `PLAY`. On later runs, use `Game Data` to change either file.
`Enhancements`, `Settings`, and `About` are available in the same launcher.
No compiler, Python, Git, global SDL2 install, ROM, or BIOS comes with the ZIP.
Your save file stays beside your selected game file. Launcher settings are in
`%APPDATA%\MZMRecompiled\`; logs are in
`%LOCALAPPDATA%\MZMRecompiled\logs\`.

### Linux

From the package directory, run `./MZMRecomp`. Select the game file and BIOS,
verify that both show `Valid`, press `CONTINUE`, then `PLAY`. On later runs,
the launcher opens Home if both files still validate. `Game Data` can change
either file. A save file is written beside the selected game file by default.

On Linux, SDL2, OpenGL, and compatible system libraries must be installed. A
desktop display is required. If no native file picker is available, paste an
absolute path into the corresponding field.

The Linux package includes the original MZM icon in SVG, PNG sizes and SDL2 BMP,
plus `MZMRecompiled.desktop`. The desktop file is an installation template:
`Exec=MZMRecomp` requires the binary on `PATH`, and `Icon=mzm-recompiled`
requires an installed icon theme entry. It is not registered automatically.
For a portable directory, run `./MZMRecomp` as above.

## Reproduce the package

Prerequisites: initialize `recomp-ui`; generate the private ROM-derived source
locally; build an x86_64 Linux executable with `MZM_RECOMP_UI=ON` using
`scripts/build-m1.sh`. Then run:

```bash
bash scripts/package-beta-linux.sh
```

The result is `dist/MZMRecompiled-Beta/`. Set `MZM_BUILD_DIR` and
`MZM_PACKAGE_DIR` to change input/output. The script copies an explicit
allowlist and rejects game/BIOS/save file extensions. It does not build from
scratch; it requires the completed local build.

## Reports

Include the version, Linux distribution, GPU, reproduction steps, and the
sanitized launcher log from
`~/.local/state/MZMRecompiled/logs/mzm-recompiled.log` (or the matching
`XDG_STATE_HOME` path). `About` can open the logs folder. Runtime diagnostics
can be captured separately from a terminal; inspect them before sharing,
because the runtime may print absolute local paths.

Known limits: only USA revision 0 is accepted; an external canonical GBA BIOS
is still required; UI settings outside Game Data are informational; runtime
fatal output is not yet copied into the launcher log; Linux packages rely
on system SDL2/OpenGL. A BIOS-independent runtime is future work and needs its
own compatibility and legal review.
