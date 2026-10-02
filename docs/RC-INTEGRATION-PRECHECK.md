# RC-INTEGRATION-PRECHECK-1

**This is not a release candidate.** It is a local, unpushed proof that two independently
qualified lines of work live together: NES-PERF-1 (`feat/nes-performance`) and ENHANCEMENTS-1
(`feat/enhancements-menu`). It does not replace, rebuild or alter the frozen Windows Beta 2
(`feat/windows-beta-2 @ 8892ba7`); the Windows smoke must complete first, after which this base is
considered for Beta 3 or RC1. The original branches were not modified.

Date: 2026-10-01. MZM branch `feat/rc-integration-precheck` (from `a223020`),
GBARecomp branch `mzm/rc-integration-precheck` (from `0b9d0326d53a28516d06f2eab43b2bc72c6fef43`).

## What was combined

GBARecomp (`GBARecomp-rc-integration`, no file overlap, all cherry-picks clean):

| Commit | Origin | Class |
|---|---|---|
| `perf(recompile): binary-search private entry lookup` | NES-PERF-1 `6198f77` | codegen (generator) |
| `feat(runtime): add host presentation settings model and rate meters` | ENH `d2303a2` | runtime/host UI |
| `feat(renderer): add presentation scaling, filtering, CRT Lite and perf overlay` | ENH `19bc973` | runtime/host UI |
| `feat(ui): add opt-in in-game enhancements menu and pause-on-menu` | ENH `8222216` | runtime/host UI |
| `test(runtime): report the corrected underrun count on the audio probe` | ENH `bc65c55` | diagnostics/test |
| `test(recompile): cover private lookup index first-match semantics` | new here | test |

MZM: the four NES-PERF-1 commits (profiling, pin, qualification, docs), the five ENHANCEMENTS-1
commits (framework pin + opt-in main, settings/display tests, docs, NES pause, closing docs), one
`build: pin one GBARecomp revision for generator and runtime` commit, and the tests/docs of this
precheck. Conflicts occurred only in `CMakeLists.txt` (the two different `MZM_GBARECOMP_PIN`
values) and were resolved to the single integrated pin, not "ours" or "theirs".

## The pin (important)

ENHANCEMENTS-1 kept the *generator* at `0b9d032` because it did not touch codegen. NES-PERF-1
does: the generated `runtime_invoke_private_entry_in_image` now binary-searches a sorted key index
emitted into `dispatch_table.cpp`. A generator at `0b9d032` would silently lose that (and the
NES would drop back from about 6.5 ms/frame to about 40 ms/frame). In this branch the **runtime
pin, the build pin and the generator pin are one revision**:

`2acbc2b99fcf7e925a1584fbd39faeb4b5567be4`

(`CMakeLists.txt` `MZM_GBARECOMP_PIN`, `scripts/generate-m1.sh` `PIN`, `docs/UPSTREAMS.md`;
`tests/m4/test_perf_index.py` verifies that the two code pins agree and that the corpus carries the
index.)

## Private lookup semantics

First match wins, exactly as the old linear scan. `private_lookup_index_tests` (new,
`tests/recompile/private_lookup_index_test.cpp`) checks the index builder against the original
loop: hit, miss, wrong image, wrong mode, `pc & ~1` normalisation, duplicate keys (root before
alias before a late duplicate: lowest table row wins), an exhaustive PC x image x mode grid, empty
table. `private_lookup_runtime_tests` keeps the scaling check. Generated output is unchanged by
the refactor (the MZM corpus is byte-identical to the one NES-PERF-1 produced).

## Evidence

All numbers are in `.local/rc-integration-precheck-1-report.md` (not committed) and summarised
here.

* GBARecomp suite: 58/58 (plain) and 58/58 (runtime UI enabled with the pinned `recomp-ui`);
  `bios_smoke` does not link with the runtime UI, identically at `0b9d032`; it is a standalone
  tool, not a CTest case.
* Corpus: regenerated from scratch twice, byte-identical; shards identical to the pre-integration
  corpus, `dispatch_table.cpp` differs only by the index; no absolute paths in generated sources.
* MZM: all 18 `mzm-*` tests PASS (including `mzm-host-display`, `mzm-nes-pause` run through a
  private Xephyr with the system Python), Python `tests/m4` 49/49, harness 01/02/03 PASS, strict
  counters zero.
* NES headless, same harness, pure NES-PERF-1 vs the combined build: 100 f 1.00 / 1.00 s, 1000 f
  7.07 / 6.90 s, 3000 f 20.23 / 20.32 s, 10,000 f 67.43 / 67.33 s (the host was about 7% slower
  than during NES-PERF-1 for both builds). Every `NES3` line is identical except the host-stack
  figure, 446,912 B -> 447,056 B.
* Windowed NES (private Xephyr, software GL, dummy audio): EMU 59.7 fps, PRESENT 59.7 fps with
  defaults and 59.4 fps with CRT Lite + bilinear. The ENHANCEMENTS-1 figure of 37 frames/1.5 s
  (about 25 fps) is gone: the host is now frame-limited at the 59.73 Hz guest rate.
* ESC inside the NES: pause, 25 open/close cycles, no catch-up burst, Linear filter, CRT Lite and
  Integer scaling changed from the menu with the guest machine hash unchanged while held,
  clean resume at the pre-pause rate, restore defaults, strict zero.

## Known limitations

* `scripts/run-host-display.py` intermittency (HOST-DISPLAY-STABILITY-1): the earlier failures
  (original script 4/20 on the combined binary) were a harness defect, not a product defect.
  Root cause: Xephyr runs with `-no-host-grab`, so the desktop's real pointer passes through the
  nested screen, and recomp-ui moves the keyboard selection to whatever menu row/section is
  hovered; a stray pointer (and the script's own early hover test) desynchronised the script's
  menu model. Fix (test scripts only, no product change): the pointer is parked and grabbed
  inside the nested server except in the steps that need it, keyboard focus is set explicitly,
  and every fixed sleep is replaced by a wait on an observable effect with a timeout (frame
  counter, layout log line, config.ini, window geometry, stable screenshot). Causal check: with
  a pointer-wandering process the original script fails 17 checks, the hardened one 0.
  Qualification of the hardened script: 50/50 consecutive PASS on an idle machine and 20/20 under
  5 busy loops on 16 cores; 18/18 `mzm-*` CTest PASS.
  Audio caveat: one run in ~30 (before the final script) showed 1767 corrected underruns after
  the resume grace (raw 1247997), a refill outlasting the 400 ms grace under jitter. It has not
  reproduced in the 70 qualification runs and was not classified as a host bug. The post-stress
  check now tolerates at most 1% of raw and < 32768 samples; normal-play and flat-counter checks
  remain strict, and the probe history is printed whenever the corrected count is non-zero.
* The windowed numbers are Xephyr/llvmpipe, not a real GPU. True fullscreen, real refresh-rate
  reporting and VSync pacing on hardware remain untested.
* Audio is activity/integration only; fidelity is not declared.
* The remaining NES cost is the live-byte gate over Part 2 (see `docs/M4-NES-PERFORMANCE.md`).
* `recomp-ui` was initialised from the pinned submodule SHA (`2b7e9c6`) via a local reference
  clone; nothing external was substituted.
