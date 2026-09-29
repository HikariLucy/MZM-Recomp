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

Images come only from the ROM via `scripts/extract-nes-emulator.py`. Function seeds and code/data runs come from `scripts/derive-nes-emulator-map.py`, which reads a **locally built** nested decomp ELF used purely as an address/mode oracle (built out of tree in scratch; agbcc + `arm-none-eabi-ld`). That ELF's sections equal the ROM-derived Parts byte for byte, except 46 bytes at/after `0x0203E772` in Part 6 (different libgcc thunk placement; the ROM layout is encoded explicitly in the script and checked against the extracted image). Nothing ROM-derived is committed: `configs/mzm-us-nes-emulator.toml` and `src/mzm_nes_emulator_map.h` hold addresses, modes and digests only.

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
- Regeneration is deterministic through `scripts/generate-m1.sh`, which now extracts the emulator images and passes `configs/mzm-us-nes-emulator.toml`.

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

- All six Parts are `overlay = true` images with `image =` tags (`configs/mzm-us-nes-emulator.toml`, generated by `scripts/derive-nes-emulator-map.py`); Part 5 `0x0600E474` is seeded. Seeds are the decomp `FUNC` symbols **plus** indirect-transfer targets discovered from pointer words in the image's data runs and validated against decomp symbols (needed for the 6502 opcode handlers, which are local labels: 388 seeds in total across the six Parts).
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
