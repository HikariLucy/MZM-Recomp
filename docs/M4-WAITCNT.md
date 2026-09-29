# M4 dynamic WAITCNT qualification

The isolated GBARecomp branch `mzm/waitcnt-timing` starts at `e7728148`.
The official pin remains there. The branch covers programmed Game Pak and
SRAM waitstate costs, not cartridge prefetch-buffer timing or full hardware
cycle accuracy.

## WAITCNT fields

`REG_WAITCNT = 0x04000204`. The hardware table below follows
[GBATEK](https://mgba-emu.github.io/gbatek/). Each bus cost includes one
base cycle in addition to the selected wait cycles.

| Bits | Field | Encoded wait cycles | Full bus cycles |
|---|---|---|---|
| 0–1 | SRAM | 4, 3, 2, 8 | 5, 4, 3, 9 (byte access) |
| 2–3 | WS0 first/N | 4, 3, 2, 8 | 5, 4, 3, 9 |
| 4 | WS0 second/S | 2, 1 | 3, 2 |
| 5–6 | WS1 first/N | 4, 3, 2, 8 | 5, 4, 3, 9 |
| 7 | WS1 second/S | 4, 1 | 5, 2 |
| 8–9 | WS2 first/N | 4, 3, 2, 8 | 5, 4, 3, 9 |
| 10 | WS2 second/S | 8, 1 | 9, 2 |
| 11–12 | PHI terminal output | off, 4.19, 8.38, 16.78 MHz | No CPU waitstate effect modeled |
| 13 | unused | — | — |
| 14 | Game Pak prefetch enable | 0/1 | Stored; timing effect not yet modeled |
| 15 | Game Pak type flag | read-only hardware input | Current IO store does not enforce read-only behavior |

Game Pak ROM has a 16-bit bus: N32 = N16 + S16, S32 = 2 × S16.
ROM byte accesses use a halfword cycle. SRAM is an 8-bit interface; tests
qualify byte accesses only. `GbaIo::waitcnt()` reads the existing IO backing
array, so `GbaBus::access_cycles()` uses one source of truth. CPU and DMA
already consume that function; DMA's cycle-debt calculation changes without
a separate waitstate implementation.

## MZM values and observed access costs

The real decomp defines in `include/gba/waitstate.h` make
`src/init_game.c::InitializeGame` write `0x45B4`: SRAM 4, WS0/1/2 first 3,
second 1, bit 14 set. The header calls bit 14 `WAIT_GAMEPACK_CGB`, but
GBATEK identifies it as hardware prefetch enable; bit 15 is the type flag.
`src/sram/sram.c` writes bits 0–1 as `3` (8 waits) in
`SramWriteUnchecked`, `SramWrite`, and `SramCheck`. Those functions do not
restore the previous value internally. From `0x45B4`, the helper value
would be `0x45B7`. An earlier `0x0003` helper write was actually observed
before `InitializeGame`. The NES Metroid payload assembly records `0x0014`;
its runtime route is outside this validation.

An opt-in MMIO dump from a strict-static 1400-step MZM run records
power-on `0x0000`, the preceding `0x0003`, and the `0x45B4` halfword
write at PC `0x08000706`. A temporary bus probe recorded the following
live WS0 N16/S16 costs; the probe was removed afterward.

| Point | WAITCNT | WS0 N16/S16 | WS0 N32/S32 | SRAM byte |
|---|---:|---:|---:|---:|
| Power-on | `0x0000` | 5/3 | 8/6 | 5 |
| Before InitializeGame write | `0x0003` | 5/3 | 8/6 | 9 |
| After InitializeGame write | `0x45B4` | 4/2 | 6/4 | 5 |
| Later SRAM helper from that state (source-derived, not observed) | `0x45B7` | 4/2 | 6/4 | 9 |

The generic test exercises all four SRAM fields and a dynamic `0x0014`
to `0x0017` SRAM change: **SRAM waitstate semantics PASS**. Full MZM SRAM
operation timing is **PARTIAL/UNVERIFIED** without a complete save trace.

## RED → GREEN and regressions

At `e7728148`, the corrected generic test had **135 failing assertions**:
programmed WS0/1/2 N/S and 16/32-bit costs, default WS1/WS2 S timing,
SRAM variants, dynamic writes, and DMA cycle debt. Default WS0 and the
unchanged internal-memory assertions passed. After the fix, WAITCNT, bus,
and DMA tests pass (3/3), the full applicable upstream CTest suite passes
(35/35), and `git diff --check` passes.

The `build-m4-waitcnt` build passes both MZM passive strict-static cases.
They end in BIOS before `InitializeGame`, so their cycles stay unchanged:

| Case | Baseline/candidate cycles | Final PC | PPU frames | Strict-static error counters |
|---|---:|---|---:|---|
| `01_boot_headless` | 0/0 | `0x00000c0c` | 1 | all 0 |
| `02_static_120_frames` | 24,600,475/24,600,475 | `0x00000348` | 120 | all 0 |

`compare-m4-regression.py` reports `UNCHANGED` for both. In a separate
1400-step probe with VBlank yielding disabled, baseline/candidate cycles
were 67,641,054/67,667,408 (+26,354). Both finished at `0x000001b4`
with `dispatch_misses=interpreted_insns=unmapped=io_unhandled=0`.
The changed cycle count is expected from a timing fix, not a strict-static
regression. This probe is not a cycle oracle.

## Remaining prefetch timing gap

MZM enables prefetch with hardware bit 14. GBATEK describes an
eight-halfword opcode buffer filled when the Game Pak bus is idle.
Cartridge instruction fetches can consume buffered opcodes; ROM data
reads and code executing in RAM/BIOS do not receive that benefit.
`access_cycles(addr,width,sequential)` has no fetch/data distinction,
buffer occupancy, fill progress, or branch history. A later model needs
per-fetch state, idle-time fill, consumption, and invalidation/interruption
rules for branches, ROM data accesses, DMA contention, and WAITCNT changes.
Forced nonsequential timing at Game Pak 128 KiB boundaries also remains
outside this stateless helper. No prefetch timing credit is claimed.

## Cycle oracle

`oracle/diff_cycle_nba.py` compares cycle deltas at a recurring PC when
recomp and NanoBoyAdvance debug servers run on ports 19842/19844. No local
NBA service or binary was found, so it was not run and no emulator was
installed. With an MZM oracle session, enable recomp instruction trace,
choose a recurring PC reached after `InitializeGame`, and run
`python oracle/diff_cycle_nba.py --pc <pc> --hits 8 --frames 8` against
baseline and candidate. Keep prefetch validation separate. Unit tests
alone do not establish hardware-accurate timing.
