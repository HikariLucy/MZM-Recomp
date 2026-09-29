# M4 NES Metroid executable subsystem audit

Date: 2026-09-29. Target: USA rev 0 ROM (SHA-256 `fc94f65380b65b870a30b9b04b39cca1dc63d6e46a4a373d3904adc0912ebc37`). MZM HEAD `770349f`; integrated GBARecomp `644ec842f8b2106f21fdef6ae05ae997c8e49869`. This is an architectural audit, not a NES gameplay qualification. Source references below are in the local `Metroid-ZeroMissionRecomp/_m0/upstream/mzm` decomp unless otherwise stated. Its *main* `mzm_us.map`/ELF and built `mzm_us.gba` exist; the built main ROM is **byte-identical** to the legal USA ROM. The nested NES emulator/payload maps and binaries are absent. Sizes below therefore distinguish actual DMA counts and ROM observations from unbuilt linker section sizes.

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
| **NES-1a (next)** | Generate ordinary public ROM entries for `0x087D8000`, `0x087D80D4`, and return `0x087D8124`; synthetic strict-static control-flow test and byte identity; no gameplay claim |
| NES-1b | Controlled real loader SWI 0x11 reaches **first RAM PC `0x03007400`**, with reconstructed-byte gate and zero strict-static misses |
| NES-2 | Payload executes through all decompression/DMA stages to `0x06006558`; generic VRAM dispatch and external-image support qualified |
| NES-3 | Emulator initializes and yields first frame with zero strict-static counters |
| NES-4 | Title, input and audio qualified against a reference |
| NES-5 | Password/SRAM/save and Quit/Reset behavior qualified |
| NES-6 | SoftReset returns cleanly to MZM at `0x08000000` with preserved SRAM |

A future local boot-chain fixture may prepare documented file-select stage/register state and execute the **real** `OptionsNesMetroidHandler`/ROM loader on the legal ROM, stopping at `0x03007400`. It must not patch guest instructions or infer arbitrary menu state. If a valid state cannot be constructed, capture a genuine local checkpoint. Such a fixture proves integration, not gameplay. The next implementation target is NES-1a because strict-static currently fails at the ROM data-as-code stub before any decompression. After that, NES-0/external-image work becomes the prerequisite for RAM execution. No large generator/runtime refactor begins in this audit.

## Validation and open evidence

Baseline regression on integrated binary: 01, 02, 03 **PASS**, all four strict-static counters zero. Case 03: 317 frames, WAITCNT `0x45B4`, final PC `0x000001B4`. The built MZM CTest suite is **53/53 PASS** (three MZM-local and 50 integrated GBARecomp tests). `build-m4-integration/CMakeCache.txt` points to the exact integrated GBARecomp worktree and its HEAD is the pin above. The synthetic VRAM fixture and generated output remain ignored under `.local/nes-vram-fixture*`; its public-child failure is reproducible. Nested emulator/payload maps and binaries, exact custom-decompressed image hashes, a **passing** VRAM private runtime fixture, real boot checkpoint, and NES gameplay remain unavailable/unqualified. No derived ROM image is tracked.
