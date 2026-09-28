# MZM-Recomp Product Shell

**State:** MZM-specific private beta launcher and visual identity implemented;
validation details and current limitations are tracked in [LAUNCHER.md](LAUNCHER.md).
The palette, mark, and packaging rules are in [VISUAL-IDENTITY.md](VISUAL-IDENTITY.md).

This workstream runs in parallel with M4 compatibility. It improves the native
PC experience without changing the guest cartridge logic or relaxing the
strict-static validation path.

## P1 — Launcher foundation

The launcher uses `recomp-ui`'s Dear ImGui, SDL2/OpenGL, and native file picker
in an MZM-specific frontend. GBARecomp's runtime remains the launch target.

Current launcher surface:

- verified MZM USA ROM picker;
- verified retail GBA BIOS picker;
- Home, Game Data, Settings, and About;
- PLAY into the existing strict-static runtime.

Runtime menu capabilities (not editable on the launcher Settings page):

- window scale/fullscreen;
- GBA screen color profiles;
- audio volume;
- keyboard/controller remapping;
- save-state slots;
- rewind/fast-forward assist tools;
- resizable native presentation.

The older generic launcher persisted player-owned configuration beside the
executable:

```text
mzm-config.ini
mzm-keybinds.ini
mzm-rom.cfg
mzm-bios.cfg
```

No ROM, BIOS, save, or generated ROM-derived source is committed.
The MZM-specific beta instead stores path references under the user config
directory and logs under the user state directory. See [LAUNCHER.md](LAUNCHER.md).

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

P2 now includes the original MZM mark, themed launcher, Linux desktop template,
and release directory assets. AppImage research remains separate.
