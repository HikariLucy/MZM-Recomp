# MZM-Recomp Product Shell

**State:** **ACTIVE / EXPERIMENTAL**

This workstream improves the player-facing native application without changing
the guest game's logic or weakening the strict-static compatibility path.

It runs in parallel with M4 compatibility.

## Principles

1. The faithful 240×160 game path remains the validation baseline.
2. ROM and BIOS are always user-supplied.
3. The launcher may remember paths, but never copies those inputs into Git.
4. Strict-static/CLI workflows remain available for engineering validation.
5. Host-side quality-of-life features must remain separable from guest behavior.
6. Game-specific rendering enhancements such as adaptive widescreen require
   their own later compatibility work; they are not part of the initial shell.

## P1 — Graphical launcher

The project uses GBARecomp's shared `recomp-ui` pre-boot launcher rather than
maintaining a bespoke frontend.

Current integration:

- pinned `recomp-ui` git submodule;
- isolated launcher translation unit;
- launcher-enabled CMake option;
- verified MZM USA ROM SHA-1 baked into the runner;
- persistent player-owned ROM/BIOS path caches;
- freely resizable host window;
- shared Assist Tools enabled;
- 9 save-state slots;
- 4× default fast-forward;
- 15-second rewind history;
- ordinary CLI path preserved.

Player-owned launcher state:

```text
mzm-config.ini
mzm-keybinds.ini
mzm-rom.cfg
mzm-bios.cfg
```

These are runtime files, not repository content.

## Build

Initialize the pinned UI dependency once:

```bash
git submodule update --init --recursive recomp-ui
```

Then:

```bash
./scripts/build-launcher.sh
```

The launcher build is emitted separately from the milestone development build:

```text
build-launcher/MZMRecomp
```

Run:

```bash
./build-launcher/MZMRecomp
```

or force setup:

```bash
./build-launcher/MZMRecomp --launcher
```

## P2 — Identity and Linux packaging

Next product-shell items:

- original MZM-Recomp application icon;
- launcher box-art / identity treatment using distributable original artwork;
- Linux `.desktop` entry;
- AppDir/release staging;
- release ZIP and/or AppImage research;
- first-run documentation;
- release audit that rejects ROM, BIOS, saves, generated ROM-derived C++ and
  local path leakage.

## Later enhancements

Framework capabilities we can expose after the shell is stable:

- GBA display color profiles;
- controller remapping;
- save states;
- rewind;
- fast-forward;
- fullscreen/resizable-window preferences.

Game-specific enhancements are separate:

- adaptive widescreen;
- high-refresh/60 FPS experiments;
- game-owned presentation extensions.

Those require dedicated MZM validation rather than merely turning on a generic
runtime option.
