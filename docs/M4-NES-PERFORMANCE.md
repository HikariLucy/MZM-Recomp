# M4 NES performance investigation (NES-PERF-1)

Date: 2026-10-01. Base `a223020` (`feat/m4-compat-harness`), work branch `feat/nes-performance`.
GBARecomp: pinned `0b9d0326d53a28516d06f2eab43b2bc72c6fef43` before, `6198f771f35cf7cbbbb6a80f013c9363fd343866`
after (one generic commit on local branch `mzm/private-lookup-index`; nothing pushed).
This milestone is independent of the frozen Windows candidate (`feat/windows-beta-2 @ 8892ba7`),
which was not touched; no ZIP was built.

**Result.** The integrated NES emulator was slow because the generated
`runtime_invoke_private_entry_in_image` scanned the 7,836-row private dispatch table
linearly, about 9,900 times per frame. Replacing the scan with a binary search over a
sorted key index (generic GBARecomp change, same semantics) takes the NES title loop from
**39.7 ms/frame to 6.3-6.6 ms/frame (6.0-6.3x)**, i.e. about 152 frames/s headless, 2.5x the
59.7 Hz real-time rate and faster than MZM itself was before (3-3.6 ms/frame). All functional
output (frame hashes, audio statistics, SRAM, strict counters, host-stack depth) is identical.

## Methodology

* Build: RelWithDebInfo, `gcc 13.3.0`, `-O2 -g -DNDEBUG -std=gnu++20`, CMake/Make, the three
  builds (base, instrumented, optimized) use identical flags and differ only in the stated
  option / GBARecomp revision. CPU: AMD Ryzen 7 5800H, governor `performance`, 16 threads.
  Benchmarks run one at a time, single thread.
* Benchmark A (CPU/emulation, headless): `mzm_nes_behavior_test` title loop, no input, 100 /
  1,000 / 3,000 frames (and 10,000 for the soak). Frame 1-10 is the ROM loader and payload
  start-up (about 0.3 s of the totals). The renderer is the same software scanline
  compositor used by every harness run, so renderer cost is a separate measured bucket.
* Benchmark B (real window presentation) was **not** re-measured: on this branch the NES
  cannot be entered from the shipped binary without a completed game, and Xephyr/llvmpipe is
  not representative of a real GPU. Host GL upload/present is therefore unmeasured; only the
  host scanline compositor (`GbaPpu::render_scanline`) is in the table below.
* Profiling (`-DMZM_PERF_PROFILE=ON`, default OFF, all hooks compile to nothing): exclusive
  TSC attribution (a single "current bucket"; nested activations are never double counted),
  `--wrap` shims for `runtime_dispatch*`, `runtime_tail_drain`, PPU/audio/timer/DMA ticks,
  per-Part counters, an in-memory histogram keyed by (source return address, target PC,
  dispatch kind), and a native function-entry histogram. Nothing prints per call.
  `MZM_PERF=1` enables it at run time; `MZM_PERF_SCAN=0` disables the private-scan replica.
  Instrumentation overhead: +4% wall before the fix (100 frames, 4.07 s vs 3.91 s), +11% after
  (1,000 frames, 7.34 s vs 6.59 s; many more timer switches per second).
* The private lookup cost was isolated with a *replica* of the linear scan run alongside the
  real call (30.2 ms/frame) and confirmed by the A/B result, not assumed.

## Baseline (before)

| Frames | Wall | ms/frame | fps |
|---:|---:|---:|---:|
| 100 | 3.91 s | 39.1 | 25.6 |
| 1,000 | 39.73 s | 39.7 | 25.2 |
| 3,000 | 118.97 s | 39.7 | 25.2 |

MZM alone: 3-3.6 ms/frame. Matches the earlier audit (35-39 ms).

## Calls per frame (title loop, 1,000 frames, after; before-values at 100 frames agree)

| Counter | Per frame |
|---|---:|
| `runtime_dispatch` | 51.5 |
| `runtime_dispatch_tail` | 9,871 |
| `runtime_dispatch_with_exchange(_tail)` | 0 / 51.9 |
| `runtime_tail_drain` calls (with a pending tail) | 2.3 (0.03) |
| `mzm_ram_dispatch` hook calls | 9,960 |
| `runtime_invoke_private_entry_in_image` (all succeed) | 9,923 |
| live-byte snapshot hits / SHA fallbacks / SHA failures | 9,923 / 0.007 / 0 |
| guest ARM/Thumb instructions (`runtime_tick`) | 108,340 |
| native function activations (fn-entry hook, 100 frames) | 14,359 |

Per Part (1,000 frames, total dispatch + tail): Part 2 9,903,644 (99.3% of all); Part 6 6,917;
Part 5 6,951; Part 3 5,275; Part 1 5; Part 4 2; non-NES 51,940. The NES hot path is
essentially only Part 2. `runtime_invoke_private_entry` (unscoped) is not used by the hook.

## Time breakdown

Before (100 frames, replica ON, instrumented, 70.3 ms/frame wall; with the replica OFF the
real scan sits inside "generated native" at 32.5 ms/frame, 40.7 ms/frame wall):

| Bucket | ms/frame | Note |
|---|---:|---|
| **private-table lookup (replica of the real scan)** | **30.2** | avg 4,605 rows scanned per call, 9,070 calls/frame |
| generated native excluding the scan | ~2.3 | 32.45 minus the scan |
| harness | 3.6 | |
| live-byte memcmp (snapshot) | 3.07 | |
| hook bookkeeping | 0.62 | |
| host scanline render | 0.49 | |
| runtime dispatch/tail machinery | 0.22 | |
| PPU tick + audio + timers + timed DMA | 0.18 | audio alone 0.04 |

~74% of the frame was the linear scan; the buckets above explain >95% of the 40.7 ms.

After (1,000 frames, instrumented 7.34 ms/frame; real un-instrumented 6.59 ms/frame):

| Bucket | ms/frame | % |
|---|---:|---:|
| live-byte memcmp (Part 2 gate, 20,360 B) | 3.12 | 42.5 |
| generated native (incl. the new binary search ~0.1) | 2.33 | 31.7 |
| host scanline render | 0.58 | 7.9 |
| hook bookkeeping | 0.43 | 5.8 |
| harness | 0.41 | 5.6 |
| runtime dispatch/tail machinery | 0.19 | 2.6 |
| ppu tick / audio / timers / timed DMA | 0.07 / 0.04 / 0.04 / 0.02 | 0.2-1.0 |

* Per private entry now: about 315 ns byte verification, 235 ns native body, 43 ns hook,
  20 ns tail machinery.
* Audio (FIFO DMA, mixer, timers) is about 0.1 ms/frame (1.5%): not a factor; no audio
  was disabled to gain speed.
* PPU emulation by guest code is part of "generated native"; the host compositor is 0.5-0.6
  ms/frame.

## Hot transfers and classification

Top source->target pairs (100 frames, 950,703 transfers; all are tail dispatches, kind
`runtime_dispatch_tail`, every one entering Part 2):

| Source (native fn) | Target PC | Share |
|---|---|---:|
| `afunc_nes_part2_03004D4C` | 03004EF8 | 14.5% |
| `nes_part2_03003A7C` | 03003518 | 14.2% |
| `nes_part2_03003518` | 03004E00 | 14.2% |
| `nes_part2_03004EF8` | 03003A7C | 13.9% |
| next 16 pairs | various Part 2 | 0.7-1.7% each |
| `afunc_03007510 -> 0300753C -> 03007554`, `afunc_03007420 -> 0300742C` (audio/IWRAM stubs) | | 1.1-1.4% each |

* The four-function cycle 03004D4C -> 03004EF8 -> 03003A7C -> 03003518 -> 03004E00 is 56.8%
  of all transfers: the 6502 fetch/decode/execute loop of the emulator, chained by static
  tail branches (each call site has one constant target). Classification: tail dispatch to a
  statically known private target; none are indirect, registered returns, resumes, IRQs or
  hook round trips beyond the one hook call that every tail dispatch makes.
* `runtime_invoke_private_entry` is not the culprit by itself: the same 9.9k invocations cost
  ~2 us each only because the lookup scanned 4,605 rows; they cost ~32 ns after.
* 6502 ratio: guest ARM instructions per native entry is about 11 (108,340 / 9,923). The
  number of emulated 6502 opcodes per frame is **not measured** (no oracle counter); the
  fetch/decode/execute cycle above runs about 1,376 times per frame and NTSC gives 29,780
  6502 cycles per frame, so one cycle iteration is not one opcode at 3 cycles each: the emulator
  evidently batches work. Treat any native-entries-per-opcode figure as unmeasured.

## Root cause

`runtime_invoke_private_entry_in_image(image, pc, mode)` (emitted by `gba_recompile`) walked
`kPrivateDispatchTable[0..7836)` comparing image, PC and mode, returning the first match.
Part 2 entries sit late in the table: about 4,600 rows per call, ~0.5 ns per row, ~2.3 us per
call, ~9,070 calls per frame. Image-handle resolution is **not** repeated: `mzm_ram_dispatch`
resolves each Part's handle once (`handles_ready`). The byte gate is not the bottleneck (the
earlier ablation, -9%, matches 3.07 of 40.7 ms).

## Change

GBARecomp `6198f77` (generic emitter, `tools/gba_recompile/main.cpp`): emits
`kPrivateIndexKey[]` (sorted `image<<33 | thumb<<32 | pc`) and `kPrivateIndexSlot[]` (original row,
stable tie-break) and a binary search that reproduces the first-match semantics; same
fail-closed contract, `g_runtime_resume_pc` handling and PC normalisation. The MZM shards are
byte-identical; only `dispatch_table.cpp` differs. New upstream test
`private_lookup_runtime_tests` (ROM-less fixture, N private Thumb leaves; RED then GREEN):

| Table 4,096 rows | iterations | first-entry ns | last-entry ns | ratio |
|---|---:|---:|---:|---:|
| before (RED) | 1k / 10k / 100k | 33.9 / 19.0 / 17.9 | 1,977 / 2,016 / 2,044 | 58 / 106 / 114 |
| after (GREEN) | 1k / 10k / 100k | 54.8 / 27.2 / 26.7 | 35.9 / 31.4 / 32.1 | 0.65 / 1.15 / 1.20 |

Upstream suite after: 57/57 pass. MZM: `tests/m4/test_perf_index.py` guards the index.

## Before / after (NES title loop, headless)

| Frames | Before | After | Speedup | After ms/frame | After fps |
|---:|---:|---:|---:|---:|---:|
| 100 | 3.91 s | 0.94 s | 4.2x | (loader-dominated) | |
| 1,000 | 39.73 s | 6.59 s | 6.0x | 6.6 | 152 |
| 3,000 | 118.97 s | 18.79 s | 6.3x | 6.3 | 160 |
| 10,000 x2 soak (parallel) | 390.6 s | 64.6 s | 6.0x | 6.5 | |

Real-time target (<16.7 ms/frame) is met headless with 2.5x headroom.

## Correctness and determinism

* `diff` of every `NES3 ` output line (frame hashes at 100/1,000/3,000, audio statistics, SRAM
  hash, entry counters, resolver counters, strict counters, host-stack line) between the base
  and optimized binaries at 100 / 1,000 / 3,000 frames: **identical**.
* CTest `mzm-*` 14/14 (includes `mzm-nes-behavior` qualify with its pinned frame hashes,
  `mzm-nes-save-roundtrip`, `mzm-host-save-persistence`, `mzm-host-savestate`,
  `mzm-host-stack-dispatch`, `mzm-resume-entry`); Python `tests/m4` 49/49 (45 + 4 new);
  upstream 57/57; harness 01/02/03 PASS; 10,000-frame soak x2 identical, strict clean,
  resolver failures 0.
* Host stack: `hook_depth_bytes=446,912` before and after at 100/1,000/3,000 frames; limit 8 MiB
  unchanged. No recursive-stack regression.

## Security contract (unchanged)

Live-byte verification still runs on every private entry (snapshot memcmp with SHA-256
fallback); image identity, mode, private/public separation, yield, IRQ, SoftReset, resume and
strict failure are untouched. Nothing is cached as "verified forever"; only the immutable
dispatch metadata gained an index.

## Candidates considered

| Candidate | Measured cost | Estimated gain | Correctness risk | Scope | Status |
|---|---:|---:|---|---|---|
| **Binary-search private index** | 30.2 ms/frame | 6x (measured) | very low | generic, GBARecomp | **done** |
| Part 2 gate dirty/generation mechanism (skip memcmp when no write hit a gated range since the last verify) | 3.1 ms/frame (45% of residual) | ~1.8x on top | high: must observe CPU stores, DMA, the stack helpers, save-state restore, overlays | MZM + bus hook | not implemented (needs design review; verify_fail=0 so far is not a proof) |
| Same-image direct tail (skip hook + dispatch + lookup) | ~0.1 ms/frame | ~1.15x | high: bypasses the byte gate for the target | generic codegen + MZM | rejected for now |
| Compact tail trampoline / hook bookkeeping | 0.4-0.6 ms/frame | ~1.08x | medium (reentrancy) | generic | not pursued; trampoline is cheap (20 ns) |

## Remaining limit

The residual is dominated by the live-byte gate over Part 2's 20,360 gated bytes (about
315 ns per entry, 42% of the instrumented frame) and by the native 6502-loop body itself. A
safe further speedup needs an audited write-tracking generation for the gated ranges; that is
the next step, not part of this milestone. Headless NES is now real time; real-window
presentation cost (GL upload/present) is unmeasured here.

## Expectation for later releases

The frozen Windows candidate keeps the old generator and is unchanged. A future Beta 3 / RC
built from this branch (corpus regenerated with GBARecomp `6198f77`) should run the NES
about 6x faster on the CPU side; the windowed figure (~37 fps under Xephyr) must be
re-measured on real hardware.
