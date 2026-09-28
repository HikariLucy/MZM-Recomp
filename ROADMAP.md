# MZM-Recomp — Roadmap

The roadmap is organized around **evidence gates**, not percentages. A milestone closes only when its acceptance criteria are reproduced and recorded.

## M0 — Feasibility

**Goal:** establish that MZM is a technically and legally sensible GBARecomp target before substantial integration code is written.

### M0.1 — Cartridge identity

- [x] Verify USA ROM size and hashes.
- [x] Verify USA header identity (`BMXE`, revision 0).
- [x] Verify Europe ROM size and hashes.
- [x] Select USA as primary bring-up target.
- [x] Preserve Europe as secondary architecture-validation target.

### M0.2 — Decomp reproducibility

- [x] Pin `metroidret/mzm`.
- [x] Pin `agbcc`.
- [x] Build required tooling on Linux.
- [x] Rebuild the USA target from the verified baserom.
- [x] Confirm built `mzm_us.gba` SHA-1 equals the verified USA SHA-1.
- [x] Confirm `cmp` byte-for-byte identity.
- [x] Save non-copyrighted evidence: tool versions, commit pins, hashes, pass/fail result.

**Gate:** byte-identical USA reconstruction on the development Linux host. **PASSED 2026-09-28.**

### M0.3 — Semantic map

Build a repeatable mapping:

```text
ROM address → ARM/Thumb state → symbol → source file/function → notes
```

Required anchors include:

- cartridge entry;
- crt0/startup;
- `agbmain`;
- `InitializeGame`;
- `LoadInterruptCode`;
- `IntrMain`;
- main-loop HALT/VBlank path;
- soft reset;
- title/file-select path;
- SRAM initialization;
- audio initialization.

The decomp is used as the semantic layer; the ROM remains behavioral authority.

Current M0.3 progress:

- [x] Confirm ELF entry point at `0x08000000`.
- [x] Confirm startup `_start` at `0x080000C0`.
- [x] Confirm ARM IRQ handler `IntrMain` at `0x08000104`.
- [x] Confirm ARM→Thumb handoff to `agbmain` via pointer `0x0800023D`.
- [x] Extract the initial boot/runtime symbol set from the rebuilt ELF/map.
- [x] Derive ISA state for the wider function set using ELF ARM mapping symbols.
- [x] Produce a repeatable machine-readable address → ISA → symbol → object/source map.
- [x] Add title/file-select and main-loop HALT/VBlank anchors.
- [x] Close M0.3 with reproducible mapping output.

**Gate:** reproducible semantic map including boot, HALT/VBlank, Intro, Title and File Select anchors. **PASSED 2026-09-28.**

### M0.4 — GBARecomp Linux qualification

- [x] Pin GBARecomp commit.
- [x] Build framework/tools on Linux.
- [x] Run relevant upstream tests (34/34 passed).
- [x] Verify a normal Linux host build path.
- [x] Record compiler/CMake/host SDL2 versions.
- [x] Do not patch GBARecomp for MZM until the unmodified baseline is proven.

**Gate:** pinned GBARecomp builds on Linux and upstream CTest passes 34/34 with host/toolchain inventory captured. **PASSED 2026-09-28.**

### M0.5 — MZM cartridge analysis

Initial progress:

- [x] Run `gba_scan` against the verified USA ROM.
- [x] Confirm cartridge entry/header/save signature through GBARecomp.
- [x] Import the byte-matching decomp symbols and data layout.
- [x] Confirm imported function/data ranges are non-contradictory (`dropped-in-data=0`).
- [x] Resolve the startup IRQ ROM→IWRAM code copy from decomp symbols.
- [x] Run the first configured GBARecomp discovery/codegen pass.
- [x] Classify indirect transfers, undefined instructions, auto jump tables, and static roots.
- [x] Identify known dynamic executable regions: startup IRQ copy is modeled; NES Metroid runtime-loaded code is explicitly deferred to M4.
- [x] Determine whether any normal boot/title/gameplay construct is fundamentally unsupported by static analysis: none found in the first discovery/codegen pass.

Analyze the verified USA ROM and classify:

- ARM/Thumb code roots;
- interworking;
- indirect calls/branches;
- function-pointer tables;
- jump tables;
- ROM→RAM code copies;
- resume/interior-entry aliases;
- IRQ resume targets;
- BIOS/SWI use;
- DMA modes;
- HBlank/VBlank timing;
- timer use;
- PPU modes/features;
- audio FIFOs;
- SRAM;
- serial/link paths;
- dynamically installed code.

**Gate:** no fundamental unsupported execution model is discovered for the normal Zero Mission boot/title/gameplay path. **PASSED 2026-09-28.**

### M0.6 — Runtime support matrix

Current findings:

- [x] ARM/Thumb/interworking support present.
- [x] IRQ/VBlank/HBlank + HALT wake path present; MZM runtime validation still required.
- [x] Immediate, VBlank/HBlank-timed and sound-FIFO DMA present.
- [x] Timers 0..3 and IRQ/cascade support present.
- [x] SRAM and keypad support present.
- [x] Normal MZM PPU mode requirements fit supported tile/affine modes 0/1.
- [x] Windows, alpha blending and brightness paths are present.
- [x] PSG + Direct Sound FIFO A/B support present.
- [x] Serial/link infrastructure exists; MZM Fusion-link validation deferred to M4.
- [x] Identify PPU mosaic as an MZM-relevant engine gap.
- [x] Identify mutable same-PC `hazeCode` as a strict-static engine gap.
- [x] Pin and qualify boot-time fixed executable RAM copies (IRQ + audio A/B/C).
- [x] Pin remaining M0/M3-relevant fixed executable RAM copies (clipdata; later/deferred copies classified separately).
- [x] Regenerate once with clipdata mapping and confirm `code_copies=5`, `undefined=0`, and runtime dispatch root.
- [x] Assign implementation ownership/acceptance criteria for mosaic (generic GBARecomp PPU).
- [x] Assign implementation ownership/acceptance criteria for mutable code-copy variants (generic GBARecomp dispatch/code-copy support; hybrid allowed during bring-up).
- [x] Close M0.6 matrix.

Every required subsystem is classified as:

- **SUPPORTED**
- **SUPPORTED / VALIDATE**
- **GAP**
- **DEFERRED**

A gap is not automatically a no-go: ordinary framework improvements can be implemented or contributed upstream. A no-go would require a fundamental incompatibility with the intended static-recompilation approach.

**Gate:** runtime/hardware requirements are classified; fixed M0/M3-relevant executable-RAM copies are modeled; remaining gaps are bounded with ownership/acceptance plans. **PASSED 2026-09-28.**

### M0.7 — Upstream/duplicate audit

- [x] Track `metroidret/mzm`.
- [x] Track `mstan/gbarecomp`.
- [x] Track `MZM-Reprimed`.
- [x] Record project differentiation.
- [ ] Recheck before first public playable release.

### M0 exit criteria

M0 closes only if:

1. USA decomp rebuild is byte-identical.
2. GBARecomp builds and passes its relevant baseline on Linux.
3. MZM analysis finds no fundamental boot/gameplay blocker.
4. Identified ordinary gaps have an upstream/local ownership plan.
5. Repository boundaries prevent ROM/BIOS/generated copyrighted material from being committed.
6. Existing related projects remain documented accurately.

**M0 EXIT: PASSED / GO — 2026-09-28.**

Residual risks are tracked, bounded, and do not prevent M1 bootstrap:

- PPU mosaic rendering;
- mutable same-PC `hazeCode`;
- WAITCNT/prefetch accuracy;
- stack-local SRAM executable helpers;
- late Chozodia HBlank copy;
- NES Metroid dynamic executable regions.

---

## M1 — Static bootstrap

**Goal:** turn verified MZM machine code into a native Linux build that executes recompiled game code.

### M1A — Generation

- [x] MZM-specific cartridge config.
- [x] ROM identity guard.
- [x] ARM + Thumb discovery.
- [x] Initial fixed code-copy declarations.
- [x] Decomp-derived semantic seeds/indirect discovery baseline.
- [x] Generated C++ compiles without hand editing generated files.
- [x] Minimal Linux host links successfully.

**Gate:** generated MZM corpus + pinned GBARecomp runtime link into a native Linux executable. **PASSED 2026-09-28.**

### M1B — First execution

- [x] Native host application starts.
- [x] Verified cartridge is loaded.
- [x] Static-recompiled backend executes the session.
- [x] Native-recompiled MZM code executes successfully.
- [x] Hybrid interpreter/self-heal may be used temporarily for discovery.
- [x] Runtime remains mapped/handled (`unmapped=0`, `io_unhandled=0`).
- [x] Coverage debt is reported explicitly rather than hidden.

First-session evidence:

```text
native_calls=5193795
frames_presented=43921
dispatch_misses=18
self_heal_coverage=NOT_STATIC
```

**Gate:** deterministic first recompiled execution with trace evidence. **PASSED 2026-09-28 (hybrid).**

**M1 EXIT: PASSED — 2026-09-28.**

---

## M2 — Boot / title

**Goal:** reach the title flow correctly.

### M2A — Hybrid bring-up

- [x] Link statically recompiled BIOS and remove BIOS fallback misses.
- [x] Classify high-IWRAM misses as stack-local SRAM copied helpers.
- [x] Implement byte-verified transient-RAM canonicalization.
- [x] Re-run from a cold self-heal cache with `dispatch_misses=0`.
- [x] Confirm `interpreted_insns=0`, `unmapped=0`, and `io_unhandled=0`.
- [x] Capture semantic Intro/Title milestone hits under explicit strict-static enforcement.

Qualify:

- initialization;
- BIOS/SWI behavior;
- IRQ/VBlank;
- HALT wake-up;
- DMA;
- timers;
- PPU;
- input;
- SRAM;
- minimum viable audio.

Target route:

```text
reset → startup → InitializeGame → intro → title
```

### M2B — Strict-static title route

The exact validated route must run with strict-static enforcement and no hidden interpreter fallback.

**Gate:** Boot → Intro → Title with the strict-static metrics defined by the pinned GBARecomp version. **PASSED 2026-09-28.**

**M2 EXIT: PASSED — 2026-09-28.**

---

## M3 — Gameplay proof

**Goal:** prove that MZM is functioning as a game, not only as a boot demo.

### M3A — Hybrid gameplay

- [x] Title → New Game.
- [x] Opening → controllable Samus.
- [x] Traverse multiple early rooms.
- [x] Reach a Save Room.
- [x] Complete an SRAM save write.

```text
Title → New Game → opening → Samus control → first playable room
```

Validate:

- controls;
- collision;
- camera;
- sprites/backgrounds;
- room transitions involved in route;
- core audio;
- save behavior encountered by route.

### M3B — Static-verified gameplay

- [x] Repeat the qualified route under `GBARECOMP_STRICT_STATIC=1`.
- [x] Confirm `dispatch_misses=0`.
- [x] Confirm `interpreted_insns=0`.
- [x] Confirm `unmapped=0` and `io_unhandled=0`.
- [x] Persist a 32 KiB SRAM save during the enforced route.
- [ ] Validate save reload/round-trip compatibility (M4).
- [ ] Expand differential fidelity coverage beyond the proof route (M4).

Repeat the qualified route under strict-static enforcement with runtime validation.

**Gate:** controllable Samus in the first playable room with no unexpected dispatch/interpreter fallback on the qualified route. **PASSED 2026-09-28.**

The qualified run actually continued through multiple rooms to a Save Room and completed a save write while strict-static enforcement remained active.

**M3 EXIT: PASSED — 2026-09-28.**

This is the first milestone that may reasonably be described as a **static-recompilation gameplay proof of life**.

---

## M4 — Compatibility

### M4.1 — SRAM lifecycle

- [x] Save from strict-static gameplay.
- [x] Persist 32 KiB SRAM image.
- [x] Exit and relaunch the native host.
- [x] Existing save recognized.
- [x] Load save back into gameplay.
- [x] Reload session remains `FULLY_STATIC`, zero-miss, zero-interpreter.

**Gate:** basic save → exit → reload → gameplay round-trip. **PASSED 2026-09-28.**

### M4.2 — USA progression campaign

- [ ] Continue from the known-good save under strict-static enforcement.
- [ ] Record area/room progression checkpoints.
- [ ] Exercise major item acquisition and progression flags.
- [ ] Exercise bosses/minibosses.
- [ ] Exercise elevators/doors/transitions/cutscenes.
- [ ] Record visual/audio/timing defects independently of static coverage.
- [ ] Maintain reproducible before/after save hashes per diagnostic session.
- [ ] Resolve the first deterministic compatibility blocker before advancing past it.

See `docs/M4-COMPATIBILITY-CAMPAIGN.md`.

Expand from the proof route to complete original-game behavior:

- all areas and room transitions;
- bosses;
- items and progression;
- menus/map/status;
- cutscenes;
- saves and reloads;
- audio/music/SFX;
- endings and postgame;
- sequence variants;
- European target;
- Fusion Gallery/link functionality;
- unlockable NES Metroid.

### Dedicated M4 risk: NES Metroid

The bundled/unlockable NES emulator is treated as its own compatibility subproject because MZM dynamically loads executable code into IWRAM/EWRAM/VRAM. "Full game compatible" must not be claimed while this feature remains broken or bypassed.

---

## M5 — Enhancements

Enhancements begin only after a faithful baseline exists.

Candidates:

- window/fullscreen quality-of-life;
- integer/upscaled presentation;
- controller remapping;
- rumble;
- save states;
- fast-forward;
- adaptive/widescreen research;
- high-refresh presentation;
- achievements integration;
- Steam Deck packaging/validation.

Enhancements must remain separable from the faithful validation path.
