# M0 Feasibility Report

**Project:** MZM-Recomp  
**Date opened:** 2026-09-28  
**Decision:** **GO WITH CONDITIONS**

## 1. Question

Can **Metroid: Zero Mission (GBA)** be developed as a reproducible, Linux-first native project using static recompilation, without duplicating an equivalent existing public project and without depending on hand-maintained generated code?

Current answer: **probably yes**, subject to the M0 exit conditions below.

## 2. Cartridge baseline

### Primary — USA

```text
Size:    8388608
Game ID: BMXE
Version: 0
SHA-1:   5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8
SHA-256: fc94f65380b65b870a30b9b04b39cca1dc63d6e46a4a373d3904adc0912ebc37
```

Status: **CONFIRMED**

### Secondary — Europe

```text
Size:    8388608
SHA-1:   0fd107445a42e6f3a3e5ce8c865f412583179903
SHA-256: 0d061ff36c62ebbf2220e106ea34abdb8d813943515b35007fd8fdba3ed019b0
```

Status: **CONFIRMED**

USA is selected for initial bring-up because it is the default/widely referenced target in the decomp ecosystem and avoids introducing EU-specific code/data differences during early debugging.

## 3. Decompilation readiness

The audited `metroidret/mzm` upstream reports:

- 2718 / 2721 functions decompiled;
- 99.89% function completion;
- 100% of tracked data outside blobs;
- USA, Europe, Japan and beta-region build support, with China not yet supported.

The three remaining non-matching functions are documented upstream as matching/code-generation issues rather than large unknown subsystems. For MZM-Recomp, this is not inherently a static-recompilation blocker because the machine code in the verified ROM remains the execution source of truth.

### Baseline pin

```text
metroidret/mzm
43b7fd52f552e4d38c1521ff9d4df5ee57e61493
```

## 4. GBARecomp readiness

GBARecomp is a general-purpose ARM7TDMI static-recompilation framework with a shared GBA hardware runtime. It already covers the broad categories MZM needs: ARM/Thumb execution, memory/bus behavior, PPU, DMA, timers, interrupts, save hardware, input and audio.

Its design also supports a discovery workflow in which unresolved code can temporarily use an interpreter/self-healing tier, followed by strict-static validation once control-flow coverage is known.

### Baseline pin

```text
mstan/gbarecomp
e7728148c6829ba526f682876430a0c9022dc6c0
```

Reference integration work, especially **Mega Man Zero**, is highly relevant because it exercises fast 2D action, ARM/Thumb interworking, IRQ/DMA/audio, SRAM, indirect control flow and copied executable code.

## 5. Public duplicate audit

The 2026-09-28 audit did **not** identify an existing public project that matches all of these properties:

- Metroid: Zero Mission;
- GBARecomp-based;
- static-recompilation focused;
- Linux-first;
- open/reproducible integration;
- strict-static closure as an explicit quality gate.

However, related work exists and must be credited.

### MZM: Reprimed

`Nanikoss/MZM-Reprimed` is an early native Windows x64 proof of concept. Its public README reports:

- native executable;
- original game logic initialization;
- keyboard input;
- GBA memory/register compatibility;
- DMA and BIOS decompression replacements;
- user-supplied USA ROM loading;
- opening intro execution;
- native software rendering for intro sprites.

It is **not** described as a complete playable port, and its public repository currently does not present the same GBARecomp/Linux/reproducibility architecture.

Therefore MZM-Recomp must **not** claim "first native PC port." Its differentiation is the engineering model, not precedence.

Other emulator-wrapped "PC edition" work and decomp forks are not equivalent to the intended static-recompilation project.

## 6. MZM execution characteristics discovered in source audit

### 6.1 Startup

`InitializeGame()` performs low-level platform initialization including memory clearing, interrupt setup, SRAM access and audio initialization.

### 6.2 Copied interrupt code

`LoadInterruptCode()` copies `IntrMain` into IWRAM and installs a pointer to the copied handler.

Implication: MZM requires explicit treatment of executable code copied from ROM to RAM. This is a known category in GBARecomp integrations and should be representable through code-copy declarations and resume/entry metadata.

### 6.3 HALT + VBlank

The main loop uses GBA HALT and waits for the VBlank request/interrupt path.

Implication: scheduler, HALT wake-up and IRQ timing correctness are part of the boot gate, not optional polish.

### 6.4 Indirect control flow

The decomp exposes many callbacks/function-pointer tables, including gameplay, effects, cutscenes and interrupt callbacks.

Implication: static closure will require bounded target sets, jump/callback-table declarations, observed runtime targets, or framework improvements where appropriate.

### 6.5 HBlank DMA

Visual effects use HBlank-triggered DMA with repeat/reload behavior.

Implication: title may not exercise the full risk; gameplay visual validation must include scenes that use these effects.

### 6.6 Save hardware

MZM uses SRAM.

Implication: save compatibility can be tested early and should not be deferred to the end.

### 6.7 Fusion Gallery / serial

The Fusion-related feature path uses serial communication and Timer 3.

Implication: this is not required for M2/M3 and may be deferred to M4.

### 6.8 NES Metroid

The unlockable NES Metroid feature is unusually complex. Upstream documentation shows emulator code being loaded into IWRAM, EWRAM and VRAM at runtime, with multiple payload segments.

Implication: this is a dedicated high-risk M4 target. Normal Zero Mission gameplay can proceed without making "full compatibility" claims.

## 7. Initial risk matrix

| Subsystem | Initial risk | M0 interpretation |
|---|---:|---|
| ARM execution | Low | Framework capability exists |
| Thumb execution | Low | Framework capability exists |
| ARM↔Thumb interworking | Low | Must still qualify MZM routes |
| ROM/EWRAM/IWRAM | Low | Standard GBA memory model |
| IRQ | Low/Medium | Copied handler and resume targets require validation |
| VBlank | Low | Central boot path |
| HALT | Low/Medium | Timing/wake semantics must be correct |
| General DMA | Low | Core framework functionality |
| HBlank DMA | Medium | Visual/timing validation required |
| Timers | Low/Medium | Audio/link behavior uses them |
| Input | Low | Standard |
| SRAM | Low | Standard, verify compatibility |
| Audio | Medium | Must qualify timing/FIFO behavior |
| Function pointers/callbacks | Medium | Static-closure work expected |
| EU region support | Low/Medium | Real code/layout differences |
| Fusion link/SIO | Medium/High | Defer to M4 |
| NES Metroid | High | Dedicated M4 subproject |
| Linux host | Low/Medium | Supported architecture, project-specific qualification still required |

## 8. GO conditions

The project remains **GO WITH CONDITIONS** until all conditions are met:

1. Reproduce the USA ROM byte-for-byte from the pinned decomp on Linux.
2. Build and qualify the pinned GBARecomp baseline on Linux without MZM-specific patches.
3. Complete an MZM ROM scan for indirect control flow, code copies and unsupported constructs.
4. Confirm no fundamental execution model blocks the normal boot/title/gameplay path.
5. Maintain a strict repository boundary around ROMs, BIOS, extracted assets and generated ROM-derived C++.
6. Maintain transparent differentiation from MZM: Reprimed and other related projects.

If these conditions pass, the project moves from feasibility into M1 implementation.

## 9. M0 work packages

| ID | Work package | State |
|---|---|---|
| M0.1 | ROM identity | **CONFIRMED** |
| M0.2 | Decomp reproducibility | **PENDING final evidence** |
| M0.3 | Semantic address/symbol map | PENDING |
| M0.4 | GBARecomp Linux qualification | PENDING |
| M0.5 | MZM static-analysis scan | PENDING |
| M0.6 | Hardware support matrix | PENDING |
| M0.7 | Duplicate/upstream positioning | **Initial audit confirmed** |

## 10. Final M0 decision format

When M0 closes, this document will end with one of:

- **GO** — M1 may proceed.
- **GO WITH CONDITIONS** — specific residual risks remain bounded and documented.
- **NO-GO** — a fundamental technical or distribution constraint makes the architecture unsuitable.

No percentage-based completion claim replaces these gates.
