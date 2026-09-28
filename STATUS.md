# MZM-Recomp — Status

**Last updated:** 2026-09-28  
**Current phase:** M4 — Compatibility  
**M0 verdict:** **PASSED / GO**  
**M1 verdict:** **PASSED — native hybrid execution demonstrated**  
**M2 verdict:** **PASSED — strict-static boot/intro/title**  
**M3 verdict:** **PASSED — strict-static gameplay proof through multiple rooms and Save Room write**

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
| Rebuild MZM USA from decomp | **CONFIRMED** | Pinned `metroidret/mzm` rebuilt `mzm_us.gba` with the expected SHA-1 and `cmp` confirmed byte-for-byte identity |
| Export semantic symbol/address map | **CONFIRMED** | Reproducible CSV maps 21,082 ROM-range symbols using 15,575 ARM ELF mapping symbols; boot, HALT/VBlank, Intro, Title and File Select anchors are captured with ISA/source metadata |
| Build/qualify GBARecomp on Linux | **CONFIRMED** | Pinned framework built successfully on Ubuntu Linux; CTest passed 34/34; host/toolchain/SDL2 inventory captured; `gba_recompile`, `gba_scan`, and `bios_smoke` verified |
| MZM cartridge static-analysis scan | **CONFIRMED** | First configured discovery/codegen pass succeeded: 28,346 translation roots, 49 ARM / 28,297 Thumb, 9,540 indirect transfers, 335 auto jump tables / 8,675 targets, `undefined=0`; IRQ IWRAM code-copy emitted and dispatched |
| Hardware support matrix qualification | **CONFIRMED** | Final configured run reports `code_copies=5`, `undefined=0`, exit 0; clipdata RAM dispatch is emitted; mosaic/haze/WAITCNT/SRAM stack-code risks are bounded and assigned to later validation/work |
| Generated native C++ corpus | **CONFIRMED** | GBARecomp emits 16 shards plus dispatch/symbol metadata from the verified ROM |
| M1 minimal host scaffold | **CONFIRMED** | CMake runner + minimal `run_game` host + reproducible generation/build scripts committed and compiled successfully |
| M1A host build | **CONFIRMED** | `MZMRecomp` linked successfully as a Linux x86-64 ELF; 16 generated shards compiled without hand edits; `--help` runs |
| First native MZM execution | **CONFIRMED** | M1B hybrid run completed successfully with static-recompiled backend, 5,193,795 native calls, ~43.9k presented frames, `unmapped=0`, `io_unhandled=0`; 18 dispatch misses were bridged/self-healed and remain static-coverage debt |
| M2A static coverage | **CONFIRMED** | Cache-free run with recompiled BIOS + byte-verified SRAM stack canonicalizer reports `FULLY_STATIC`, `dispatch_misses=0`, `interpreted_insns=0`, `unmapped=0`, `io_unhandled=0`; explicit strict-static/title milestone run is next |
| Boot/intro/title | **CONFIRMED** | Strict-static run: `IntroHandler=YES` (97 hits), `TitleScreenHandler=YES` (140 hits), zero dispatch misses/interpreter instructions |
| Controllable Samus | **CONFIRMED** | Same strict-static session proceeded through New Game into controllable gameplay and multiple rooms |
| Strict-static gameplay route | **CONFIRMED** | Same enforced session reached a Save Room with `dispatch_misses=0`, `interpreted_insns=0`, `unmapped=0`, `io_unhandled=0`; SRAM write persisted as a 32 KiB `.sav` |
| Full-game compatibility | **PENDING** | M4 |
| NES Metroid compatibility | **PENDING** | Dedicated M4 workstream |
| Europe runtime support | **PENDING** | After USA bring-up proves architecture |
| Enhancements | **PENDING** | M5 only after compatibility baseline |

## M0.2 result — decomp reproducibility confirmed

The initial host-toolchain blocker was resolved by installing the missing ARM binutils. The pinned decomp then completed successfully and produced a byte-identical USA ROM reconstruction.

```text
MZM commit: 43b7fd52f552e4d38c1521ff9d4df5ee57e61493
Expected SHA-1: 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8
Original SHA-1: 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8
Built SHA-1:    5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8
BYTE_IDENTICAL: YES
```

M0.2 is therefore **CONFIRMED**.

## M0.3 result — semantic map confirmed

The rebuilt ELF/map now provides a reproducible semantic map for the verified USA ROM. Automatic ARM/Thumb/Data classification works through ELF mapping symbols, and the required boot/runtime/menu anchors are captured. The exact main-loop HALT instruction is `SVC 2` at `0x0800066C`; the flow resumes only after `gVBlankRequestFlag` is set by the VBlank/IRQ path.

M0.3 is therefore **CONFIRMED**.

## M0.4 result — functional Linux gate passed

The pinned GBARecomp baseline built successfully on Linux. The build reached 100% and produced the expected core tools including `gba_recompile`, `gba_scan`, and `bios_smoke`. The upstream CTest suite reported **34/34 tests passed, 0 failed**.

The compiler emitted warnings, including missing-field initializers in PPU smoke tests and several unused/extern-initialized variables, but no warning became a build failure.

Final host inventory:

```text
GBARecomp HEAD: e7728148c6829ba526f682876430a0c9022dc6c0
Host: Linux x86_64, Ubuntu 24.04-series kernel 7.0.0-31-generic
CMake: 3.28.3
GCC: 13.3.0
G++: 13.3.0
Python: 3.12.3
SDL2: 2.30.0
gba_recompile: OK
gba_scan: OK
bios_smoke: OK
```

M0.4 is therefore **CONFIRMED**.

## M0.5 progress — cartridge scan and decomp import

`gba_scan` accepted the verified USA ROM and reported a valid Nintendo header, entry branch to `0x080000C0`, game code `BMXE`, and SRAM save hardware identified by `SRAM_V`.

The GBARecomp decomp importer then consumed the byte-matching MZM ELF/sections/link-map evidence and produced 2722 function seeds: 8 ARM and 2714 Thumb, with **zero functions dropped for colliding with authoritative data ranges**. It also resolved the known IRQ ROM→IWRAM copy as `0x03000C7C <- 0x08000104`, size `0x200`.

The first configured `gba_recompile` discovery/codegen run then succeeded with `undefined=0`, emitted 28,346 translation roots, discovered 335 automatic jump tables with 8,675 targets, and materialized the copied IRQ entry in the dispatch table at `0x03000C7C`.

The finder-level "function" count is not a source-function count: GBARecomp materializes direct branch/case/control-flow roots as translation units. The 2,722 decomp functions remain the semantic function inventory.

M0.5 is therefore **CONFIRMED** as a static feasibility gate.

## M0.6 progress — hardware/runtime matrix

The pinned runtime covers the major hardware surfaces needed for boot/title/gameplay: ARM/Thumb execution and interworking, GBA memory, IRQ/VBlank/HBlank scheduling, HALT wake-up, immediate/timed/FIFO DMA, timers, SRAM, keypad, tile/affine PPU modes used by normal MZM, windows/blending, and Direct Sound/PSG audio.

Two bounded engine gaps have been identified:

1. **Mutable same-PC executable RAM code.** MZM reuses the same `hazeCode` RAM buffer for several different ROM source functions. Current fixed `[[code_copy]]` mappings resolve a runtime PC to one source mapping, so strict-static execution of all haze variants needs engine work.
2. **PPU mosaic rendering.** MZM actively uses MOSAIC in gameplay/sprite effects, while the pinned GBARecomp PPU does not currently render mosaic.

The three boot-time audio copies are now qualified:
`gSoundCodeA 0x03003B90 <- 0x08004464`,
`gSoundCodeB 0x030041EC <- 0x08004310`,
and `gSoundCodeC 0x03004294 <- 0x080043B4`.
With `IntrMain -> gInterruptCode`, the regenerated corpus reports `code_copies=4`, `undefined=0`, and successful code generation.

Remaining executable-RAM inventory work is centered on the clipdata helper for gameplay, the mutable same-PC haze buffer, and later/deferred Chozodia/NES paths.

Final M0.6 qualification added the fixed gameplay clipdata copy:

```text
clipdataCode 0x030016C4 <- ClipdataConvertToCollision 0x08057F7C
size=0x280
mode=Thumb
```

The final configured discovery/codegen run reports:

```text
exit=0
undefined=0
code_copies=5
TOTAL emitted=28411
codegen shards=16
```

and emits `gf_clipdatacode_entry` at `0x030016C4` in the symbol map, header, and dispatch table.

M0.6 is therefore **CONFIRMED**. The remaining known items — PPU mosaic, mutable same-PC `hazeCode`, WAITCNT/prefetch accuracy, stack-local SRAM helper execution, Chozodia HBlank code, and NES Metroid dynamic code — are tracked as bounded M1–M4 work rather than M0 feasibility blockers.

## M0 exit decision

**M0 — Feasibility: PASSED / GO.**

All M0 exit criteria are satisfied with reproducible evidence. MZM-Recomp may proceed to **M1 — Static bootstrap**. This decision is a feasibility result only; it is not a claim of a playable native build or strict-static runtime closure.

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


## M1B result — first native execution confirmed

The first full MZMRecomp runtime session used the verified USA ROM and the previously validated 16 KiB GBA BIOS.

Runtime evidence:

```text
cpu_backend=static-recompiled
self_heal_recompile=ENABLED
unmapped=0
io_unhandled=0
steps=1614165
cycles=10345585742
ppu_frames=43952
frames_presented=43921
native_calls=5193795
dispatch_misses=18
interpreted_insns=1180314
healed_native=9
failed=9
```

The application remained operational for a long interactive session and produced tens of thousands of presented frames. M1B therefore satisfies the project's first-execution gate.

The session is explicitly **HYBRID / NOT_STATIC**:

```text
self_heal_coverage=NOT_STATIC
```

Nine misses are BIOS PCs and nine are high-IWRAM Thumb PCs around `0x03007D08..0x03007D90`. The high-IWRAM cluster is consistent with the previously identified stack-local executable SRAM-helper risk, but that attribution remains pending fragment/source review.

M1B is **CONFIRMED / PASSED**. Strict-static closure is not claimed.


## M1 exit decision

**M1 — Static bootstrap: PASSED.**

The generated MZM corpus compiles into a native Linux host and the first interactive runtime session executed through the static-recompiled backend for tens of thousands of frames. Hybrid/self-heal coverage was used and reported transparently.

The project now enters **M2 — Boot / title**, where the target is to qualify the route:

```text
reset → startup → InitializeGame → intro → title
```

first in hybrid mode and later under the strict-static gate.


## M2A SRAM stack-code result

After linking the locally recompiled BIOS, all BIOS dispatch misses disappeared. The next run produced only eight Thumb misses in `0x03007D08..0x03007D90`.

The decomp/ELF disassembly proves that:

- `SramWriteUnchecked()` reserves `0x80` bytes on the guest stack, copies `SramWriteUncheckedInternal` into that local array, then calls `sp|1`;
- `SramCheck()` reserves `0xC0` bytes, copies `SramCheckInternal` into that local array, then calls `sp|1`;
- the System-mode stack base is `0x03007E60`;
- with a top-level `SramCheck()` frame, the local buffer begins exactly at `0x03007D90`, one of the observed misses.

Therefore the miss fragment's automatic "jump-table candidate" label is rejected for this range. These are transient copied-code entries whose absolute addresses vary with stack depth.

GBARecomp exposes `RuntimeRamDispatchHook` specifically for byte-verified position-independent code copied into transient RAM / moving stack frames. MZM-Recomp now installs a hook that recognizes exact runtime copies of:

```text
SramWriteUncheckedInternal 0x080051D4 size=0x24 Thumb
SramCheckInternal          0x0800529C size=0x30 Thumb
```

and dispatches their already-generated native canonical translations.

Validation is still pending; strict-static is not claimed until a fresh cache-free run reports zero unexpected misses.


## M2A static-coverage result

The first cache-free run after enabling the byte-verified transient-RAM canonicalizer completed with:

```text
cpu_backend=static-recompiled
unmapped=0
io_unhandled=0
ppu_frames=4131
frames_presented=4129
self_heal_coverage=FULLY_STATIC
dispatch_misses=0
interpreted_insns=0
healed_native=0
```

This validates the SRAM stack-code solution and proves that, for the tested session, neither BIOS nor cartridge/RAM execution required interpreter fallback or self-healed overlays.

This is a static-coverage result, not yet the formal M2B title-route verdict. M2B still requires:

1. an explicit `GBARECOMP_STRICT_STATIC=1` run; and
2. objective evidence that the tested route reaches `IntroHandler` and `TitleScreenHandler`.

An optional `MZM_MILESTONE_TRACE=1` runner probe records those semantic anchors without synthesizing input.


## M2/M3 qualification result

A single explicit strict-static session with `GBARECOMP_STRICT_STATIC=1` and semantic milestone tracing progressed beyond the M2 target and through the M3 gameplay-proof route.

Runtime evidence:

```text
cpu_backend=static-recompiled
final_pc=0x000001B4
unmapped=0
io_unhandled=0
steps=181013
cycles=1151698528
ppu_frames=6891
frames_presented=6889

self_heal_coverage=FULLY_STATIC
dispatch_misses=0
interpreted_insns=0
healed_native=0

mzm_milestones intro_handler=YES intro_hits=97
               title_handler=YES title_hits=140
```

Operator-observed route during this same enforced session:

```text
Boot
→ Intro
→ Title
→ New Game
→ controllable Samus
→ multiple early rooms
→ Save Room
→ save command completed
```

The runtime persisted a 32 KiB SRAM file:

```text
size=32768
SHA-256=471f0af7a3b315f8ac39b7185e157e6f3fe4ef70be165961a986eaab6ede3c67
```

This closes:

- **M2A — bring-up**
- **M2B — strict-static title route**
- **M2 — Boot / Title**
- **M3A — gameplay proof**
- **M3B — strict-static gameplay route**
- **M3 — Gameplay proof**

The save result proves write persistence for the exercised route. It does **not yet** prove full save compatibility or reload/round-trip behavior; those remain M4 validation items.

The active phase is now **M4 — Compatibility**.
