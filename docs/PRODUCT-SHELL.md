# MZM-Recomp Product Shell

**State:** launcher foundation implemented; local validation pending.

This workstream runs in parallel with M4 compatibility. It improves the native
PC experience without changing the guest cartridge logic or relaxing the
strict-static validation path.

## P1 — Launcher foundation

The launcher uses GBARecomp's shared `recomp-ui` seam rather than a custom
frontend.

Initial surface:

- verified MZM USA ROM picker;
- verified retail GBA BIOS picker;
- existing SRAM save detection;
- window scale/fullscreen;
- GBA screen color profiles;
- audio volume;
- keyboard/controller remapping;
- save-state slots;
- rewind/fast-forward assist tools;
- resizable native presentation.

The launcher persists only player-owned configuration beside the executable:

```text
mzm-config.ini
mzm-keybinds.ini
mzm-rom.cfg
mzm-bios.cfg
```

No ROM, BIOS, save, or generated ROM-derived source is committed.

## Dependency pin

`recomp-ui` is a Git submodule. The repository pins a concrete revision
instead of following the upstream default branch.

Initialize it with:

```bash
git submodule update --init --recursive recomp-ui
```

If the submodule is absent, CMake warns and builds the existing CLI-only host
instead. This makes launcher work non-blocking for compatibility development.

## Fidelity boundary

Launcher/settings features are host presentation and workflow capabilities.
The faithful 240x160 strict-static route remains available.

Not included in P1:

- adaptive widescreen;
- gameplay modifications;
- 60 FPS logic patches;
- ROM patches/mod packages;
- game-specific rendering extensions.

Those require their own evidence and do not ride on the launcher milestone.

## Next product steps

P2:

- original MZM-Recomp icon/brand mark;
- launcher box art / project art that does not redistribute extracted game
  assets;
- Linux `.desktop` integration;
- release directory layout;
- AppImage/portable archive research;
- first-run documentation and release audit.
