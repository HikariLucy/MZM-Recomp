# M4 NES Metroid executable subsystem audit

Date: 2026-09-29. Target: USA rev 0 ROM (SHA-256 `fc94f65380b65b870a30b9b04b39cca1dc63d6e46a4a373d3904adc0912ebc37`). MZM HEAD `770349f`; integrated GBARecomp `984957a4f1c70379e9ce6717c1fd080aecf7e37d`. This is an architectural audit, not a NES gameplay qualification. Source references below are in the local `Metroid-ZeroMissionRecomp/_m0/upstream/mzm` decomp unless otherwise stated. Its *main* `mzm_us.map`/ELF and built `mzm_us.gba` exist; the built main ROM is **byte-identical** to the legal USA ROM. The nested NES emulator/payload maps and binaries are absent. Sizes below therefore distinguish actual DMA counts and ROM observations from unbuilt linker section sizes.

## First executable frontier and boot chain

```
OptionsNesMetroidHandler (0x0807AA74, Thumb; submenu stage 4)
  IME=0, IF=FFFF; r0=0x08000000; indirect call
  -> sNesEmuBootLoader at 0x087D8000 (ARM word EB000033)
  -> BL 0x087D80D4 (_start, ARM; ROM bytes)
       set IRQ/SVC/System stacks; r12=0x08000000
       SWI 0x11: LZ77 stream 0x087D8150 -> 0x03007400
       pop {r0,pc}: r0=0x087D8150, pc=0x03007400 (ARM)
  -> payload: WAITCNT=0x0014; custom decompression to IWRAM staging
       DMA VRAM/IWRAM/EWRAM images and NES ROM data
       ldm r5!,{r9,r10,pc} -> 0x06006558 (ARM)
  -> emulator Part 1 entry; VRAM/IWRAM/EWRAM cross calls
  -> Quit Yes: 0x0600ECFC -> 0x0600ED14 cleanup ->
       restore SP, ldm sp!,{pc} -> 0x087D8124 (ARM)
       BX Thumb at 0x087D812C; IME=0, reset byte 0x03007FFA=0;
       RegisterRamReset(0xFB), SoftReset -> 0x08000000
```

The first missing normal generated entry is **0x087D8000 ARM**, not the RAM payload: `generated/data_symbol_map.cpp` calls it data and `generated/dispatch_table.cpp` has no `0x087D8000`, `0x087D80D4`, `0x03007400`, or `0x06006558` entry. The ROM word `EB000033` computes target `0x087D8000+8+0xCC = 0x087D80D4`. The main map places the word in `src/data/nes_metroid.o` `.rodata`; normal function discovery did not promote it. A controlled strict-static call would miss here, *before* payload. The static ROM loader is the first implementation target; payload native execution becomes the following target.

The loader's literal `0x13850021` and `r0=0x087D8150` choose the first custom-compressed source at `0x087D8360`; later `ldrh [0x087D814C+2] = 0x1385` selects `0x087DCF60`. These addresses are stream bytes, not final ARM opcodes. The loader LZ stream starts `10 14 02 00`: uncompressed size **0x214**, compressed consumption **0x206** bytes, ending at `0x087D8356`. Local pure-Python BIOS-LZ77 reconstruction to ignored `.local/nes-payload-usa.bin` gives SHA-256 `e94f6dba7b7ec0dd183335fa2efdd5bb5a1f4dc1f7593d8e8961b1e2ce681f44`; the complete 0x214-byte image is absent from the ROM as a contiguous byte sequence. It has ARM entry `0x03007400`; code and literal/DMA tables occupy `0x03007400..0x03007614` (last word at `0x03007610`). Function labels `start_code` and `_03007458` are only named starts, not a complete CFG/function count. No ROM `source_addr` points to these final opcodes.

At `0x03007408`, payload executes `strh r9,[r4,r10]` with `r9=0x01300014`, `r4=0x040000D4`, `r10=0x130`: WAITCNT `0x04000204` receives `0x0014` before any custom decompression. Integrated `GbaBus::access_cycles` reads live WAITCNT. For `0x0014`: SRAM 5, WS0 N=4/S=2, WS1 N=5/S=5, WS2 N=5/S=9 cycles per 16-bit transfer; 32-bit ROM reads add the bank's S half. Prefetch bit 14 is clear, so missing Game Pak prefetch does not affect this NES setting directly. This is a model inspection, not a timing oracle.

## Executable and data load map

All DMA counts below are low 16 bits of the actual CNT words in `payload/asm/code.s`, multiplied by width (`0x8400....` = 32-bit increment; `0x8500....` = 32-bit fixed source). They describe **transferred span**, which may contain literals/data and must not be called all instructions. The first eight 0x1000-byte custom-decompression blocks stage the emulator at `0x06000000..0x06008524`; further blocks stage NES ROM data into `0x0201C000..0x0203C000`. The decomp comments' block-by-block lengths are not used as authority.

| DMA source | DMA destination | CNT | Width / words / bytes | Role |
|---:|---:|---:|---:|---|
| `0x06007C44` | `0x0203E000` | `0x84000238` | 32 / `0x238` / `0x8E0` | Part 6 |
| `0x06006EBC` | `0x0600E000` | `0x84000362` | 32 / `0x362` / `0xD88` | Part 5 |
| `0x06006E5C` | `0x0600C000` | `0x84000018` | 32 / `0x18` / `0x60` | Part 4 |
| `0x06006D0C` | `0x0600B000` | `0x84000054` | 32 / `0x54` / `0x150` | Part 3 |
| `0x060012C0` | `0x03000000` | `0x84001693` | 32 / `0x1693` / `0x5A4C` | Part 2 |
| `0x06000080` | `0x06006000` | `0x84000490` | 32 / `0x490` / `0x1240` | Part 1 |
| `0x03000100` | successive `0x06000000..0x06008524` and `0x0201C000..0x0203C000` | computed `0x8400nnnn` | 32 / variable | decompression staging; final-block count depends on cursor |

| Image / classification | Runtime transferred range | Bytes | Mode / source and transition | Mutation assessment |
|---|---:|---:|---|---|
| Payload, mixed code/literals | `0x03007400..0x03007614` | `0x214` | ARM; BIOS LZ77 from `0x087D8150` | Writes DMA/literal table at `0x03007604` during execution; mixed image, **mutable data** |
| Part 1, mixed code/tables | `0x06006000..0x06007240` | `0x1240` | ARM handwritten; staged `0x06000080`, final DMA `0x84000490` | Writes other VRAM; self-write of executable bytes unproven |
| Part 2, mixed code/data | `0x03000000..0x03005A4C` | `0x5A4C` | Thumb compiled audio plus ARM handwritten NES core; staged `0x060012C0`, DMA `0x84001693` | IWRAM `0x03005A4C..` is state/audio buffers, not code; self-write unproven |
| Part 3 | `0x0600B000..0x0600B150` | `0x150` | ARM handwritten; `0x06006D0C`, DMA `0x84000054` | unverified after load |
| Part 4 | `0x0600C000..0x0600C060` | `0x60` | ARM handwritten; `0x06006E5C`, DMA `0x84000018` | Part 1 also writes transformed bytes into `0x0600C000`/`0x0600C480`; **overlaid/mutable**, exact active code image must be gated |
| Part 5, mixed `.rodata/.text` | `0x0600E000..0x0600ED88` | `0xD88` | Thumb entry then ARM handwritten; `0x06006EBC`, DMA `0x84000362` | self-write unproven |
| Part 6, mixed code/data | `0x0203E000..0x0203E8E0` | `0x8E0` | Thumb compiled C/SRAM utilities; `0x06007C44`, DMA `0x84000238` | Copies two SRAM helper functions onto System stack for execution: additional mutable **stack code** |

Part 1 finalization copies `0x06000080..0x060012C0` into `0x06006000`, clears `0x06000080..0x06004000` with fixed-zero DMA, and copies the first `0x80` bytes of `0x06000000` to `0x0600B800`. Other setup transfers: `0x03007560 -> OAM` 0x400 bytes fixed source; zero -> PALRAM 0x400 bytes; zero -> OBJ VRAM `0x06010000..0x06018000`; zero -> `0x03005A4C..0x03005FE4` (`0x166*4=0x598`). These are **not** additional executable images. The Part 1 loader staging footprint and Part 4 overlay require a dynamic ownership/lifetime map, not one broad VRAM-executable flag.

The NES ROM at `0x0201C000..0x0203C000` (0x20000 bytes) is emulated **6502 cartridge data**, not GBA ARM/Thumb code. `0x0203C000..0x0203E000` and `0x03000100..0x03002100` participate in decompression/staging and state; the former's exact semantic subdivisions need runtime trace. `0x06000000..0x06008524` is temporary mixed staging, then portions become VRAM code and graphics. PALRAM/OAM/OBJ VRAM are graphics. `.rodata`, literals and jump tables inside each transferred code span must be excluded from function counts. The nested linker script gives VMA origins and object order, but its generated map is unavailable; exact per-function coverage and nested `.text` ends remain **UNVERIFIED**. Source `arm_func_start` lower bounds: Part 1 6, Part 2 handwritten assembly 108 (plus compiled C), Part 3 5, Part 4 4, Part 5 33 (first function Thumb), payload 2. These are label counts, **not** final function counts.

## Provenance and suitability

| Image | Source provenance | Compressed? | Existing ROM `source_addr`? | Private relocation / dispatch | Resume / status |
|---|---|---|---|---|---|
| ROM stub + loader | Literal ROM at `0x087D8000`, `0x087D80D4` | No | Yes, ordinary ROM image | Normal public generation needed; currently undiscovered | Loader return `0x087D8124` required; **NEEDS SMALL EXTENSION** |
| Payload | BIOS LZ stream `0x087D8150`; final image only after expansion | Yes | **No** | Private entry machinery could run IWRAM PC after generator can read external image | Interior resume likely; **NEEDS ARCHITECTURE** |
| Parts 1/3/4/5 | Custom compressed ROM streams -> VRAM staging -> DMA | Yes | **No** | Current RAM hook excludes VRAM; private CFG/literal handling also hardcodes RAM | Extensive resume/cross-part calls; **NEEDS ARCHITECTURE** |
| Part 2 | Custom compressed -> VRAM staging -> IWRAM DMA | Yes | **No** | Hook range covers PC; byte gate/source corpus missing | IRQ/cross-part resume; **NEEDS ARCHITECTURE** |
| Part 6 | Custom compressed -> VRAM staging -> EWRAM DMA | Yes | **No** | Hook range covers PC; source corpus missing; stack helpers add private code | **NEEDS ARCHITECTURE** |
| NES cartridge bytes | Custom compressed ROM data -> EWRAM | Yes | Inapplicable | Not ARM/Thumb executable | Data only |

GBARecomp `gba_recompile` reads **one input image** (`--rom` or `--bios`) with one `load_address`/base. `[[extra_func]] source_addr` and `[[code_copy]]` map back into that image. `FunctionFinder::walk_source_for` validates source within `rom_`; `main.cpp` emits from the same buffer. The CLI can override a *single* base with `--rom-base`, but there is no multi-image or external executable-image declaration. Pointing `source_addr` at compressed bytes would translate unrelated instructions. A second standalone generator invocation would duplicate `kDispatchTable`, `kPrivateDispatchTable`, `runtime_invoke_private_entry`, generated headers, symbol maps, and likely function names; combining it requires explicit namespacing, shared ABI, cross-corpus calls and resume dispatch. It is not a small workaround. A multi-image input layer in one generator is the smaller coherent architecture, after exact local image extraction exists.

In integrated `runtime_dispatch`, `g_runtime_force_interp_hook` sees all PCs, but strict-static must not use it. `g_runtime_ram_dispatch_hook` is invoked only for `0x02000000..0x03FFFFFF`; `runtime_has_static_entry` sees only public dispatch. `runtime_invoke_private_entry` explicitly scans the private table without a RAM address guard, so a VRAM private *root* can in principle be called directly. However `FunctionFinder::can_read_literal`/`read_literal` and same-image private CFG propagation restrict relocated bodies to `<0x04000000`. A local synthetic test took the upstream private-relocation fixture unchanged except relocating `0x030010xx` to `0x060010xx`, using the integrated generator. It **accepted** the private root `0x06001000`, preserved its logical PCs and emitted private interior-resume entries `0x06001002..08`; but it incorrectly emitted the CFG child `0x0600100C` into the **public** dispatch table. The original fixture's no-public-entry assertion fails. Thus VRAM private relocation is **not safe/complete** today; direct `runtime_invoke_private_entry` execution and VRAM MMIO trace were not run after this generator failure. Ordinary `runtime_dispatch(0x06006558)` misses. The smallest generic runtime change is an opt-in **dynamic executable dispatch hook** over game-declared verified ranges, with game-owned full-byte validation and no interpreter fallback. Generator relocation must independently accept VRAM literals/CFG; the hook alone cannot fix that. Add negative-byte, root, interior-resume, IRQ and MMIO-PC tests. No GBARecomp source changed in this audit.

## Lifecycle, BIOS, hardware, and code mutation

Boot disables IME, sets IRQ/SVC/System stacks, decompresses and executes payload. Run uses VRAM code with calls to IWRAM audio/NES core and EWRAM save/password utilities; Part 1 `sub_06006558` calls `sub_0600E754`, `sub_0600E4D0`, `sub_0600E794`, `sub_0600C00C`, `sub_03005568`, `sub_030057A8`, and EWRAM `EmulatorSaveToPasswordBytes`. Part 4 transfers to `sub_06006000`. IRQ handlers and callback graph still require a nested-map plus runtime trace. Save/password use SRAM near `0x0E007FB0`/`0x0E007FD8`; SRAM routines alter WAITCNT SRAM bits and copy helpers to stack. Quit Yes cleans timers/DMA/IME and returns to the saved loader PC; loader uses `RegisterRamReset(0xFB)` then `SoftReset`, selecting ROM `0x08000000` via `0x03007FFA=0`. SRAM is excluded from the reset flags and must persist. Reset menu is distinct from quitting; full behavior remains unqualified.

Observed BIOS SWIs in source: loader `0x11` (LZ77), `0x01` (RegisterRamReset), `0x00` (SoftReset); Part 5 `0x12` (VRAM LZ77), `0x11`, `0x02` (Halt), `0x19` and `0x03`; Part 2 handwritten `0x02`. Integrated GBARecomp has a generated BIOS dispatch corpus and an HLE service table, but none of these calls has passed the NES boot path; mark **implemented but unqualified** pending controlled execution. Hardware dependencies visible in source: DMA3 code/data loads and graphics, DMA1 FIFO A, timers and sound, VBlank/HBlank/IRQ/IME, KEYINPUT, PALRAM/OAM/VRAM writes, SRAM, WAITCNT, and VRAM instruction fetch. Existing substrate for each has M4 baseline evidence only; NES-specific timing/IRQ/audio/graphics are **UNVERIFIED**.

The executable images are not globally immutable. Payload mutates its DMA table; Part 1 writes/overlays Part 4 VRAM; Part 6's SRAM helpers execute stack copies. No proof yet excludes instruction self-modification in Parts 1/2/3/5/6. Each private image must be checked at entry/resume against exact expected bytes; mutable overlays need separate image identities. Do not mark a whole VRAM/EWRAM bank executable.

## Roadmap and first controlled probe

| Milestone | Gate |
|---|---|
| NES-0 | Reconstruct and hash exact payload and six emulator images from verified local ROM; obtain per-image instruction/data maps; compare nested build only if available |
| **NES-1a (QUALIFIED)** | Generate exact public ROM islands and prove real BL → loader → SWI 0x11 → **first RAM PC `0x03007400`**, with byte gate, CPU state, and zero earlier strict-static misses; PASS |
| **NES-1b (QUALIFIED)** | Execute the locally derived `0x214`-byte payload at `0x03007400` strict-static; byte-verified, WAITCNT=0x0014 write, DMA loops completed, post-payload frontier `0x06006558` reached with zero misses; PASS |
| NES-2 | Payload executes through all decompression/DMA stages to `0x06006558`; generic VRAM dispatch and external-image support qualified |
| NES-3 | Emulator initializes and yields first frame with zero strict-static counters |
| NES-4 | Title, input and audio qualified against a reference |
| NES-5 | Password/SRAM/save and Quit/Reset behavior qualified |
| NES-6 | SoftReset returns cleanly to MZM at `0x08000000` with preserved SRAM |

A future local boot-chain fixture may invoke the real ROM entry with documented minimum state and the legal ROM, stopping at `0x03007400`. It must capture the expected frontier without changing production strict-static semantics, and compare guest IWRAM to a fresh local LZ77 reconstruction. Such a fixture proves integration, not gameplay. The next implementation target remains NES-1a because strict-static currently fails at the ROM data-as-code stub before any decompression. After that, executable-image input work becomes the prerequisite for NES-1b. No large generator/runtime refactor begins in this audit.

## Validation and open evidence

Baseline regression on integrated binary: 01, 02, 03 **PASS**, all four strict-static counters zero. Case 03: 317 frames, WAITCNT `0x45B4`, final PC `0x000001B4`. The built MZM CTest suite is **53/53 PASS** (three MZM-local and 50 integrated GBARecomp tests). `build-m4-integration/CMakeCache.txt` points to the exact integrated GBARecomp worktree and its HEAD is the pin above. The synthetic VRAM fixture and generated output remain ignored under `.local/nes-vram-fixture*`; its public-child failure is reproducible. Nested emulator/payload maps and binaries, exact custom-decompressed image hashes, a **passing** VRAM private runtime fixture, real boot checkpoint, and NES gameplay remain unavailable/unqualified. No derived ROM image is tracked.

## NES-1a implementation probe (2026-09-29): blocked in pinned generator

This probe used only the local USA ROM, SHA-1 `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8`, and the pinned integrated `gba_recompile`. ROM bytes at `0x087D8000` are `33 00 00 EB` (`BL 0x087D80D4`). The loader/reset instruction bytes agree with `nes_metroid/asm/loader.s` and the main map. The imported overlay's single `[[data_range]]` is `[0x087D8000,0x087F7734)`. It covers **all four** requested entries; the map gives `.rodata` for the whole NES blob, so merely adding `[[extra_func]]` is invalid.

The exact source-backed executable islands are `[0x087D8000,0x087D8004)` ARM (one BL), `[0x087D80D4,0x087D8114)` ARM (loader and post-SWI `pop {r0,pc}`), `[0x087D8124,0x087D812C)` ARM (reset interwork), and `[0x087D812C,0x087D8140)` Thumb (reset calls). The adjacent ranges `[0x087D8004,0x087D80D4)`, `[0x087D8114,0x087D8124)`, and `[0x087D8140,0x087F7734)` must remain data. `sNesMetroidData_Prologue` is not a trustworthy code/data bound by itself; these bounds come from the map, ROM words, and loader assembly. Local temporary overlay/config probes are ignored under `.local/nes-*`; neither the imported overlay nor generated production corpus was changed.

With that exact split and only `0x087D8000` seeded, the generator exits with `ERROR: 1 control-flow entries into [[data_range]]`: it enters **`0x087D8004`** from `nes_rom_trampoline`. `FunctionFinder::discover_one` continues after every call-shaped BL, even this one-instruction transfer. Relaxing the data range would decode NES data as ARM and would fail the required boundary. An isolated `0x087D80D4` seed succeeds: it yields a public ARM loader entry plus an automatically discovered public ARM SWI continuation at `0x087D8110`. Its generated `LDM` loads `r0` and `pc` from the System stack and calls `runtime_dispatch(g_cpu.R[15])`. This is codegen inspection, **not** a boot execution or payload proof.

The existing far-BL analysis also cannot establish this transfer as non-returning: `link_register_dead_at` returns false on the loader's first `MSR CPSR` at `0x087D80E4` (it treats mode changes as unknown LR-bank changes). Consequently, if the finder boundary alone were fixed, the ordinary BL lowering would still push a host return continuation for `0x087D8004`. A correct NES-1a change must make both discovery and lowering use a proven non-returning transfer fact, while still writing the guest LR architecturally and leaving no host return frame. The proof can be an explicit, address-specific config annotation checked against the ROM bytes and target, or a generator analysis that safely models this mode-switching loader. The local source cannot solve this with `extra_func`, `exclude_func`, or a wider executable gap.

With the loader and both reset entries manually seeded in the same narrow overlay, the finder also reports four entries into data at `0x087D8140` after Thumb `SVC 0` (`SoftReset`). It synthesizes a continuation after the non-returning BIOS service and even an interior function at `0x087D813E`. This needs a proven non-returning SWI/soft-reset terminator in discovery and emission, with no data continuation. Computed `ORR r1,pc,#1; BX r1` discovery of `0x087D812C` cannot be qualified while this collision remains; manual Thumb seeding is a safe fallback once termination is fixed.

**NES-1a status: BLOCKED.** No production ROM entries were added; `0x087D8000`, `0x087D80D4`, `0x087D8124`, and `0x087D812C` remain absent from the normal dispatch table. No local-ROM boot frontier, guest payload hash, CPU frontier state, reset-stub PASS, or NES BIOS PASS is claimed. The first expected dynamic frontier remains `0x03007400` ARM, but reaching it has not been demonstrated. Per the task's upstream boundary, stop before changing GBARecomp. After a reviewed generic finder/codegen fix, regenerate with the exact data islands, add only necessary seeds, verify no entry at `0x087D8004` or inside adjacent data, then run the controlled first-frontier fixture and existing regressions. NES-1b begins only after that gate passes.

Regression check after the probe: MZM/integrated CTest **53/53 PASS** (including two resolvers, native RAM and upstream `link_branch_tests`); launcher state CTest **1/1 PASS**; M4 Python **9/9 PASS**; strict-static cases **01/02/03 PASS**, each with `dispatch_misses=0`, `interpreted_insns=0`, `unmapped=0`, `io_unhandled=0`. Case 03 remains 317 frames and WAITCNT `0x45B4`. No NES loader integration test exists yet because generation fails at the real data boundary. `git diff --check` passes; integrated GBARecomp stays clean at the pinned HEAD.

## NES-1a qualification (2026-09-29): PASS

Following generic non-returning call (`returns = false`) and terminal SoftReset flow modeling in GBARecomp integration `984957a4f1c70379e9ce6717c1fd080aecf7e37d`, the NES executable islands were generated and verified.

The production configuration in `configs/mzm-us.toml` defines:
- `0x087D8000` ARM (`nes_rom_trampoline`)
- `0x087D80D4` ARM (`nes_rom_loader`, `returns = false`)
- `0x087D8124` ARM (`nes_rom_reset_arm`)
- `0x087D812C` Thumb (`nes_rom_reset_thumb`)

The symbols overlay is deterministically prepared via `scripts/prepare-nes-overlay.py`, splitting the monolithic data range into:
- `0x087D8004..0x087D80D4` (data before loader)
- `0x087D8114..0x087D8124` (loader literal pool)
- `0x087D8140..0x087F7734` (data after reset stub)

Generated dispatch table entries in `generated/dispatch_table.cpp` within `0x087D8000..0x087F7734`:
1. `0x087D8000` ARM (`gf_nes_rom_trampoline`)
2. `0x087D80D4` ARM (`gf_nes_rom_loader`)
3. `0x087D8110` ARM (`gf_afunc_087D8110`, SWI 0x11 continuation)
4. `0x087D8124` ARM (`gf_nes_rom_reset_arm`)
5. `0x087D812C` Thumb (`gf_nes_rom_reset_thumb`)
6. `0x087D813E` Thumb (`gf_tfunc_087D813E`, RegisterRamReset continuation)

Data suppression is 100% verified: `0x087D8004` is NOT generated, and no entries exist in the literal pools `0x087D8114` or `0x087D8140`.

### Real execution qualification (`tests/m4/nes_loader_frontier_test.cpp`)
- ROM SHA-1: `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8` (PASS)
- Route: `0x087D8000` (trampoline) → `0x087D80D4` (loader) → SWI 0x11 (LZ77) → `0x087D8110` (continuation) → `pop {r0, pc}` → `0x03007400`
- Dynamic frontier reached: `PC = 0x03007400`, mode = ARM
- Host call stack depth: 0
- Strict accounting before frontier: `dispatch_misses = 0`, `interpreted_insns = 0`, `unmapped = 0`, `io_unhandled = 0`, `self_heal = disabled`
- CPU state at frontier:
  `pc=03007400 mode=ARM cpsr=0000001F sp=03007EF8 lr=087D8004`
  `r0=087D8150 r1=03007614 r2=03007FA0 r3=00000000 r4=040000D4 r5=13850021 r6=00000000 r7=00000000 r8=00000000 r9=00000000 r10=00000000 r11=087D8124 r12=08000000`
- Guest IWRAM payload: `0x03007400..0x03007614` (`0x214` = 532 bytes)
- Payload SHA-256: `e94f6dba7b7ec0dd183335fa2efdd5bb5a1f4dc1f7593d8e8961b1e2ce681f44` (byte-for-byte match against independent local ROM reconstruction)
- Quit/reset stub: `0x087D8124` ARM → `0x087D812C` Thumb → `RegisterRamReset` → `0x087D813E` Thumb → `SoftReset` (SWI 0) → clean cartridge re-entry at `0x08000000` ARM with no execution of subsequent literal pool bytes.

CTest: `mzm-nes-loader-frontier` PASS (54/54 overall suite PASS).
Harness cases 01/02/03 PASS (Case 03: 317 frames, `WAITCNT=0x45B4`, `final_pc=0x000001b4`).

## NES-1b qualification (2026-09-29): strict-static payload execution (PASS)

Milestone **NES-1b** is fully qualified and verified.

### Architecture & implementation
1. **Multi-Image Support in GBARecomp (`[[executable_image]]`)**:
   - Upstream GBARecomp extended with `ExecutableImageRegistry` in `src/recompile/executable_image.h` / `src/recompile/executable_image.cpp`.
   - Ingests `.local/nes-payload-usa.bin` derived locally from legal USA ROM at build time (`scripts/extract-nes-payload.py`).
   - Zero Nintendo-derived binaries committed to the git repository.
   - Enforces SHA-256 verification (`e94f6dba7b7ec0dd183335fa2efdd5bb5a1f4dc1f7593d8e8961b1e2ce681f44`) and address overlap rejection.
   - `FunctionFinder` propagates `private_entry` along CFG branches within the same secondary executable image, preventing private internal functions from leaking into `kDispatchTable` and ensuring full resume aliases in `kPrivateDispatchTable`.
   - `g_runtime_ram_dispatch_hook` extended in GBARecomp to service VRAM execution (`0x06000000..0x07000000`).

2. **MZM Configuration**:
   - `configs/mzm-us.toml` defines `[[executable_image]] id = "nes_payload"`, data ranges `[0x03007450, 0x03007458)` and `[0x03007560, 0x03007614)`, and private roots `0x03007400`, `0x03007458`, and `0x030074E4` (`dispatch = false`).
   - Generates 16 shards with `undefined=0`, mapping all 133 ARM instructions of the payload into `kPrivateDispatchTable`.

3. **Runtime Execution & Byte Gate**:
   - `mzm_ram_dispatch` hooks ARM dispatches in `0x03007400..0x03007614`.
   - On initial entry (`0x03007400`), verifies the complete 532-byte payload image against SHA-256 `e94f6dba...`.
   - Latches payload authentication for subsequent internal loops and DMA cursor updates.
   - Executes strictly static native translations via `runtime_invoke_private_entry`.

### Real execution qualification (`tests/m4/nes_payload_frontier_test.cpp`)
- Chain: Trampoline `0x087D8000` → Loader `0x087D80D4` → SWI 0x11 LZ77 → Continuation `0x087D8110` → Pop PC `0x03007400` → Native payload execution → Post-payload frontier `0x06006558`.
- WAITCNT: `0x0014` written at `0x03007408` (verified after payload completion).
- Decompression/DMA staging loop runs to completion in native AOT code.
- Post-payload frontier reached:
  - `PC = 0x06006558` (ARM mode in VRAM)
  - `SP = 0x03007EF8`
  - `LR = 0x087D8004`
  - Host call stack depth: 0
  - `dispatch_misses = 0`, `interpreted_insns = 0`, `unmapped = 0`, `io_unhandled = 0`
  - Zero self-heal misses (`!gbarecomp::self_heal_any_misses()`)
  - NES payload dispatch hits: 36,785 / 36,785 matches.

### Verification suites
- MZM CTest: **56/56 PASS** (including `mzm-nes-loader-frontier` and `mzm-nes-payload-frontier`).
- GBARecomp CTest: **51/51 PASS**.
- Pytest: **9/9 PASS**.
- M4 regression suite: Cases 01, 02, 03 **PASS** (Case 03: 317 frames, `WAITCNT=0x45B4`, `final_pc=0x000001b4`).

## NES-2 qualification (2026-09-29): emulator initialisation frontier (PASS, Part 2 blocked upstream)

Route, all native, strict-static, no interpreter, no self-heal, from the legal local ROM only:

```
0x087D8000 ARM -> loader -> BIOS LZ77 -> 0x03007400 payload (AOT) -> 0x06006558 ARM (Part 1)
  -> Part 6 SRAM probe (0x0203E414) -> Part 5 (0x0600E4E8) -> Part 1 (0x06006890, 0x060068D4)
  -> Part 5 Thumb LZ77 loop (0x0600E048, 1395 native entries) -> Part 4 boot (0x0600C00C)
  -> Part 1 font setup (0x06006000) -> Part 5 sub_0600E1C4 (IE=0x2031 IF=0xFFFF IME=1)
  -> bx r4 -> 0x03000488 Thumb = EmulatorAudio_Initialize (Part 2)   <-- new frontier
```

`tests/m4/nes_emulator_frontier_test.cpp` (CTest `mzm-nes-emulator-frontier`) runs the whole chain from `0x087D8000`. Phase A reproduces NES-1b exactly (`PC=0x06006558 ARM SP=0x03007EF8 LR=0x087D8004 CPSR=0x6000001F`, WAITCNT `0x0014`) and asserts the six guest images equal the ROM-derived images byte for byte. Phase B lifts the NES-1b stop and runs to the first verified emulator PC with no native entry.

### Image table (all six guest-matched at `0x06006558`)

| Part | Runtime range | Size | Whole-image SHA-256 | Guest match | Mutability observed to the frontier | Corpus |
|---|---|---:|---|---|---|---|
| 1 | VRAM `0x06006000..0x06007240` | `0x1240` | `eff34fbc387676a159dce57aff8089a2d6e355eaf96ecdef32155422c14da6d5` | yes | **SELF-MODIFYING**, 2 bytes `0x06006700..0x06006702` (see below); gate excludes exactly these | 6 seeds ARM, 6 data ranges |
| 2 | IWRAM `0x03000000..0x03005A4C` | `0x5A4C` | `15df40ee533211143aae8cd3deb6062f9d0364135479b0c361e9a24ca6ff0fb2` | yes | MUTABLE DATA ONLY (29 bytes: `0x030029AC..0x030029D0`, `0x03005809`, `0x0300580B`); identified by code runs only | **none (blocked, below)** |
| 3 | VRAM `0x0600B000..0x0600B150` | `0x150` | `c9978bfe63c71e714f5b95c1324abcbddfd1a613a74a44c84f158438f421c3d9` | yes | IMMUTABLE AFTER LOAD (observed; never dispatched before the frontier) | 5 seeds ARM |
| 4 | VRAM `0x0600C000..0x0600C060` | `0x60` | `201cd71270e85933f208ae434bb0a446d3e5b75471da7a120dcf349ef2779f31` | yes | **EXECUTABLE OVERLAY**: 90 bytes (77 in code) overwritten by font-setup tiles after boot staging; whole-image gate fails closed afterwards | 4 seeds ARM |
| 5 | VRAM `0x0600E000..0x0600ED88` | `0xD88` | `027930d0edc399cc93acce1a21e15be36e34a69b51ceb803686d2ad6c580fee0` | yes | IMMUTABLE AFTER LOAD (observed) | 32 seeds (31 ARM, 1 Thumb), 22 data ranges; `0x0600E474` **unseeded** |
| 6 | EWRAM `0x0203E000..0x0203E8E0` | `0x8E0` | `d3c8c872d123dea0cc39c304a959547257351eb2687d9c1c9b85d5d422938514` | yes | IMMUTABLE AFTER LOAD (observed) | 38 Thumb seeds (23 functions + 15 `bx rN` thunks at `0x0203E8A4`), 23 data ranges |

Images come only from the ROM via `scripts/extract-nes-emulator.py`. Function seeds and code/data runs live in the versioned canonical map `configs/nes-emulator-map-us.toml` (see *NES map provenance* below); `scripts/derive-nes-emulator-map.py` re-derives them from a **locally built** nested decomp ELF (agbcc + `arm-none-eabi-ld`) purely as a maintainer oracle. That ELF's sections equal the ROM-derived Parts byte for byte, except 46 bytes at/after `0x0203E772` in Part 6 (different libgcc thunk placement; the ROM layout is encoded explicitly in the script and checked against the extracted image). Nothing ROM-derived is committed: `configs/nes-emulator-map-us.toml` and `src/mzm_nes_emulator_map.h` hold addresses, modes and digests only.

### Part 1 self-modification (why the gate excludes two bytes)

`sub_06006000` (font setup, Part 1 asm) executes `strh r6,[0x06006700]` = `0x0182`: a BG screenblock-12 tilemap entry that lands on Part 1's own instruction `sub r5,r5,#32` (`0x06006700`). The straight-line init in `sub_06006558` runs that instruction *before* it calls font setup (the Part 1 re-entries at `0x06006890`/`0x060068D4`, transitions #4 and #7, are past `0x06006700` in that straight-line code and precede the write; this ordering is inferred from the transition log, not from an instruction trace), so the pristine AOT body is correct for its execution. The overwrite matters to the gate because a VBlank yield inside font setup re-enters at `0x06006098` and must still verify. The exclusion is exactly `[0x06006700,0x06006702)`; the neighbouring byte is gated (unit-tested). Residual risk: any later re-execution of `0x06006700` after the overwrite would run the pristine instruction; the emulator's main loop does not return to `sub_06006558`, and the audit pins that no other Part 1 byte changes.

### Resolver (`src/mzm_nes_emulator_resolver.h`, `src/mzm_ram_dispatch.cpp`)

1. Identify the Part containing `pc` (Part 2 only when a frontier observer is installed: its range aliases MZM's IWRAM code and must not pay a hash per call).
2. Verify identity: SHA-256 over the Part's gate runs (whole image minus proven mutable data; Part 2: code runs). Literal pools are gated because generated code bakes them in.
3. Invoke the exact private entry with `runtime_invoke_private_entry(pc, thumb)`; fail closed (no entry, no bytes match) by falling through to the remaining resolvers (Haze/Chozodia/payload keep their own gates) and, finally, a normal dispatch miss.
4. Counters are per Part and separate from the NES payload counters: attempts / verified / matches / verify_failures / invoke_failures / no_corpus, plus the first 64 cross-image transitions. `matches` is counted at entry (a native body may unwind through the observer with the dispatch in flight).

Lifecycle: **no permanent verified latch.** The bus has no write notification, so every entry re-hashes; a cached verdict could go stale. Cost is bounded by the small gated size (0.4 s total for the 12-frame NES-2 run); an upstream write epoch would allow caching. The pre-existing NES *payload* gate still latches after the entry check (NES-1b baseline, unchanged).

NES-1b isolation: `mzm_set_nes_payload_frontier_stop(true)` (used only by `mzm-nes-payload-frontier`) restores the old behaviour of stopping at exactly `0x06006558`; NES-2 leaves it off.

### Root cause of the handed-over "0x0600E1A0" blocker

The state left by the previous agent was three fixture problems stacked in front of the real frontier, none in the runtime:

1. **Unmapped SRAM (category C, fixture).** The test attached no save backing, so the emulator's SRAM probe at `0x0E007FB0..` read unmapped bytes. Fix: `bus.save().configure_sram(32 KiB)` (fresh cartridge, erased `0xFF`), as the production runner does.
2. **Wrong stall criterion (category A, scheduling boundary).** `0x0600E19E` is `swi #0x11` (LZ77UnCompWram) and the generator emits `runtime_swi(0x11); return;`, so each SWI returns to the *outer loop* with `PC=0x0600E1A0`. Part 5 loops over many SWIs, so "PC did not advance" was a false frontier. `runtime_should_yield()` was `false` there (verified in gdb), and `vblank_starts` and `halted()` were unchanged. Fix: stall means a bit-identical CPU state after a dispatch or a self-heal miss; halted guests are pumped exactly as `runtime.cpp step_once` does.
3. **Missing DMA wiring (fixture).** `bus.io().set_bus(&bus)` (done by `runtime.cpp:1935`) was absent from the NES-1b fixture, so its DMA transfers were inert and the images were never populated. NES-1b's PC/WAITCNT assertions still passed, but its images were empty; the agent's `set_bus` change was correct and is now in both fixtures.

Once those were fixed the real frontier appeared: `0x03000488` in Part 2. (A miss observed at `0x03001992` with the previous agent's partial corpus was a Part 2 address reached after an interpreter-bridged miss; not investigated further, Part 2 has no corpus.)

### Frontier (pinned by the test)

`target PC=0x03000488 Thumb part=part2 reason=part_has_no_corpus`, caller `sub_0600E1C4` (Part 5) via `bx r4`, `R4=0x03000489`, `LR=0x0600E224`, `CPSR=0x4000003F`, `SP=0x03007200`, `R7=0x06006700`, `R11=0x030029AC`; host return depth 0, IRQ depth 0; PPU frame 12 (`vblank_starts=12`), 28 top-level dispatches, 0 halt pumps. IRQ: handler `0x030057A8` is installed at `0x03007FFC`, `IE=0x2031`, `IME=1`, but no IRQ vector entry was observed before the frontier (no delivery yet). No visual or audio evidence exists.

Strict counters at the frontier: `dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0 self_heal=disabled`.

### Why Part 2 cannot be generated yet (GBARecomp gaps, not implemented)

Both are generic generator limitations in the pinned revision `e0c7cb2`; no GBARecomp file was changed.

**Gap B — private CFG aliasing public IWRAM entries (blocks Part 2 / NES-3).** Declaring Part 2 (`[[executable_image]]` at `0x03000000`) makes the finder abort: `[finder] private CFG conflicts with public entry at 0x030041EC` then `terminate ... std::runtime_error: ambiguous private CFG entry`. `0x030041EC` is MZM's own `gSoundCodeB` `[[code_copy]]` entry; Part 2 also aliases `gInterruptCode` `0x03000C7C`, sound code `0x03003B90/0x03004294`, clipdata `0x030016C4` and Chozodia `0x03001730`. With Part 2's data ranges declared, 68 further "control-flow entries into `[[data_range]]`" appear because `data_range` and `visited_` are keyed by address, not image. Expected: a private CFG rooted in one image resolves its internal PCs to that image and coexists with public entries at the same runtime address (the runtime already disambiguates with byte gates and separate private/public tables). Current: the aliasing is rejected. Smallest generic fix: key private CFG nodes and `data_range`/`extra_func` applicability by `(image id, pc, mode)` and drop the private-vs-public conflict check when the images differ. Synthetic test plan: two images at the same IWRAM address with different bodies (one public `code_copy`, one private image) plus distinct data ranges; assert distinct generated bodies, no cross-image direct call, both dispatch correctly by byte gate.

**Gap A — ARM `ldrne pc,[pc,Rn,lsl #2]` jump-table idiom (Part 5 `0x0600E474`).** `tst r0,#9; ldrb r1,[sp,#..]; ldrne pc,[pc,r1,lsl #2]; beq 0x0600EA58; .word 0x0600E49C,0x0600EC90,0x0600E670,0x0600E93C`. The finder starts an automatic function at the `beq` (`0x0600E4BC`) whose not-taken edge falls into the table bytes `0x0600E4C0..0x0600E4D0`: a hard `data_range` collision. Neither `exclude_func` nor a `[[jump_table]]` declaration suppresses it. The edge is unreachable (`ldrne` takes NE, `beq` takes EQ). Smallest fix: treat a conditional `ldr pc,[pc,...]`/`beq` pair as covering both edges, or accept an unreachable fall-through into a declared `jump_table`. Synthetic test: that exact instruction pair followed by a 4-entry table. Workaround kept: `0x0600E474` is not seeded (documented in the config), so the block is a visible strict miss rather than code generated over data.

### Validation

- CTest **57/57** (56 baseline + `mzm-nes-emulator-frontier`), including NES-1a, NES-1b, Haze/Chozodia resolvers and SRAM helpers; GBARecomp `e0c7cb2` **51/51**, clean; Python **9/9**; `git diff --check` clean.
- M4 harness: 01 PASS, 02 PASS, 03 PASS (317 frames, `final_pc=0x000001b4`, `FULLY_STATIC`, all counters 0).
- Regeneration is deterministic through `scripts/generate-m1.sh`, which now extracts the emulator images and expands the canonical map into `.local/mzm-us-nes-emulator.toml` (the committed `configs/mzm-us-nes-emulator.toml` was later removed, see below).

## NES-2b qualification (2026-09-29): Part 2 generated, emulator runs to the title screen (PASS)

The NES-2 frontier (`0x03000488`, `EmulatorAudio_Initialize`, Part 2) was blocked by generator gaps. After the upstream work below, Part 2 is declared like every other Part and the same test (`mzm-nes-emulator-frontier`, still starting at `0x087D8000`) now runs **600 frames natively, strict-static, with no frontier**:

`dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0 self_heal=disabled`, host return depth 0, final `PC=0x000001B4` (BIOS wait loop) between frames, `dispcnt=0x1860`.

### Upstream changes (GBARecomp `2c40fe8`, none in MZM)

| Change | Why |
|---|---|
| Image-scoped private CFG (`overlay = true` executable images; `image = "<id>"` on `[[extra_func]]`/`[[data_range]]`; function identity `(image, addr, mode)`) | Part 2 aliases MZM `code_copy` entries (`0x030041EC` etc.); the old finder aborted with `ambiguous private CFG entry`. Overlays may overlap each other and public entries. |
| `runtime_private_image_handle()` / `runtime_invoke_private_entry_in_image()` (fail-closed); unscoped `runtime_invoke_private_entry()` only reaches primary-world entries | The private table is now image-qualified, so an address-only invoke could be ambiguous. The MZM resolver passes the Part it just verified. |
| Conditional indirect transfer + complementary branch terminates the function (`ldrne pc,[pc,r1,lsl #2]` / `beq`) | Part 5 `0x0600E474` (Gap A); now seeded and generated. |
| LR continuation seeded for `ldr pc,[...]` indirect calls (`add lr,pc,#k ; ldr pc,[...]`) | Third gap found here: the 6502 core's memory/opcode callbacks return to continuations no BL reveals (first seen as `0x03004F70`). |

Backward compatibility was checked on the real corpus: regenerating the pre-existing MZM configuration with the new generator gives byte-identical function bodies and public dispatch table; the private table differs only by the added image column (2162 entries, all image 0). Yield/IRQ identity: no image is stored in runtime state; the outer loop re-dispatches the interior PC and the game's verified RAM hook re-selects the image from live bytes (synthetic test: yield at an interior PC and a synchronous IRQ, for two overlays at one address).

### MZM side

- All six Parts are `overlay = true` images with `image =` tags (`configs/nes-emulator-map-us.toml`, expanded at build time); Part 5 `0x0600E474` is seeded. Seeds are the decomp `FUNC` symbols **plus** indirect-transfer targets discovered from pointer words in the image's data runs and validated against decomp symbols (needed for the 6502 opcode handlers, which are local labels: 388 seeds in total across the six Parts).
- Part 2 gate: whole image minus `.data` (`0x03002330..0x03002DF0`, linker map) and `sUnk_03005808` (`0x03005808..0x0300580C`, the emulator state pointer read by the IRQ handler). Observed dirty bytes in Part 2 during 600 frames: 31, none in code runs; the gate never failed.
- Resolver: prefilter (SHA of the first 32 gated bytes) plus, after a successful full SHA, a verified-copy `memcmp` fast path (bytes identical to an already-verified image); any difference falls back to the full SHA. Still no permanent latch. Without this, 5.9 M entries were too slow to test.

### Result (600 frames)

| Part | Verified native entries | Verify / invoke failures | Mutability observed |
|---|---:|---|---|
| 1 | 4 | 0 / 0 | overwritten by graphics after init (2,516 dirty bytes, 1,048 in code runs); no entry after that |
| 2 | 5,901,006 | 0 / 0 | 31 data bytes, none in code |
| 3 | 3,136 | 0 / 0 | immutable |
| 4 | 2 | 0 / 0 | overlay (boot staging overwritten by font tiles) |
| 5 | 4,151 | 0 / 0 | immutable |
| 6 | 4,117 | 0 / 0 | immutable |

IRQ: `sub_030057A8` installed at `0x03007FFC`, `IE=0x2031`, `IME=1`; **2,603** IRQ vector entries observed, handler executed natively. `halt_pumps=63,033`.

Frame evidence: the last latched frame (fnv1a64 `0xCB0431A65E6BD988`) is the NES Metroid title screen: `METROID`, `PRESS START`, `(C)1986-2004 NINTENDO`, starfield and planet surface (inspected visually from the PPM the test writes with `MZM_NES_FRAME_DUMP`; no pixel comparison against a reference emulator, no input, no audio check).

### Validation

CTest **61/61** (57 previous + 4 new upstream tests in the integrated suite); GBARecomp integration **55/55**, worktree branch 55/55; Python **9/9**; M4 01/02/03 PASS (317 frames, `final_pc=0x000001b4`, `FULLY_STATIC`); NES-1a/NES-1b unchanged and passing. The previous "NES-2" frontier text above is kept as history.

## NES map provenance and reproducible build (2026-09-29, R1-R7)

### R1 audit: where each datum came from

| Datum | Class | Notes |
|---|---|---|
| Image id, load address, size | B/C | decomp linker sections (`.vram_text_N`, `.iwram_text`, `.ewram_text`); equal to the ROM DMA/LZ77 sizes |
| Image SHA-256 | A | of the bytes extracted from the legal ROM; hash-checked by `gba_recompile`, the generator and the runtime gate |
| Code/data runs, ARM vs Thumb | C | ELF mapping symbols (`$a`/`$t`/`$d`); Part 6 tail (`0x0203E888..0x0203E8E0`) is overridden from the ROM because the local toolchain lays it out differently (46 bytes) |
| Function seeds (FUNC symbols) | C | ELF symbol table, kept only if inside a code run |
| Indirect targets (opcode table, literal pools) | C, then A | pointer words scanned in ROM-extracted data runs, accepted only if they hit an ELF symbol of matching mode; 388 seeds total |
| Data ranges in the gba_recompile config | C | complement of code runs |
| Mutable ranges (`0x06006700`, `0x03002330..0x03002DF0`, `0x03005808`) | B/D | linker map `.data` (B) and runtime mutability audit (D), not ELF-derived |
| Unclassified `0x0600C000..0x0600C00C` (Part 4) | C | no mapping symbol; neither code nor data, preserved as derived |
| Image boundaries at run time | D | resolver + frontier test |

Only class A is derivable from the ROM alone; nothing else can be, without a disassembler heuristic. So the structure is versioned rather than re-derived on every build.

### Canonical map

`configs/nes-emulator-map-us.toml` (schema 1, ~570 lines): per `[[image]]` the id, load address, size, sha256, a complete run tiling `[start, end, arm|thumb|data]`, `seeds_arm` / `seeds_thumb`, and `[[image.mutable]]` ranges with notes. It contains no code bytes, literal pools, payload, `part*.bin`, ELF or other ROM-derived blob, and no absolute paths.

```
canonical map  (configs/nes-emulator-map-us.toml)
     ^ verified against (maintainer only)
nested decomp ELF  (symbol/structure oracle)
     ^ reconstructed from
legal MZM ROM / matching decomp source
```

The ELF is an oracle for structure and symbols, not an absolute authority; the ROM remains the authority for every executed byte (Part hashes are checked at generation and by the runtime byte gate).

### Normal build (no nested ELF)

`scripts/generate-m1.sh`: verify ROM SHA-1 -> `extract-nes-payload.py` -> `extract-nes-emulator.py` -> `generate-nes-emulator-config.py` (expands the map into `.local/mzm-us-nes-emulator.toml`, verifies Part hashes, and fails if the committed `src/mzm_nes_emulator_map.h` is stale) -> `gba_recompile`. The old committed `configs/mzm-us-nes-emulator.toml` is gone (generated, never edited). While doing this, a clean tree was found not to extract the NES payload at all (it relied on a stale `.local/nes-payload-usa.bin`); that step is now in the script.

### Maintainer verification

```
scripts/derive-nes-emulator-map.py --elf /path/to/nested/mzm_us.elf \
    --compare configs/nes-emulator-map-us.toml
scripts/derive-nes-emulator-map.py --elf ... --emit-candidate .local/nes-map-candidate.toml
```

`--compare` fails on any divergence in geometry, hashes, runs or seeds (mutable ranges are runtime evidence and not compared). The canonical map is never overwritten unless `--emit-candidate` is pointed at it. Python tests (`tests/m4/test_nes_map.py`) check tiling, seed/mode consistency, no-leak, deterministic expansion and header freshness.

### Result

- Oracle compare vs the local nested ELF: PASS (6 images, 374 runs, 388 seeds).
- Clean tree (no `.local`, no ELF, no arm-none-eabi in PATH): `generate-m1.sh` PASS twice from scratch; the two corpora are byte-identical to each other and to the pre-change corpus (tree hash `e1741bf6...`); no `/home`, `.local`, `.elf` or scratch path appears in generated output.
- Full build + CTest 61/61; Python 14/14 (9 + 5 new).

## NES-3 behavioural qualification (2026-09-29): soak, input, gameplay (PARTIAL)

Status: **PARTIAL.** Soak, input and first gameplay pass strict-static; a first behavioural frontier was captured (Part 1 resident handlers, frame 3068); audio is silent because of a new generic GBARecomp gap. GBARecomp stays at `2c40fe8`; no GBARecomp or production-runtime file was changed. The harness is `tests/m4/nes_behavior_test.cpp` (CMake target `mzm_nes_behavior_test`), driven by `scripts/run-nes-behavior.py` (CTest `mzm-nes-behavior`, ~2 min; `--mode soak` is manual, ~7 min).

### Input route

The test injects input with `bus.io().set_keyinput(active-low mask)`, the call `runtime.cpp` uses for host polling (line ~4022) and for `GBARECOMP_INPUT_REPLAY`, once at a frame boundary. It composes with the synthesised mask into `REG_KEYINPUT` (`0x04000130`, active low; A=bit0, B=1, SELECT=2, START=3, RIGHT=4, LEFT=5, UP=6, DOWN=7). Guest memory is never written. Script format: `MZM_NES_INPUT="frame:KEY[+KEY]:hold,..."`.

### N1/N2 soak (no input)

Two 10,000-frame runs. Every reported line was identical between runs (checkpoints every 1,000 frames, hashes, audio hash). Strict counters `dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0`, host return depth 0, IRQ depth 0 (max 0), resolver failures 0, no frontier. IRQ vector entries 44,235 (2,603 at frame 600, as NES-2b). Title frame hashes (fnv1a64 of the latched frame):

| Frame | Hash | Content |
|---:|---|---|
| 600 | `CB0431A65E6BD988` | title (same as NES-2b) |
| 1200 | `ED2F86A12FF98046` | intro scene (planet, no text) |
| 3000 | `C78BDE19BAE7E475` | intro scene |
| 6000 | `D9CAEFDA86152DD6` | title again |
| 10000 | `569682311AAA43C8` | (final line) |

Without input the game cycles title <-> attract scene by itself: hashes are not constant by design (palette fade/animation, 693 distinct frames sampled every 10 frames) and are neither black nor frozen.

### N3-N6 START, post-title state, input effect

`START` at frame 700 (hold 8): the frame changes from the title (`E4A220128A80B933`) to the START/CONTINUE menu (`16E0720AC211F04B`, 4 colours, stable from frame 720). `SELECT` (hold 8) toggles the cursor (`59D671E2FA30C813`) and a second `SELECT` returns to the menu hash. A second `START` at frame 1000 enters gameplay: the first Brinstar room (`EN..30` HUD, Samus, enemies). In gameplay the state is stable until ~frame 1500 (intro jingle), then evolves; `RIGHT` held from frame 1650 scrolls the room and moves Samus, `A` makes Samus jump, and Samus takes enemy damage (`EN..30` -> `EN..22`). Frames at 1700/1760/1830 differ from the no-input control. Frames were inspected visually from PPM dumps (`MZM_NES_FRAME_DUMP_DIR`); no external pixel oracle. The menu is toggled by SELECT, as in the NES original, not by the D-pad.

### N4 first behavioural frontier (frame 3068) — superseded by NES-3b

> **Superseded.** NES-3b (below) fixed this false rejection; `0x06006E08` now runs natively. Kept as history.

Scripted play session (RIGHT/A/B/LEFT/DOWN/UP cycles from frame 1700, a START pause at 3010): at **frame 3068** `dispatch_misses=1`, `interpreted_insns=2478`, Part 1 `verify_fail=1`. The recorded miss is **Part 1 `0x06006E08` ARM** (`tst r0,#0x80` ..., a PPU-register handler), entered from Part 2's IRQ path. Determinism: two runs reproduce it identically.

Cause (measured): Part 1 is two different things. Its first `0xE00` bytes (`0x06006000..0x06006E00`, init code) are overwritten by graphics from frame ~13 on (2,460 dirty bytes at the stop, buckets `000..D00` all dirty), and the earlier "no entry after that" observation held only for that half. The second half (`0x06006E00..0x06007240`, buckets `E00..1200` = 0 dirty bytes) holds resident PPU handlers that the running game re-enters. The runtime byte gate covers the whole image, so the first legitimate entry into the resident half after the init half was overwritten fails closed. This is a gate/map design issue on the MZM side (split Part 1 into init and resident verification scopes with their own literal pools), not evidence of bad code. It was not fixed here because a gate change alters the security-relevant verification policy and needs its own review (proposed milestone **NES-3b**). CTest pins the frontier (frame, PC, miss, verify failure, dirty buckets) so that a fix must update the pin deliberately.

### N7 audio audit: PARTIAL

Internal evidence over the title and gameplay runs: `EmulatorAudio_Initialize` and `SetupOutput` once; `EmulatorAudio_WriteToApu` (`0x03000408`) 4,108 calls in 1,300 frames writing real APU registers (`$4011,$4015,$4017`, then pulse/triangle/noise `$4000..$400F`); `Timer1Callback` and `ProcessApuAndMixBuffers` about every 2 frames; the guest IWRAM mix buffer holds non-zero samples (139/768 bytes). DMA1 is programmed `CNT=0xB2000004` with `DAD=0x040000A0` (FIFO A) and a moving `SAD` (`0x03005DD0`/`0x03005F10`), `SOUNDCNT_H=0x0304` (FIFO A to both sides, timer 1), `SOUNDCNT_X=0x0080`, timer 1 cascaded with IRQ. Host side: `dma1_runs=0`, FIFO A `count=w=r=0`, and the capture ring is all zeros for 10.9 M samples.

Root cause candidate, confirmed by a diagnostic: `GBARecomp/src/gba/gba_io.cpp:400` (`run_sound_fifo_dma`) returns unless DMA `CNT_H` bit 10 (32-bit) is set. On hardware, FIFO-mode DMA is always 32-bit and ignores that bit; the NES emulator programs `0xB200`. Forcing bit 10 from the test harness (`MZM_NES_DIAG_FORCE_FIFO32=1`, diagnostic only, never used for qualification) gives 8,577 DMA runs, 34,308 words and 240,586 non-zero mixed samples, still strict-clean. This is a **new generic GBARecomp gap**; per the campaign rule upstream was not edited. Audio stays PARTIAL until that is fixed and an audio oracle exists.

### N8 SRAM / password

Observed only, nothing forced: `EmulatorLoadFromSram` (`0x0203E414`) runs twice at boot; `EmulatorRetrieveGameOverPassword` (`0x0203E000`) and `EmulatorFillPasswordWithSaved` (`0x0203E118`) run about once per frame; `EmulatorSaveToSram`, `SaveToPasswordBytes` and `LoadFromPasswordBytes` are never called; SRAM is never written (snapshot compared every 30 frames, dirty flag clear) across the soak and the start/play sessions. Save/password stays UNVERIFIED and needs its own milestone.

### N9 quit, N10 strict

Quit was not attempted (no natural route yet; internal functions were not called). Strict counters were zero in every run up to the frontier; the frontier run stops at the first non-zero, as required.

### Validation

See the matrix (`docs/M4-COMPATIBILITY-MATRIX.md`, recomputed from the table).

## NES-3b Part 1 verification scopes (2026-09-29): PASS, new frontier captured

Status: **PASS** for the stated goal (the `0x06006E08` false rejection is gone without weakening the gate). The scripted session then reaches a **new, different frontier** at frame 3111 (stack-resident SRAM routine, below), so real gameplay, save/password and audio all stay **PARTIAL/BLOCKED**. GBARecomp stays at `2c40fe8539c566ce2aee7dce8a722917a6cf475c`, unchanged; nothing was pushed or merged.

### Part 1 lifecycle (measured, not assumed)

`MZM_NES_WATCH_IMAGE_WRITES=1` (read-only bus write observer in `tests/m4/nes_behavior_test.cpp`) records every store that changes a Part 1 byte over the 3,111-frame session:

- Changed range: `0x06006000..0x06006DBF` inclusive (54 of 55 chunks of `0x40`, all halfword stores). Nothing at or above `0x06006DC0` is ever stored to; the resident half `0x06006E00..0x06007240` has 0 dirty bytes.
- First frame 12 (chunks first touched at frames 12..18); repeated ~244,000 changing stores (sum over writers): it is tile/tilemap upload, not a one-shot.
- Writers: Part 2 PPU-upload code (`0x03003420`, `0x03005438`, `0x03005464`, `0x03005474`, `0x03005678`, `0x0300568C`, `0x03005754`, `0x03005758`) plus the known single font-setup `strh` from Part 1 itself (`0x0600607C` -> `0x06006700`).
- Boundary: `0x06006E00` is the literal pool of `sub_06006E08` and `0x0600713C` / `0x06007210` are function symbols (`--compare` proves each internal boundary is an ELF symbol; it cannot prove lifecycle, which is runtime evidence).

### Canonical map schema

`configs/nes-emulator-map-us.toml` gains `[[image.scope]]` (`id`, `ranges`, `requires`, `lifecycle`, `note`) and one more `[[image.mutable]]`. Part 1 stays ONE image (`nes_part1`, one `part1.bin`, one private domain); the scopes never reach the generated GBARecomp config (`.local/mzm-us-nes-emulator.toml` is byte-identical before and after, tested).

| scope | ranges | requires | lifecycle |
|---|---|---|---|
| init | `0x06006000..0x06006E00` | resident | overwritten-after-init |
| resident | `0x06006E00..0x0600713C`, `0x06007210..0x06007240` | none | resident |
| menu | `0x0600713C..0x06007210` | init, resident | overwritten-after-init |

The third scope is a finding: `sub_0600713C` (the emulator menu/error screen) tail-branches into init code (`0x06006880/0x06006968`) and its literals sit in resident, so it inherits the init lifecycle. `requires` lists the *direct* static transfers (`b/bl`, pc-relative literal and table references) that leave a scope, because generated code follows them without going through the resolver; the runtime verifies the transitive closure. `tests/m4/test_nes_map.py` derives those edges from the extracted bytes and fails if one is undeclared (`init -> resident` `0x0600691C -> 0x06006E08`, `menu -> init` `0x06007208 -> 0x06006880`, resident has none).

### Verification policy

PC -> owning scope -> verify that scope and its `requires` closure (prefilter SHA-256 of the first 32 gated bytes, then the full gate SHA-256, then a verified-copy snapshot fast path per scope) -> invoke the exact private entry. There is no "Part verified once" latch. After init is overwritten: resident PCs run if the resident bytes match; init and menu PCs fail closed. Literal pools are inside the gated ranges (generated code bakes them in); nothing is excluded for proximity. `mzm_nes_emulator_scope_stats()` adds per-scope `attempts/verified/matches/verify_failures/dependency_failures`.

Security tests (`mzm-nes-scope-policy`, `tests/m4/nes_scope_policy_test.cpp`, no ROM run): fresh init/resident/menu entries valid; init byte changed -> resident valid, init and menu rejected; last init byte `0x06006DFF` only affects init and first resident byte `0x06006E00` is required by init; resident instruction or any of four resident literals changed -> resident rejected; the `0x06006700` exclusion still holds and the byte after it is still gated; a Thumb entry at an ARM PC is not run and not counted; Parts 2-6 unaffected by Part 1 mutation and still reject their own flipped byte; restoring a byte re-admits the entry (no latch); production hook stats attribute the failure to the right scope.

### Old frontier

`0x06006E08` ARM at frame 3068: before `verify_fail=1`, `interpreted_insns=2478`; now the entry is a verified native private invoke and the run continues (Part 1: 8 attempts, 8 verified, 8 matches, `verify_fail=0`, `interpreted_insns=0` up to the new frontier).

### Regression found and fixed on the way: native stack depth

The first NES-3b build of `mzm_ram_dispatch` segfaulted (stack overflow) before frame 20 and also broke NES-2b. The generated Part 2 code recurses natively (`0x030056E8 -> 0x03005754 -> 0x03005794 -> ...`) through this hook thousands of frames deep, so the hook's own frame size is part of the budget: with `ulimit -s unlimited` it passed, with 8 MB it overflowed. The verification (lambda, byte snapshots) now lives in a `noinline` helper and the hook frame is `0x88` bytes. With the default 8 MB stack the runs pass; **4 MB overflows**. Any future change to `mzm_ram_dispatch` must keep its frame small; this is a latent limit, not a fix of the recursion.

### Part 6 mutable password buffer (frame 3068)

The first continued run then failed Part 6's gate at `0x0203E390` Thumb (`failed_scope=part6`): four bytes changed, `0x0203E448/449/44C/44D` (`0x80,0x02,0xC0,0x42`), written once at frame 3068 by `0x0203E31C`. They lie in `sPasswordBytes[18]` (`nes_metroid/emulator/src/part6.c`, first object of `part6.o .data`, zero-initialised, filled by `MemoryCopy`), i.e. exactly the 18-byte zero run `0x0203E43C..0x0203E44E` of the image, bounded by non-zero constants. That range (and only it) is now `[[image.mutable]]`; adjacent bytes and all code stay gated (unit-tested). It is the same class as Part 2's `.data`. Evidence is decomp source + extracted image + observed writer; the maintainer ELF oracle could not be re-run (the nested ELF is not on this machine and rebuilding it from `nes_metroid/emulator` failed at link, so `--compare` was not re-executed after this change; it does not compare mutable ranges).

### New first frontier (frame 3111) — superseded by NES-3c

> **Superseded.** NES-3c (below) runs this helper natively; the session now continues to the emulator quit and stops at `0x080006CA`. Kept as history.

Scripted session (`700:START, 1000:START`, then RIGHT / RIGHT+A / B / LEFT / A / DOWN / UP cycles every 240 frames from 1700). Samus dies, the game shows GAME OVER, then PASSWORD and `SAVE YOUR PROGRESS TO THE MEMORY? YES / NO` (inspected by eye), and the scripted `A` selects YES.

| field | value |
|---|---|
| PC / mode | `0x03827110` Thumb (mirror of IWRAM `0x03007110`, the stack) |
| Part | none: the code is not in any Part image; bytes are a copy of Part 6 `0x0203E7BC` (`SramCheckInternal`, first `0x20` bytes equal) |
| Caller | Part 6 `_call_via_r3` `0x0203E8B0` (`bx r3`), from `SramCheck` (`nes_metroid/emulator/src/sram/sram.c`), which copies the routine into `u16 code[0x60]` on the stack and calls `code + 1` |
| CPU at the miss | `r0=0x0201C010 r1=0x0E007FE8 r2=0x18 r3=0x03827111 r4=0x0201C010 r5=0x0E007FE8 r6=0x18 r7=0 r8=0x4E42 r9=0 r10=0xC399 r11=0x0201C028 r12=0x0203E3DD sp=0x03827110 lr=0x0203E849 pc=0x03827110 cpsr=0x3F` (Thumb, system mode) |
| Counters | 1 miss, 384 interpreted instructions, `unmapped=0`, `io_unhandled=0`, resolver failures 0, IRQ depth 0 |

Observed by gdb on `runtime_bridge_interpret` (the test's stall check runs after the top-level dispatch returns, so its own `stop_cpu` line is the later stall state, PC `0x0600ED74`). The routine runs in the interpreter for that one call and the wait loop continues; the run is stopped at the stall as designed. A variant that sent `SELECT` then `A` at the prompt (intended as NO; the cursor state was not checked) reached the same miss at frame 3099, so the path is not specific to the original script's `A`. This is the emulator's SRAM save/check path (`sram.c`); modelling code copied to a runtime stack address is a separate design (the address depends on `sp`) and is **not** attempted here.

### Continued gameplay / limits

Gameplay runs natively from frame ~1000 to 3111 (~2,100 frames, strict counters zero); only 43 of those frames lie beyond the old frontier at 3068, because the new frontier arrives right after. The requested 10,000 post-gameplay frames were **not** reached because a genuine new frontier stops the session first, and the rule is to stop at the first miss. Inputs are the START,START,RIGHT,A pattern plus B/LEFT/UP/DOWN cycles; no TAS. Real gameplay, Save/password and Audio stay PARTIAL; audio remains blocked by the GBARecomp FIFO DMA gap (not touched, not a NES-3b criterion).

### Determinism

Two concurrent 14,000-frame-limit runs (both stop at the frontier) produced byte-identical stdout (217 lines) and stderr: same stall frame, frame hashes at 1300/3000/3068/3100, strict counters, IRQ counts (`irq_vec=irq_handler=13724` at the stop), per-Part resolver stats and the four Part 6 write records.

### Validation

CTest 63/63 (NES-1a, NES-1b, NES-2b, scope policy, behavior); Python 19/19 (unittest over `tests/m4`); M4 harness 01/02/03 PASS; soak (below) PASS; `git diff --check` PASS; GBARecomp `mzm/mzm-integration` untouched at `2c40fe8`.

## NES-3c runtime-copied SRAM helper (2026-09-29): PASS, new frontier captured (superseded past the ROM restart by M4-RESUME-1)

Status: **PASS** for the stated goal. The stack-copied `SramCheckInternal` at `0x03827110` now runs natively (no interpreter, no self-heal, no executable-stack wildcard), the save completes, the emulator quits and the ROM restarts. The session then stops at a **different** first miss, MZM ROM `0x080006CA` (below). Save/password stays PARTIAL, Audio stays PARTIAL/BLOCKED (not touched). GBARecomp unchanged at `2c40fe8539c566ce2aee7dce8a722917a6cf475c`; nothing pushed or merged.

### Source helpers (audited, not assumed)

The NES emulator's `sram.c` (`nes_metroid/emulator/src/sram/sram.c`) copies `[XInternal, X)` into a `u16 code[]` on the stack and calls `code + 1` through `_call_via_r3` (Part 6 `0x0203E8B0`, `bx r3`).

| helper | source in Part 6 | mode | size | SHA-256 | identical to |
|---|---|---|---|---|---|
| `SramCheckInternal` | `[0x0203E7BC, 0x0203E7EC)` | Thumb | `0x30` | `27fc0ae48fc8b1d2a213b08ab67ca56f7c4bb96fbb84279bb9238c4526782fb8` | MZM ROM `0x0800529C` |
| `SramWriteUncheckedInternal` | `[0x0203E6F4, 0x0203E718)` | Thumb | `0x24` | `1818db03109c2c0fbbd414c8303ad6ef4cecc64cf6029759023f6ba48e6ff8d3` | MZM ROM `0x080051D4` |

Copy size is computed by the code, not read from a register: `csize = ((SramCheck|1) - (SramCheckInternal|1)) << 15 >> 16` halfwords = `0x18` halfwords = `0x30` bytes (literals at `0x0203E820/824`). The `r2 = 0x18` seen at the miss is a different value that happens to be equal: it is SramCheck's *size argument* (`EmulatorSaveToSram` checks `0x18` bytes at SRAM `0x0E007FE8`, then `0x10` at `0x0E007FD8`). The last two bytes of the `0x30` are zero padding and are part of the verified image. No bytes are committed; `part6.bin` is derived from the legal ROM by the existing pipeline and the identity is a test (`mzm-nes-stack-helper`, `test_nes_map.py`).

### Runtime copy: destination is not a constant

Destination = `sp` after SramCheck's `sub sp,#192`, i.e. `S - frames`, where `S` is the emulator's stack alias. Measured / derived:

| path | frames below `S` | `S` | destination |
|---|---|---|---|
| `EmulatorSaveToSram` -> `SramWriteChecked` -> `SramCheck` (observed, save YES) | `12 + 20 + 16 + 192 = 0xF0` | `0x03827200` | **`0x03827110`** |
| `sub_0203E3AC` -> `SramWriteChecked` -> `SramCheck` (derived, not reached) | `8 + 20 + 208 = 0xEC` | `0x03827200` | `0x03827114` |
| `EmulatorLoadFromSram` -> `SramWriteUnchecked` at boot (observed) | `12 + 16 + 128 = 0x9C` | `0x03007200` | **`0x03007164`** |

So the destination depends on the call path AND on which alias of the same physical stack the emulator is using (`0x03007200` at boot, `0x03827200` after). Compiling only `0x03827110` would not be a general solution and was not done. Stability: two identical 14,000-frame-limit runs give byte-identical output; the destinations above repeat exactly.

### IWRAM mirror semantics (checked in the bus)

IWRAM is `0x8000` bytes mirrored through `0x03000000..0x03FFFFFF`; `0x03827110` -> physical offset `0x7110` (= `0x03007110`). The bus reads/writes through the mirror (the live bytes at `0x03827110` were read directly). `runtime_dispatch` does **not** canonicalise: `pc = target & ~1` is the raw logical PC and only `[0x02000000,0x04000000)` reaches the RAM hook. Consequently the guest PC/LR/`sp` stay the logical alias (`sp=0x03827110`, `lr=0x0203E849`), and only the *window test* uses the physical offset.

### Position dependence

Full inventory of the `0x30` bytes (`objdump -Mforce-thumb`): `push {r4,r5,lr}`, register moves/`subs`/`negs`, one byte loop (`ldrb` x2, `adds`, `cmp`, `beq`/`bne`), `pop {r4,r5}; pop {r1}; bx r1`, padding. No pc-relative `ldr`, no ADR, no `add/mov ... pc`, no BL/BLX, no literal pool; the four branches land inside the copy; it returns through the popped LR; it only *reads* SRAM/EWRAM (no stores). `SramWriteUncheckedInternal` is the same shape (stores to its destination argument). `tests/m4/test_nes_map.py::test_copied_sram_helpers_are_position_independent_and_sized_by_symbols` decodes both and fails on any pc-relative/BL/escaping branch, and derives the copy sizes from the map's function seeds. The *callers* (`SramCheck`, `SramWriteUnchecked`) are position-dependent (five pc-relative loads, a BL) and are ordinary native Part 6 code at `0x0203E7EC` / `0x0203E718`, untouched.

### Chosen architecture

Reuse the mechanism MZM already has for its own identical helpers (`kStackHelpers` in `src/mzm_ram_dispatch.cpp`): the live bytes at the entry PC are compared with the exact ROM source (`0x0800529C`, `0x080051D4`) on **every** entry and, if equal, the already generated native translation of that ROM function runs. That is sound because the body is position-independent, and it is exactly what a destination *family* needs. Alternatives and why not:

- A `source_addr` private relocation and C (an `[[executable_image]]` overlay at the destination): one generated body per destination, but the destination set is path- and alias-dependent (table above) and only partly reachable; each new path would need a new image plus a new corpus. Also no NES-specific code is involved: the bytes are MZM's own.
- B `code_copy`: fixed-address ROM->RAM copies, not stack-relative.
- D a new mechanism / GBARecomp change: not needed, so none was made.

The one real defect was the resolver's destination model. It accepted a raw PC in `[0x03007000, 0x03007E60)` and no other; the NES emulator later uses the alias `0x0382xxxx`. It now tests the **physical** offset (`pc & 0x7FFF` in `[0x7000, 0x7E60)`, region `0x03`, copy must not cross the page), requires a **Thumb** entry (it did not check the mode before), and keeps the per-entry byte gate. Everything else (source bytes, exclusion of fixed IWRAM copies below `0x7000`, no publication) is unchanged.

### Byte gate and tests

Policy: PC in window + Thumb + live bytes == ROM source, checked at every entry (no latch: the stack is reused). `mzm-nes-stack-helper` (`tests/m4/nes_stack_helper_test.cpp`, needs the ROM, no game run) covers, for four destinations (`0x03827110`, `0x03827114`, `0x03007164`, `0x03A07188`): exact bytes run and the copied body computes the right result (`r0 == 0` for equal buffers, `&dest[9]` for a differing byte); return goes to LR and no ROM source PC leaks; alias matches are counted as mirror matches; the helper is not in ordinary dispatch (`runtime_has_static_entry == 0`); all 48 one-byte mutations are rejected; restoring re-admits (no latch); wrong mode, truncated copy (`0x20` of `0x30`), bytes below the window, bytes in EWRAM and bytes shifted by one halfword are rejected; `SramWriteUncheckedInternal` copies correctly at an alias and at the direct address. Zero-padding note: a truncated copy whose missing tail is already zero would equal the padding and be accepted; that is identity by bytes, and the test uses a non-zero stale tail.

### Private corpus

No corpus change, no new root or resume: the bodies are MZM's existing public ROM functions `gf_SramCheckInternal` / `gf_SramWriteUncheckedInternal`, reachable from the stack only through the byte-verifying hook. An IRQ inside the body resumes in the ROM translation (its PCs are ROM addresses), which is equivalent because the body never reads its own address; it does not re-enter the stack bytes.

### Old frontier and SRAM behaviour

`0x03827110` no longer misses: two verified entries (`mirror_matches=2`, `stack_helper_rejects=0`). `SramCheckInternal(src=0x0201C010, dst=0x0E007FE8, n=0x18)` and `(0x0201C000, 0x0E007FD8, 0x10)` both return `r0 = 0` (SRAM equals what was just written). Before them the native Part 6 `SramWrite` stored `SRAM [0x7FD8,0x8000)` (40 bytes; first sampled at frame 3120; `dirty=1`). `EmulatorSaveToSram` ran once. Boot also runs `SramWriteUncheckedInternal` twice at `0x03007164` (`LoadFromSram`, SRAM `0x7FB0` and `0x7FD8` -> EWRAM `0x0201C000`, erased). So: **SramCheck PASS**, save write and verify observed; a save/load round trip (second boot reading the saved data) and the password screen contents are not verified, so Save/password stays PARTIAL.

### Continued execution and the new first frontier

The chain ran to completion: start -> Brinstar -> death -> GAME OVER -> PASSWORD -> save YES -> `SaveToSram` -> emulator exit (`0x0600ED28`) -> loader reset stub `0x087D813E` -> ROM restart. The first miss is then:

| field | value |
|---|---|
| frame | 3245 (14,000-frame-limit script) / 3205 (CTest script, extra START at 3010) |
| PC / mode | `0x080006CA` Thumb |
| owner | MZM ROM `InitializeGame` interior (`ldr r1,[r0,#8]`), public corpus; not a Part |
| caller | `lr = 0x08000243` at the miss |
| CPU at the miss | `sp = 0x03007E44`, `cpsr = 0x3F` (Thumb, system) |
| CPU at the stall | `r0=0x05001F80 r1=0x85001F80 r2=0x03007E44 r13=0x03007E44 r14=0x080006E3 r15=0x080009A0 cpsr=0x3F` |
| counters | 1 miss, 12 interpreted instructions, `unmapped=0`, `io_unhandled=0`, resolver failures 0, host return depth 0, IRQ depth 0 |

This is the known interior-PC vblank-yield artifact already recorded in the compatibility matrix (`InitializeGame` interior PC absent from the dispatch table), now reached from the other side (after the NES quit). It is generic and not NES-specific and was not addressed. Before it: ~2,100 gameplay frames + the save + the exit ran with `dispatch_misses=0`, `interpreted_insns=0`, `unmapped=0`, `io_unhandled=0`, self-heal disabled. The old frontier's frame number is not comparable: the request to run 10,000 frames after gameplay start ends at the quit because the NES session ends there.

Diagnostic (not a qualification): with `GBARECOMP_YIELD_ON_VBLANK=0` the run segfaults, because the NES emulator's native recursion relies on the vblank yield to unwind.

### Host stack audit (separate risk, not fixed)

Measured with the hook's lowest frame address (`mzm_ram_dispatch_stack_low`, `NES3 host_stack` line): the native recursion through `mzm_ram_dispatch` reaches **5,711,952 bytes within 20 frames and 5,768,000 bytes from frame 300**, then stays flat to the end (frames 300/1200/2500/3245 identical, hook depth 5,768,000). Limit sweep at 300 frames: 8192 KB pass, 6144 KB pass, 5888 KB pass, **5632 KB segfault**. Default `ulimit -s` is 8 MB, so the margin is about 2.4 MB (~29%). The hook frame is `0x88` bytes (unchanged by NES-3c; the helper runs out of line). The recursion is Part 2's (`0x030056E8 -> 0x03005754 -> 0x03005794`, ~35,000 generated frames), reached at boot, and it is bounded by the vblank yield. The margin is **narrow and compiler-/flag-dependent**: any frame growth on that path (as happened in NES-3b) can cross it. Not fixed here (no `ulimit` change, no refactor). Recommended separate milestone **HOST-STACK**: iterative dispatch or a bounded/guarded native depth for the Part 2 recursion, with a CTest that runs the soak at a reduced stack limit.

### Validation

CTest 64/64 (new: `mzm-nes-stack-helper`); Python 20/20; M4 01/02/03 PASS; NES-1a/1b/2b/3b PASS; scope policy PASS; soak PASS (2 x 10,000 frames identical); `git diff --check` PASS; GBARecomp `mzm/mzm-integration` untouched at `2c40fe8`.

## M4-RESUME-1 VBlank interior resume after the NES quit (2026-09-30): PASS, new frontier captured

Status: **PASS** for the stated goal: `0x080006CA` Thumb is resolved as a static resume of the function that contains it, without interpreter, self-heal, disabling the VBlank yield, per-halfword entries or a duplicated function. The chain then runs through more of MZM's boot and stops at the next miss of the **same class** in a different function (`InitializeAudio` `0x0800271C`); the choice of how to cover the rest of the ROM is left open (below). GBARecomp is unchanged (`2c40fe8539c566ce2aee7dce8a722917a6cf475c`); nothing pushed or merged.

### The old frontier, exactly

`mzm_ram_dispatch`/generated code check `runtime_should_yield()` at every instruction prologue. It yields (unwinds to the outer loop) at the first prologue after a VBlank start when the CPU is in User/System mode and IRQ depth is 0. Before the miss (gdb on `runtime_dispatch_miss`): frame 3245, `r0 = 0x040000D4` (DMA3), `r1 = 0x85010000`, `r15 = 0x080006CA`, `lr = 0x08000243`, `cpsr = 0x3F`, `g_runtime_vblank_starts = 3245`, call-return depth 0, IRQ depth 0. `InitializeGame` is Thumb `[0x080006A0, 0x080007C4)`; `0x080006C8` is `str r1,[r0,#8]`, the DMA3 start that clears EWRAM (`0x10000` words, more than one frame of cycles), so the VBlank starts inside it and the next prologue (`0x080006CA`, `ldr r1,[r0,#8]`) yields. The instruction after the IWRAM clear (`0x080006DC`) is the same case. It is not an arbitrary PC: any instruction that follows a VBlank-crossing one can be the resume PC.

### Why a resume alias is valid here

Every generated instruction block starts with `R15 = pc; if (runtime_should_yield()) return;` and only uses locals inside its own block; the Thumb `bl` pair passes its state through `R14` (`bl.hi` sets it, `bl.lo` reads it). Resuming at any instruction boundary therefore reconstructs its state only from `g_cpu`. The generator turns `bl` into a call whose continuation is a separate generated function (`gf_tfunc_080006E2`, `...6E6`, ...), so the region is nine generated functions (98 instruction PCs, 89 interior).

### Mechanism used (config only)

`[[extra_func]] addr = <pc> mode = "thumb" resume = true` per interior instruction: a thin alias that enters the containing generated function, whose resume prologue jumps to that instruction (`docs/TOML_SCHEMA.md`). `[[resume_range]]` was tried and rejected: it re-roots the range start without its symbol name (`gf_InitializeGame` became `gf_tfunc_080006A0`), folds the continuations into one host and seeds the literal-pool words as standalone functions. `static_resume_all` (whole program) was not adopted (measured below). The generator output diff against the previous corpus is purely additive (0 removed lines: resume prologues in 9+ functions and new table rows); `recompiled.h` and the data symbol map are identical; regeneration is byte-reproducible.

### Coverage

241 entries in `configs/mzm-us.toml` in two reviewed units: `InitializeGame` and its continuations (89) and MZM's `sram.c` `[0x080051D4, 0x08005368)` (152). The second was not a guess: after `0x080006CA` the next misses were `0x080051EE` (inside `SramWriteUncheckedInternal`) and `0x08005280` (`SramWrite`): MZM's save initialisation reads/writes/verifies the whole 32 KiB SRAM byte by byte with wait states, which takes several frames, so the yield always lands in these loops. `SramWriteUncheckedInternal` and `SramCheckInternal` are also the two routines the game copies to the stack; the stack-helper hook runs their position-independent ROM translation, whose guest PCs are ROM addresses, so they resume at their ROM PCs (the case NES-3c described as "resumes in the ROM translation"). Only decoded instruction PCs are listed (checked against the generated bodies by `tests/m4/test_resume.py`); literal pools, other modes and neighbouring functions are not.

### Tests (RED first, then GREEN)

Commit `6b21cc8` adds the tests before the fix and they fail specifically for the missing aliases: `mzm-resume-entry` (`tests/m4/resume_entry_test.cpp`, real `runtime_has_static_entry`: right mode succeeds, ARM entry fails, both halves of the Thumb `bl` resume, continuation interiors resume, literal words and bytes after the last instruction are not published, the next function is unchanged) and `tests/m4/test_resume.py` (every decoded instruction of each region function is a resume alias of *that* function, roots unchanged, nothing else published, the config lists exactly those PCs). Not a unit test: the equality of state after a resume with the uninterrupted run; that is covered end to end (the chain continues from the resumed PCs through `sram.c`, `SetupSoundTransfer` and into `InitializeAudio`, and the three M4 harness cases still pass).

### Integration

The same chain as NES-3c (start -> gameplay -> death -> save YES -> quit -> loader reset stub -> SoftReset) now continues: `0x080006CA` no longer misses; MZM's boot runs `InitializeGame`, the SRAM initialisation (native resumes at `0x080051EE`, `0x080052AC/AE/B0`, `0x08005280`) and `SetupSoundTransfer`, with `dispatch_misses=0`, `interpreted_insns=0`, `unmapped=0`, `io_unhandled=0`, self-heal disabled, IRQ depth 0 and host return depth 0 until the next miss.

### A harness defect found on the way

Between the two, `SetupSoundTransfer` (`0x080028F4`) waits for `VCOUNT == 159` by reading the byte at `0x04000006`. The NES behavior harness did not call `bus.io().set_ppu(&ppu)` (the production run loop does, `runtime.cpp:1934`), so `read8(VCOUNT)` returned 0 and the game spun for 10,000 frames with a constant frame hash and no IRQ progress and **no miss** (a livelock, not a coverage gap). It never showed before because the NES emulator does not read VCOUNT. The harness now wires the PPU exactly like production; the qualified frame hashes (title `CB0431A65E6BD988`, menu `16E0720AC211F04B`, gameplay `401276D456096B86`) are unchanged. New diagnostic: `MZM_NES_DIAG_FRAMES="from,count"` prints guest state at frame boundaries.

### New first frontier

| field | value |
|---|---|
| frame | 3223 (CTest script) / 3263 (14,000-frame script) |
| PC / mode | `0x0800271C` Thumb |
| owner | MZM ROM `InitializeAudio` `[0x08002564, 0x080027F8)` interior |
| caller | `lr = 0x080029AD` |
| CPU / depths | `cpsr = 0x6000003F`, `sp = 0x03007E28`; host return depth 0, IRQ depth 0 |
| counters | 1 miss, 6 interpreted instructions, `unmapped=0`, `io_unhandled=0` |

Same mechanism (a ROM routine that outlasts a frame while the harness unwinds), different function. Three consecutive functions of MZM's boot (`InitializeGame`, `sram.c`, `InitializeAudio`) is the evidence that listing units one by one does not scale. Measured cost of the documented upstream policy `static_resume_all = true` (generation into a scratch directory, not adopted): dispatch table 28,670 -> 233,709 rows (x8.2), generated source 152 -> 179 MB (+18%), generation still ~5 s. It is a project-level trade-off (build size and corpus identity), so it needs a decision; the production runner (present-in-place) never unwinds at a VBlank and is unaffected either way.

### Host stack

Hook high-water 5,768,016 bytes (baseline 5,768,000; unchanged apart from a 16-byte alignment difference); passes at 8192 and 5888 KB, segfaults at 5632 KB, exactly as before. Not fixed here.

### Save state

Unchanged status: `SaveToSram` executed and wrote SRAM (first sampled write `0x7FD8` at frame 3120). New observation, not interpreted: after MZM's own boot writes, the final SRAM diff no longer contains `[0x7FD8, 0x8000)` (it contains only MZM's ranges `[0x0000,0x0016)`, `[0x0018,0x0030)`, `[0x6D40,0x6D56)`, `[0x6D58,0x6D70)`, `[0x6DC0,0x6E40)`, 220 bytes). Whether the NES save survives a restart (hardware-faithful vs an emulation defect) is unverified and belongs to the save round-trip milestone. No load path was exercised.

### Validation

CTest 65/65 (new: `mzm-resume-entry`); Python 26/26 (new: `test_resume.py`); M4 01/02/03 PASS; NES-1a/1b/2b/3b/3c PASS; soak PASS; `git diff --check` PASS; GBARecomp unchanged at `2c40fe8`.
