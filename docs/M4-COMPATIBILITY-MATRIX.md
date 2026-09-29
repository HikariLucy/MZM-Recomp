# M4 compatibility matrix (USA rev 0)

Audit date: 2026-09-28. Main base: `c70b039`. GBARecomp checkout actually used by `scripts/generate-m1.sh` and `scripts/build-m1.sh`: `e7728148c6829ba526f682876430a0c9022dc6c0` (clean at audit). `PASS` means only the stated route passed. Framework capability alone is not an MZM pass. `PARTIAL` means some evidence exists but scope or fidelity remains open. `BLOCKED` identifies a demonstrated engine gap. Historical evidence is labeled separately from the new automated run.

| Area | Feature / Route | Status | Evidence | Automation | Remaining Risk |
|---|---|---|---|---|---|
| Boot | USA cold boot, one headless frame | PASS | New `01_boot_headless`: strict-static, zero counters | `tests/m4/cases.toml` | Later boot behavior outside one frame |
| Boot | 120 passive headless frames | PASS | New `02_static_120_frames`: 120 PPU frames, zero counters | `tests/m4/cases.toml` | Does not reach title |
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
| Executable RAM | Fixed IRQ, audio A/B/C, clipdata mappings | PARTIAL | [M0.6](M0.6-HARDWARE-MATRIX.md): `code_copies=5`; [M3](evidence/M3-STRICT-GAMEPLAY.md) route | Indirectly in baseline | Each copy/variant not independently traced |
| Executable RAM | SRAM stack-local helpers | PASS | [M2A](evidence/M2A-SRAM-STACK-CODE.md): byte-verified native canonicalizer, zero misses | Indirectly in baseline | Only two known helper bodies |
| Indirect calls | Early route's callbacks and jump tables | PARTIAL | `undefined=0`, 335 auto jump tables; [M3](evidence/M3-STRICT-GAMEPLAY.md) zero misses | Indirectly in baseline | Later targets unknown |
| Haze effects | Same-PC `hazeCode` variants | BLOCKED | [M0.6](M0.6-HARDWARE-MATRIX.md): several sources overwrite one RAM PC; fixed mapping only | No | Generic variant-aware dispatch needed; MZM route unqualified |
| Mosaic | BG and OBJ mosaic rendering | BLOCKED | Pin has no mosaic logic in `src/gba/gba_ppu.cpp`; accuracy burndown lists gap | No test in pin | Generic upstream PPU fix and MZM scene oracle needed |
| WAITCNT / prefetch | Register storage and timing | PARTIAL | Pin: `src/gba/gba_io.h` defines WAITCNT; `gba_bus.cpp::access_cycles` uses default timings | No MZM differential test | Dynamic waitstate and cart prefetch timing absent |
| Chozodia | Escape HBlank RAM callback | UNVERIFIED | [M0.6](M0.6-HARDWARE-MATRIX.md) identifies 0x40-byte copy; absent from five configured mappings | No | Late route and mapping qualification |
| Bosses | All main-game fights | UNVERIFIED | No recorded boss route | No | Progression/visual/audio correctness |
| Endings | Final sequence and ending | UNVERIFIED | No recorded completion | No | Full-game claim unavailable |
| Zero Suit | Late Zero Suit section | UNVERIFIED | No recorded route | No | Mechanics, transitions, copied code |
| NES Metroid | Unlockable bundled game | UNVERIFIED | [M0](M0-FEASIBILITY.md): RAM/VRAM dynamic payload; separate high-risk target | No | Dynamic executable architecture and route |
| Fusion Link | Serial/Timer3 route | UNVERIFIED | [M0.6](M0.6-HARDWARE-MATRIX.md): runtime infrastructure only | No | Protocol and peripheral qualification |
| Europe ROM | EU region native execution | UNVERIFIED | `STATUS.md` records cartridge identity only; USA generated corpus/config | No | Region-specific generation and routes |

## Inventory by evidence level

- **VERIFIED:** the two new passive headless gates; historical strict-static intro/title, New Game, early rooms, Save Room write, SRAM reload, and the two SRAM stack helper bodies.
- **PARTIALLY VERIFIED:** host save-state/rewind mechanisms, audio, PPU excluding mosaic, IRQ, DMA, timers, fixed executable copies, indirect calls, and WAITCNT register/default-cycle behavior. These are architecture or route evidence with fidelity and late-route gaps.
- **UNVERIFIED:** Chozodia, bosses, endings, Zero Suit, NES Metroid, Fusion Link, and Europe execution.
- **KNOWN GAP:** BG/OBJ mosaic is unrendered; mutable same-PC haze code lacks variant-aware native dispatch; dynamic WAITCNT/prefetch timing remains unmodeled. These do not invalidate the proven early route.

## Pin audit details

**Mosaic:** `src/gba/gba_ppu.cpp` reads neither MOSAIC register `0x04C` nor OAM mosaic enable bits when sampling BG/OBJ. The scanline renderer (`render_scanline_internal`, `render_scanline_wide`) and PPU tests (`tests/ppu_smoke/test_main.cpp`) are the upstream correction/test locations. The register can be observed in diagnostics, which does not render its effect. No MZM mosaic scene has been captured.

**WAITCNT/prefetch:** `src/gba/gba_io.h` defines WAITCNT at `0x204`; IO storage/diagnostics accept writes. `src/gba/gba_bus.cpp::access_cycles` explicitly uses default `WAITCNT=0x0000` for ROM WS0/1/2 and SRAM. The `prefetch_word` and BIOS open-bus latch model BIOS/open-bus data, not cartridge prefetch waitstate timing. MZM writes WAITCNT, but no differential timing test qualifies those writes. This is a dynamic timing gap, not a missing MMIO register.

**Executable RAM:** fixed ROM→IWRAM `[[code_copy]]` mappings are supported; MZM's configured five are IRQ, sound A/B/C and clipdata. The RAM dispatch hook in `src/mzm_ram_dispatch.cpp` byte-verifies two position-independent stack-local SRAM helpers and calls their generated native bodies; early strict-static/save evidence qualifies those helper routes. A fixed runtime-PC→one-source mapping cannot select all `hazeCode` variants. Chozodia's HBlank copy and NES Metroid's multi-region payload are identified but unqualified in MZM. Framework infrastructure is not proof those routes run.

**Step-limit observation:** A new strict-static `--steps 1000` probe aborted at `0x080006CA` with a dispatch miss before the runtime summary. The accepted boot case uses `--frames 1` and the 120-frame case passes. The step probe is not interpreted as general gameplay failure; its control flow and hook differences require a separate investigation before making it a baseline.

## Running the local regression collection

```bash
export MZM_ROM=/path/to/verified-USA.gba
export MZM_BIOS=/path/to/verified-gba-bios.bin
scripts/run-m4-regression.sh --bin build-m1/MZMRecomp
python3 scripts/compare-m4-regression.py baseline/summary.json candidate/summary.json
```

`--rom` and `--bios` can replace the environment variables. The runner reads `tests/m4/cases.toml`; each case declares a USA ROM target, BIOS requirement, config, exactly one positive `frames` or `steps` limit, optional `checkpoint`/`load_state` name, expectations (`final_pc` and `ppu_frames` only when stable), and expected artifacts (`final.png` currently). `--cases`, `--output`, `--timeout`, `--config`, and `--checkpoint` are available. The runtime receives `GBARECOMP_STRICT_STATIC=1`; an unmet expected counter, artifact, exit code, or timeout fails the run. The runner stores its own SRAM per case under ignored `dist/m4-regression/<UTC timestamp>/artifacts/`, so it does not alter the ROM-adjacent save.

Each run writes `summary.txt`, `summary.json`, `logs/<case>.log`, and `artifacts/<case>/`. The JSON stores metrics and relative case names without private absolute paths. Logs and artifacts remain local and ignored; runtime diagnostics may contain local paths and game-derived content, so review them before sharing. The comparison reports `REGRESSION`, `UNCHANGED`, `IMPROVED`, or `NOT COMPARABLE` per case. It compares exit/case status and the four strict-static error counters. It compares `final_pc` only when the case declares an expected stable value.

For future checkpoints, place a genuine local state at `.local/m4-checkpoints/<name>.state` and invoke `--checkpoint <name>` or set `checkpoint = "<name>"` in a case. The runtime's headless `--load-state` resumes from that file; a missing state fails closed without starting the game. The directory is ignored by Git and no checkpoint is bundled or generated here. Record checkpoint provenance privately: game build, ROM hash, route, and capture method. A state may contain ROM-derived content and should not enter commits or distributable reports.
