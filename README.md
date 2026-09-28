# MZM-Recomp

**MZM-Recomp** is an experimental, Linux-first static recompilation project for **Metroid: Zero Mission (Game Boy Advance)**.

The project aims to run the original game logic as native host code using [GBARecomp](https://github.com/mstan/gbarecomp), while using [metroidret/mzm](https://github.com/metroidret/mzm) as the primary semantic reference for understanding functions, symbols, data structures, callbacks, and region differences.

> [!IMPORTANT]
> This repository does **not** contain the game ROM, Nintendo GBA BIOS, extracted copyrighted game assets, or distributable generated ROM-derived C/C++.
> A legally obtained copy of the game is required.

## Project status

**Current phase:** M4 — Compatibility

**M0 verdict:** **PASSED / GO**

M0 feasibility, M1 static bootstrap, M2 boot/title, and M3 gameplay proof are complete for the verified USA target.

A strict-static Linux session has now reached Intro, Title, New Game, controllable Samus, multiple early rooms, and a Save Room with:

```text
dispatch_misses=0
interpreted_insns=0
unmapped=0
io_unhandled=0
```

The same session persisted a 32 KiB SRAM save, and a later strict-static process successfully loaded that save back into gameplay with zero dispatch misses or interpreter instructions. This remains a **strict-static gameplay proof plus basic save round-trip**, not a full-game compatibility claim. M4 now expands validation across the rest of the game, visual/audio accuracy, later dynamic-code paths, Europe, Fusion-link functionality, and NES Metroid.

See [STATUS.md](STATUS.md) for the exact evidence-backed status.

## Native launcher

MZM-Recomp also has an optional `recomp-ui` product shell under active
development. It provides a graphical ROM/BIOS setup flow and shared GBARecomp
display, audio, input, save-state, rewind and fast-forward controls without
changing the guest game logic.

The launcher is intentionally separate from M4 compatibility qualification:
strict-static CLI runs remain available and authoritative for evidence.

Initialize the pinned UI dependency with:

```bash
git submodule update --init --recursive recomp-ui
```

See [docs/LAUNCHER.md](docs/LAUNCHER.md) for the current MZM-specific launcher
and [docs/BETA-TESTING.md](docs/BETA-TESTING.md) for beta packaging and testing.

## Verified cartridge targets

### Primary target — USA

| Property | Value |
|---|---|
| Title | Metroid: Zero Mission |
| Region | USA |
| GBA game code | `BMXE` |
| Revision | 0 |
| Size | 8,388,608 bytes |
| SHA-1 | `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8` |
| SHA-256 | `fc94f65380b65b870a30b9b04b39cca1dc63d6e46a4a373d3904adc0912ebc37` |
| M0 identity status | **Confirmed** |

### Secondary target — Europe

| Property | Value |
|---|---|
| Region | Europe (En, Fr, De, Es, It) |
| Size | 8,388,608 bytes |
| SHA-1 | `0fd107445a42e6f3a3e5ce8c865f412583179903` |
| SHA-256 | `0d061ff36c62ebbf2220e106ea34abdb8d813943515b35007fd8fdba3ed019b0` |
| M0 identity status | **Confirmed** |

USA is the first bring-up target to minimize region-specific variables. Europe remains a first-class planned target and will later be used as a portability test against USA-specific hard-coding.

## Technical direction

The intended pipeline is:

```text
Verified user-supplied MZM ROM
        │
        ├── metroidret/mzm
        │     semantic names / symbols / source-level understanding
        │
        └── GBARecomp
              ARM7TDMI ARM + Thumb analysis
                        │
                        ▼
              generated native C++
                        │
                        ▼
              shared GBA hardware runtime
                        │
                        ▼
                 native host build
                    Linux first
```

The ROM remains the behavioral authority. The decompilation is the semantic layer used to avoid unnecessary blind reverse engineering.

## Engineering priorities

In order:

1. Correctness
2. Reproducibility
3. Traceability
4. Compatibility
5. Documentation
6. Performance
7. Enhancements

Generated code must never be hand-edited as the primary fix. Problems should be fixed in the analyzer, configuration, runtime, integration, or upstream project as appropriate, then regenerated.

## Milestones

| Milestone | Goal |
|---|---|
| **M0 — Feasibility** | Verify ROM identity, reproduce upstream decomp, qualify GBARecomp, map hardware/code-copy/indirect-control-flow requirements, confirm project differentiation |
| **M1 — Static bootstrap** | ROM → analyzer → generated C++ → native Linux binary → execute recompiled game code |
| **M2 — Boot** | Initialization, interrupts, DMA, timers, PPU, input, audio baseline, intro/title |
| **M3 — Gameplay proof** | Title → New Game → intro → controllable Samus → first playable room |
| **M4 — Compatibility** | Areas, bosses, saves, audio, endings/postgame, region support, Fusion-link work, NES Metroid |
| **M5 — Enhancements** | Fullscreen/upscaling, remapping, rumble, save states, fast-forward, widescreen research, high-refresh, achievements, Steam Deck |

Strict-static validation gates will be used so that "native" and "statically resolved" are not treated as synonyms.

See [ROADMAP.md](ROADMAP.md).

## Important MZM-specific technical risks

Current source-level audit has identified several areas that must be validated rather than assumed:

- The IRQ handler is copied from ROM into IWRAM and executed from RAM.
- The main loop relies on HALT plus VBlank/IRQ wake-up behavior.
- MZM uses many callback/function-pointer tables and indirect dispatch paths.
- HBlank DMA is used for visual effects.
- SRAM saves must remain compatible.
- Fusion Gallery functionality uses serial/link hardware and Timer 3.
- The unlockable NES Metroid contains substantial code loaded into IWRAM, EWRAM, and VRAM and is expected to be one of the hardest full-compatibility targets.
- The European build has real code/data/layout differences and must not be treated as a simple language patch.

M0 also identified bounded follow-up work: PPU mosaic is not yet rendered by the pinned runtime; MZM reuses one RAM address for several haze-code variants; WAITCNT/prefetch accuracy needs runtime validation; SRAM uses stack-local copied helpers; and Chozodia/NES paths contain later executable-RAM cases.

These no longer block the proven early-game strict-static route, but they remain relevant to full M4 compatibility and prevent a full-game compatibility claim.

## Existing related work

This project does not claim to be the first native Zero Mission effort.

Notable related projects include:

- [metroidret/mzm](https://github.com/metroidret/mzm) — near-complete source decompilation and the principal semantic upstream.
- [mstan/gbarecomp](https://github.com/mstan/gbarecomp) — general-purpose GBA static recompilation framework and runtime.
- [Nanikoss/MZM-Reprimed](https://github.com/Nanikoss/MZM-Reprimed) — an early Windows-native Zero Mission proof of concept supporting the same USA cartridge revision.
- Other emulator-packaged or decomp forks are tracked in [docs/UPSTREAMS.md](docs/UPSTREAMS.md).

MZM-Recomp differentiates itself by targeting a **reproducible GBARecomp-based static recompilation**, **Linux-first development**, strict-static closure, multiregion architecture, and evidence-driven validation.

## Repository policy

The intended repository contains only material that can be distributed appropriately:

```text
configs/       MZM-specific recompilation configuration
runtime/       narrowly scoped MZM integration/runtime glue
scripts/       reproducible tooling
tests/         validation and differential tests
docs/          architecture, research, evidence, build documentation
```

Local-only/generated material must remain untracked:

```text
ROMs
BIOS images
save files
generated ROM-derived C/C++
extracted original game assets
temporary M0 worktrees/upstreams/evidence containing local paths or copyrighted outputs
build products
```

See [docs/LEGAL.md](docs/LEGAL.md).

## Documentation

- [STATUS.md](STATUS.md) — evidence-backed current status
- [ROADMAP.md](ROADMAP.md) — milestone gates and completion criteria
- [docs/M0-FEASIBILITY.md](docs/M0-FEASIBILITY.md) — feasibility audit and GO conditions
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — target architecture and responsibility boundaries
- [docs/UPSTREAMS.md](docs/UPSTREAMS.md) — upstream pins, related projects, contribution strategy
- [docs/LEGAL.md](docs/LEGAL.md) — distribution and licensing boundaries
- [docs/BUILD-LINUX.md](docs/BUILD-LINUX.md) — reproducible Linux development workflow

## Non-affiliation

This is an independent fan/research project. It is not affiliated with or endorsed by Nintendo, Nintendo R&D1, or the owners of Metroid.

**Metroid**, **Metroid: Zero Mission**, Nintendo, Game Boy Advance, and related names and assets are trademarks/copyrights of their respective owners.
