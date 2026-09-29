# M4 compatibility matrix (USA rev 0)

Audit date: 2026-09-29. Main base: `c70b039`. GBARecomp checkout actually used by `scripts/generate-m1.sh` and `scripts/build-m1.sh`: `e7728148c6829ba526f682876430a0c9022dc6c0` (clean at audit). `PASS` means only the stated route passed. Framework capability alone is not an MZM pass. `PARTIAL` means some evidence exists but scope or fidelity remains open. `BLOCKED` identifies a demonstrated engine gap. Historical evidence is labeled separately from the new automated run.

| Area | Feature / Route | Status | Evidence | Automation | Remaining Risk |
|---|---|---|---|---|---|
| Boot | USA cold boot, one headless frame | PASS | New `01_boot_headless`: strict-static, zero counters | `tests/m4/cases.toml` | Later boot behavior outside one frame |
| Boot | 120 passive headless frames | PASS | New `02_static_120_frames`: 120 PPU frames, zero counters | `tests/m4/cases.toml` | Does not reach title |
| Boot | 1400-step `InitializeGame` timing probe | PASS | New `03_initialize_game_timing`: `0x45B4` write, stable PC/frame/cycle counts over three direct runs; zero strict-static counters | `tests/m4/cases.toml` observes MMIO write | Execution gate only; no cycle oracle |
| Intro | IntroHandler reached | PASS | [M2B](evidence/M2B-STRICT-TITLE.md): 97 hits in strict-static session | No input replay case yet | Long route still manual |
| Title | TitleScreenHandler reached | PASS | [M2B](evidence/M2B-STRICT-TITLE.md): 140 hits | No | Menu fidelity not compared |
| New game | Selected in qualified route | PASS | [M3](evidence/M3-STRICT-GAMEPLAY.md): operator route, zero static misses | No | Inputs not recorded |
| Early gameplay | Controllable Samus, multiple rooms, Save Room | PASS | [M3](evidence/M3-STRICT-GAMEPLAY.md): strict-static manual evidence | Checkpoint candidate | Later areas unverified |
| Save SRAM | 32 KiB Save Room write | PASS | [M3](evidence/M3-STRICT-GAMEPLAY.md): persisted save and hash | No scripted input | Slot/copy/erase paths open |
| Reload SRAM | New strict-static process loads save into gameplay | PASS | [M4.1](evidence/M4.1-SAVE-ROUNDTRIP.md): successful behavioral round-trip | No scripted input | Cross-build and corrupted save handling open |
| Save states | Host state slots and `--load-state` exist | PARTIAL | `src/main.cpp`; pinned `src/runtime/runtime.cpp` save/load path | Named local checkpoint loading supported by harness | No MZM save/load checkpoint qualified |
| Rewind | Host 15-second rewind configured | PARTIAL | `src/main.cpp`; pinned `src/runtime/runtime.cpp` rewind capture | No | No MZM replay/correctness evidence |
| Audio | PSG, Direct Sound and FIFO runtime paths | PARTIAL | [M0.6](M0.6-HARDWARE-MATRIX.md); early route ran | No audio oracle | Fidelity/timing not checked |
| PPU | Tile/affine, OBJ, windows, blending | PARTIAL | [M0.6](M0.6-HARDWARE-MATRIX.md); headless framebuffer artifact | Final PNG only | No pixel oracle; subscanline effects open |
| Interrupts | Boot IRQ/VBlank/HALT route and copied `IntrMain` | PARTIAL | [M2B](evidence/M2B-STRICT-TITLE.md), `[[code_copy]]` generated; 120 frames | Indirectly in baseline | HBlank/late callback routes open |
| DMA | Boot/early immediate and timed machinery | PARTIAL | [M0.6](M0.6-HARDWARE-MATRIX.md), qualified route | Indirectly in baseline | HBlank effect fidelity open |
| Timers | Runtime timers 0..3; early route | PARTIAL | [M0.6](M0.6-HARDWARE-MATRIX.md) | Indirectly in baseline | Timer3/link and audio timing not qualified |
| Executable RAM | Fixed IRQ and audio A/B/C mappings | PARTIAL | [M0.6](M0.6-HARDWARE-MATRIX.md): `code_copies=5`; [M3](evidence/M3-STRICT-GAMEPLAY.md) route | Indirectly in baseline | Each copy not independently traced |
| Executable RAM | Fixed clipdata code copy | PARTIAL | `[[code_copy]]` at `0x030016C4`, source `0x08057F7C` | Indirectly in baseline | No isolated clipdata scene/trace |
| Executable RAM | SRAM stack-local helpers | PASS | [M2A](evidence/M2A-SRAM-STACK-CODE.md): byte-verified native canonicalizer, zero misses | Indirectly in baseline | Only two known helper bodies |
| Indirect calls | Early route's callbacks and jump tables | PARTIAL | `undefined=0`, 335 auto jump tables; [M3](evidence/M3-STRICT-GAMEPLAY.md) zero misses | Indirectly in baseline | Later targets unknown |
| Haze RAM code | Seven copied byte-window variants identified | PASS | [Haze audit](M4-HAZE-RAM-CODE.md): 7/7 synthetic and verified local-ROM matching; native targets present | Redistributable resolver CTest plus local ROM run | Identification does not execute a full guest scene |
| Haze RAM code | Strict-static native execution in a real scene | UNVERIFIED | MZM hook selects six RAM-call variants after byte-perfect match; trace confirms 01/02/03 have zero haze hits; capture flow prepared | No real scene/checkpoint yet | IRQ/resume PC fidelity and zero-miss route untested |
| Haze RAM code | Power Bomb expansion→retraction swap | UNVERIFIED | Decomp copies two different sources into one destination consecutively | No Power Bomb checkpoint | Dynamic swap and full function execution untested |
| Mosaic | BG/OBJ native rendering in local upstream branch; MZM scene | PARTIAL | Pin lacks it; local `mzm/ppu-mosaic` passes 14 synthetic cases and upstream 48/48; no MZM scene | Synthetic CTest; MZM passive cases `UNCHANGED` | Official pin remains unpatched; real scene and extended-view margin not qualified |
| WAITCNT | Dynamic Game Pak/SRAM waitstates in isolated upstream branch | PASS | [M4 WAITCNT](M4-WAITCNT.md): corrected RED 135 failures → 35/35 upstream GREEN; pin unchanged | Synthetic `waitcnt_tests` | Full hardware timing still unqualified |
| WAITCNT | MZM programmed-value timing route | PARTIAL | `0x45B4` write and live WS0 5/3→4/2 observed in 1400-step probe; passive cases unchanged | Local MMIO dump and temporary bus probe | Full SRAM route and NBA cycle oracle not run |
| Game Pak prefetch | Buffer/pipeline timing | BLOCKED | [Prefetch audit](M4-GAMEPAK-PREFETCH.md): fixed generated/interpreter fetch costs lack a shared dynamic seam; MZM sets bit 14 but timing is absent | No prefetch oracle | Opcode queue, fill/flush/contended-bus model and parity/snapshot work needed |
| Chozodia | HBlank RAM image identification | PASS | [Chozodia audit](M4-CHOZODIA-RAM-CODE.md): linked address and full 0x40-byte synthetic/local-ROM match | Resolver CTest and local ROM test | Identification does not execute the callback |
| Chozodia | Native RAM callback dispatch | BLOCKED | Generated target exists, but ROM translation exposes ROM PC and ROM-relative timing during IRQ | No execution test | Generic logical RAM-PC/timing seam required |
| Chozodia | HBlank IRQ delivery | PARTIAL | PPU event, IF request, IE/IME/CPSR gate and IRQ driver implemented; no isolated full-path upstream test | No | End-to-end MZM route unproven |
| Chozodia | Repeated HBlank callback execution | UNVERIFIED | No late-game checkpoint | No | Need multiple hits across enabled scanlines |
| Chozodia | IRQ/resume correctness | BLOCKED | `runtime_tick` can preempt native code whose `g_cpu.R[15]` is ROM; interior resume unqualified | No | Repeated RAM entry→return→IRQ exit→re-entry required |
| Chozodia | Real Escape scene | UNVERIFIED | No Chozodia scene/checkpoint | No | Visual and strict-static route untested |
| Bosses | All main-game fights | UNVERIFIED | No recorded boss route | No | Progression/visual/audio correctness |
| Endings | Final sequence and ending | UNVERIFIED | No recorded completion | No | Full-game claim unavailable |
| Zero Suit | Late Zero Suit section | UNVERIFIED | No recorded route | No | Mechanics, transitions, copied code |
| NES Metroid | Unlockable bundled game | UNVERIFIED | [M0](M0-FEASIBILITY.md): RAM/VRAM dynamic payload; separate high-risk target | No | Dynamic executable architecture and route |
| Fusion Link | Serial/Timer3 route | UNVERIFIED | [M0.6](M0.6-HARDWARE-MATRIX.md): runtime infrastructure only | No | Protocol and peripheral qualification |
| Europe ROM | EU region native execution | UNVERIFIED | `STATUS.md` records cartridge identity only; USA generated corpus/config | No | Region-specific generation and routes |

## Inventory by evidence level

Current row counts: **39 total — 13 PASS, 13 PARTIAL, 3 BLOCKED, 10 UNVERIFIED**.

- **VERIFIED:** the two passive headless gates and the 1400-step `InitializeGame` execution/write gate; historical strict-static intro/title, New Game, early rooms, Save Room write, SRAM reload, the two SRAM stack helper bodies, identification of seven haze RAM images, and the Chozodia 0x40-byte RAM image.
- **PARTIALLY VERIFIED:** host save-state/rewind mechanisms, audio, PPU excluding mosaic, IRQ, DMA, timers, fixed executable copies, indirect calls, and the MZM WAITCNT route beyond observed writes/accesses. These have fidelity or late-route gaps.
- **UNVERIFIED:** real haze strict-static execution, Power Bomb swap, repeated Chozodia callbacks, the real Chozodia scene, bosses, endings, Zero Suit, NES Metroid, Fusion Link, and Europe execution.
- **KNOWN GAP:** the official GBARecomp pin still lacks BG/OBJ mosaic and dynamic WAITCNT waitstates (both corrected only in separate local branches); Game Pak prefetch timing remains unmodeled. Haze has byte-verified dispatch but no real-scene or IRQ/resume qualification. Chozodia's image is identified, while ROM-PC exposure and ROM-relative timing block safe native IRQ dispatch.

## Pin audit details

**Mosaic:** The official `e7728148` pin reads no MOSAIC register or enable bits for rendering. The isolated local `mzm/ppu-mosaic` branch implements native BG/OBJ/OBJ-window sampling; 14 synthetic cases and 48/48 upstream tests pass. A patched MZM build passes both passive strict-static cases and compares `UNCHANGED` with the pin. The decomp contains register writes but no proven nonzero size plus layer enable; `SPRITE_STATUS_MOSAIC` is an affine matrix selector, not the hardware OAM bit. No MZM scene has been captured. See [M4 PPU MOSAIC](M4-PPU-MOSAIC.md).

**WAITCNT/prefetch:** The pin's `access_cycles` is still static. The independent `mzm/waitcnt-timing` branch reads live WAITCNT from IO and handles SRAM/WS0/WS1/WS2, including ROM 32-bit splits and DMA cost. Generic tests and an MZM write/access probe qualify the dynamic part; no NBA cycle oracle qualifies overall timing. The new case 03 makes the `0x45B4` write reproducible with VBlank yielding disabled, but is not a prefetch timing oracle. MZM's value sets hardware bit 14 despite the decomp's `WAIT_GAMEPACK_CGB` name. The BIOS open-bus latch is unrelated to cartridge prefetch. See [M4 WAITCNT](M4-WAITCNT.md) and the [Game Pak prefetch audit](M4-GAMEPAK-PREFETCH.md).

**Executable RAM:** fixed ROM→IWRAM `[[code_copy]]` mappings are supported; MZM's configured five are IRQ, sound A/B/C and clipdata. The RAM dispatch hook byte-verifies two position-independent stack-local SRAM helpers and now identifies seven full 512-byte haze images at the shared RAM address before selecting native code. Six are called from RAM by `HazeProcess`; the seventh copy is called directly from ROM. Synthetic and local-ROM resolver tests pass, while no real haze scene has yet proved full strict-static execution or IRQ/resume behavior. Chozodia's 0x40-byte HBlank image is identified, but native IRQ dispatch is blocked by ROM-PC exposure; NES Metroid's multi-region payload remains unqualified. See [M4 HAZE RAM CODE](M4-HAZE-RAM-CODE.md) and [M4 CHOZODIA RAM CODE](M4-CHOZODIA-RAM-CODE.md).

**Haze capture gate:** MZM-only opt-in trace counts hook calls, resolver attempts and matches, and records a first native variant hit. A wrapper requests a local save through upstream's existing windowed TCP observer after the generated call stack unwinds. A temporary boot-state save/load validated the capture mechanism, but `.local/m4-checkpoints/haze-bg3.state` does not exist. No case 04 or RED→GREEN scene result is claimed.

**Step-limit observation:** `0x080006CA` is Thumb `ldr r1,[r0,#8]` inside `InitializeGame` (`mzm_us.elf` disassembly; generated `gf_InitializeGame`). It is compiled into the function body but absent as a standalone `dispatch_table.cpp` entry. Raw headless `--steps 1000` enables the default VBlank unwind and re-dispatches at this interior PC, causing a strict-static miss. Repeating the same 1000-step probe with `GBARECOMP_YIELD_ON_VBLANK=0` exits normally with `dispatch_misses=0`, `interpreted_insns=0`, `unmapped=0`, `io_unhandled=0`; passive `--frames` uses present-in-place and passes. This is a headless test-mode yield artifact, not evidence of missing generated instruction coverage. The general interior-PC unwind remains a runtime limitation for that mode.

## Running the local regression collection

```bash
export MZM_ROM=/path/to/verified-USA.gba
export MZM_BIOS=/path/to/verified-gba-bios.bin
scripts/run-m4-regression.sh --bin build-m1/MZMRecomp
python3 scripts/compare-m4-regression.py baseline/summary.json candidate/summary.json
```

`--rom` and `--bios` can replace the environment variables. The runner reads `tests/m4/cases.toml`; each case declares a USA ROM target, BIOS requirement, config, exactly one positive `frames` or `steps` limit, optional `checkpoint`/`load_state` name, expectations (`final_pc` and `ppu_frames` only when stable), and expected artifacts (`final.png` currently). Case 03 sets `vblank_yield = false` and `waitcnt_write = 0x45B4`, which requires the value in a local MMIO trace. The summary also records guest `cycles`; it does not compare their direction as a pass/fail signal. `--cases`, `--output`, `--timeout`, `--config`, and `--checkpoint` are available. The runtime receives `GBARECOMP_STRICT_STATIC=1`; an unmet expected counter, artifact, exit code, or timeout fails the run. The runner stores its own SRAM per case under ignored `dist/m4-regression/<UTC timestamp>/artifacts/`, so it does not alter the ROM-adjacent save.

Each run writes `summary.txt`, `summary.json`, `logs/<case>.log`, and `artifacts/<case>/`. The JSON stores metrics and relative case names without private absolute paths. Logs and artifacts remain local and ignored; runtime diagnostics may contain local paths and game-derived content, so review them before sharing. The comparison reports `REGRESSION`, `UNCHANGED`, `IMPROVED`, or `NOT COMPARABLE` per case. It compares exit/case status and the four strict-static error counters. It compares `final_pc` only when the case declares an expected stable value.

For future checkpoints, place a genuine local state at `.local/m4-checkpoints/<name>.state` and invoke `--checkpoint <name>` or set `checkpoint = "<name>"` in a case. The runtime's headless `--load-state` resumes from that file; a missing state fails closed without starting the game. The directory is ignored by Git and no checkpoint is bundled or generated here. Record checkpoint provenance privately: game build, ROM hash, route, and capture method. A state may contain ROM-derived content and should not enter commits or distributable reports.
