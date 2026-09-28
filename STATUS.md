# MZM-Recomp — Status

**Last updated:** 2026-09-28  
**Current phase:** M0 — Feasibility  
**Current verdict:** **GO WITH CONDITIONS**

This file is intentionally conservative. A capability is not marked complete because it "should work"; it is marked complete only when reproducible evidence exists.

## Status vocabulary

| State | Meaning |
|---|---|
| **CONFIRMED** | Reproduced or directly verified with evidence |
| **EXPERIMENTAL** | Implemented or observed, but not yet qualified enough to rely on |
| **PENDING** | Planned or currently being investigated |
| **BLOCKED** | Cannot proceed until a specific dependency or defect is resolved |

## M0 scoreboard

| Item | Status | Evidence / note |
|---|---|---|
| USA ROM identity | **CONFIRMED** | 8 MiB, game code `BMXE`, revision 0, expected SHA-1 and SHA-256 match |
| Europe ROM identity | **CONFIRMED** | 8 MiB, expected EU SHA-1 and SHA-256 match |
| Primary target selected | **CONFIRMED** | USA first; Europe retained as secondary multiregion target |
| Public duplicate audit | **CONFIRMED** | No public GBARecomp-based MZM project found in the 2026-09-28 audit |
| Existing native-port work identified | **CONFIRMED** | `Nanikoss/MZM-Reprimed` exists and is documented; MZM-Recomp does not claim to be the first native MZM project |
| `metroidret/mzm` baseline identified | **CONFIRMED** | Pin: `43b7fd52f552e4d38c1521ff9d4df5ee57e61493` |
| `mstan/gbarecomp` baseline identified | **CONFIRMED** | Pin: `e7728148c6829ba526f682876430a0c9022dc6c0` |
| `agbcc` baseline identified | **CONFIRMED** | Pin: `59b966ed1b8f371856dcf99f1546c2fe89c678ca` |
| Rebuild MZM USA from decomp | **BLOCKED** | Extraction completed, but `agbcc` and the MZM build stopped because `arm-none-eabi-as` / `arm-none-eabi-ar` are missing from the Linux toolchain |
| Export semantic symbol/address map | **PENDING** | M0.3 |
| Build/qualify GBARecomp on Linux | **PENDING** | M0.4 |
| MZM cartridge static-analysis scan | **PENDING** | M0.5 |
| Hardware support matrix qualification | **PENDING** | M0.6 |
| First generated native C++ | **PENDING** | M1 |
| First native MZM instruction executed | **PENDING** | M1 |
| Boot/intro/title | **PENDING** | M2 |
| Controllable Samus | **PENDING** | M3 |
| Strict-static gameplay route | **PENDING** | M3 |
| Full-game compatibility | **PENDING** | M4 |
| NES Metroid compatibility | **PENDING** | Dedicated M4 workstream |
| Europe runtime support | **PENDING** | After USA bring-up proves architecture |
| Enhancements | **PENDING** | M5 only after compatibility baseline |

## Current blocker — M0.2

The pinned upstreams were cloned correctly and the MZM extractor completed. The current Linux host is missing ARM binutils commands required by both `agbcc` and the decomp build:

```text
arm-none-eabi-as
arm-none-eabi-ar
```

Observed consequences:

- `agbcc/libgcc` could not produce `libgcc1.a`;
- the MZM build later failed while assembling `asm/audio_internal.o`;
- `mzm_us.gba` was therefore not produced;
- the byte-identical M0.2 gate remains open.

This is a **host dependency blocker**, not evidence of an MZM or GBARecomp incompatibility.

## Confirmed cartridge identities

### USA — primary

```text
Size:    8388608 bytes
Game ID: BMXE
Version: 0
SHA-1:   5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8
SHA-256: fc94f65380b65b870a30b9b04b39cca1dc63d6e46a4a373d3904adc0912ebc37
```

### Europe — secondary

```text
Size:    8388608 bytes
SHA-1:   0fd107445a42e6f3a3e5ce8c865f412583179903
SHA-256: 0d061ff36c62ebbf2220e106ea34abdb8d813943515b35007fd8fdba3ed019b0
```

## Known MZM-specific technical findings

These are source-audit findings, not yet runtime qualification claims:

- `InitializeGame()` clears GBA memory regions, installs interrupt code, initializes SRAM and audio, configures interrupts, and enables IME.
- `LoadInterruptCode()` copies `IntrMain` from ROM to IWRAM and executes it from RAM.
- The main loop uses GBA HALT and waits for VBlank/IRQ activity.
- MZM uses function pointers/callback tables in many gameplay systems.
- HBlank DMA is used by effects and must be validated for visual timing.
- SRAM behavior is relevant from boot.
- Fusion Gallery communication uses serial/link facilities and Timer 3.
- The unlockable NES Metroid loads and executes code in multiple RAM/VRAM regions, making it a distinct high-risk compatibility target.
- The European release contains meaningful code/data/layout differences and cannot be modeled as only translated text.

## What we do not claim yet

At this stage MZM-Recomp does **not** claim:

- a playable native build;
- successful MZM generation through GBARecomp;
- strict-static closure;
- intro/title rendering;
- audio correctness;
- save compatibility;
- full Linux packaging;
- Europe compatibility;
- NES Metroid compatibility;
- performance or feature parity.

Any future README claim must be backed by a reproducible milestone gate.
