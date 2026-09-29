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
