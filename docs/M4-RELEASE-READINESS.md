# M4 release readiness (M4-RELEASE-AUDIT-1, 2026-09-30)

Evidence-driven audit of everything that is not yet `PASS` and of the reproducibility of
what is. Scope: USA rev 0, branch `feat/m4-compat-harness`, GBARecomp pin
`0b9d0326d53a28516d06f2eab43b2bc72c6fef43`. Nothing was pushed or merged; the frozen
Windows Beta 2 artifact was not touched (see the Windows section).

## Verdict

All Linux-demonstrable gates are green on a from-scratch build. The only P0 item left
before a release candidate is the **real Windows runtime smoke**. Full-game compatibility,
audio fidelity and hardware equivalence are **not** claimed and cannot be closed without a
human playthrough, an audio/pixel oracle or hardware.

## Compatibility matrix

Validated by `scripts/check-m4-matrix.py` (strict table parser, unit-tested by
`tests/m4/test_matrix.py`). Thirteen rows lacked the closing `|`, which is what broke the
earlier `awk` counts; the document's own count (44/24/1/10) was right, not the 78/43
figure quoted from memory.

| | total | PASS | PARTIAL | BLOCKED | UNVERIFIED |
|---|---:|---:|---:|---:|---:|
| before | 79 | 44 | 24 | 1 | 10 |
| after | 79 | 51 | 17 | 1 | 10 |

Rows changed (every change has a stated reason in the matrix itself):

| Row | Old | New | Evidence |
|---|---|---|---|
| Save states | PARTIAL | PASS (route-scoped) | `mzm-host-savestate`: save/load through the shipped host, exact memory restore, same end state as a straight run, `--load-state` PNG identical |
| NES Emulator Part 2 | PARTIAL | PASS (route-scoped) | 30,764,157 verified native entries in the scripted session, `invoke_fail=0`, `no_corpus=0`; per-part minima now pinned in `mzm-nes-behavior` |
| NES Emulator Part 3 | PARTIAL | PASS (route-scoped) | 15,806 entries across title/menu/gameplay, `verify_fail=0` |
| NES Emulator Part 4 | PARTIAL | PASS (route-scoped) | 2 boot entries, overlay fails closed (unit-tested for every part), no re-entry in 9,000 frames |
| NES Emulator Part 5 | PARTIAL | PASS (route-scoped) | 21,784 entries incl. menus, game over/password/save and the quit |
| NES Emulator Part 6 | PARTIAL | PASS (route-scoped) | 21,421 entries incl. `SaveToSram`, `LoadFromSram`, password and stack helpers |
| NES SRAM ownership / persistence backend | PARTIAL | PASS (route-scoped) | `mzm-host-save-persistence`: real NES-written SRAM through the shipped host's `.sav` load/flush, then a NES `CONTINUE` from the flushed file |

Still `BLOCKED`: Game Pak prefetch (needs a shared dynamic fetch-cost seam and a cycle
oracle in GBARecomp). Still `UNVERIFIED` (10): real haze scene, Power Bomb swap, three
Chozodia scene/callback rows, bosses, endings, Zero Suit, Fusion Link, Europe. Remaining
`PARTIAL` (17) are oracle-, hardware- or playthrough-bound, or have no isolated trace yet
(see the table in the matrix).

## Reproducibility (clean build)

* GBARecomp built from scratch in a new build directory at the pinned revision: 56/56
  upstream tests (tail dispatch, FIFO DMA, multi-image, image-scoped CFG, private
  relocation, conditional PC load, return continuation, `returns = false`).
* The MZM corpus and the BIOS corpus were generated twice from the normal inputs
  (`generate-m1.sh`, `generate-bios-m2.sh`): byte-identical trees (tree SHA-256
  `b98a081e48181057789a738cb6bf99bbf4b9a293295be1b6981de5d345ad508d`), also identical to the
  corpus produced before this audit. No `/home/`, `/tmp/`, `.local`, `.elf` or
  drive-letter strings in the generated corpus or in the expanded configs.
* MZM built from that corpus (CLI host, and a second build with the launcher UI).
* `scripts/generate-m1.sh` and `scripts/generate-bios-m2.sh` default `GBARECOMP_ROOT` to
  developer worktrees; maintainers must pass `GBARECOMP_ROOT`/`GBARECOMP_BUILD` (the pin
  check fails loudly otherwise).
* `recomp-ui` is a git submodule; in a checkout where it is not initialised the build
  silently falls back to a CLI-only host and the `mzm-launcher-state` test is not
  registered. Initialise the submodule for a release build.

## Test results (one clean build, final tree)

| Suite | Result |
|---|---|
| GBARecomp CTest | 56 / 56 PASS |
| MZM CTest (`mzm-*`) | 14 / 14 PASS (two new tests) |
| MZM tree CTest, all 68 registered | 14 PASS, 53 Not Run, 1 Failed: the 54 are upstream tests registered by `add_subdirectory` but not built in the MZM tree; they ran in the upstream build above |
| `mzm-launcher-state` | PASS (UI build only; not registered without the submodule) |
| Python (`tests/m4`) | 45 / 45 PASS |
| Harness 01 / 02 / 03 | PASS / PASS / PASS |
| Soak, 2 x 10,000 frames (no input) | PASS: strict clean, host/IRQ depth 0, every reported line identical between runs |
| Matrix parser | valid |

## Performance (headless harness, RelWithDebInfo, one desktop core, wall time)

| Scenario | Frames | Wall | RSS | Host-stack high-water |
|---|---:|---:|---|---|
| NES title loop, no input | 100 / 1,000 / 6,000 / 10,000 | 3.9 s / 39.2 s / 235 s / 391 s | 26-27 MB | 446,912 B at every length |
| NES scripted play, save, quit, MZM reboot | 1,000 / 3,600 / 6,000 / 9,000 | 39.1 s / 120.8 s / 129.5 s / 147.2 s | 26-43 MB | 446,912 B, then 459,072-459,168 B after the reboot |
| MZM only (shipped binary, headless) | 100 / 1,000 / 3,000 / 6,000 | 0.5 s / 2.7 s / 8.7 s / 21.8 s | 29-57 MB | n/a |

* MZM alone runs 3-3.6 ms/frame (about 5x real time).
* **The NES runs at about 35-39 ms/frame in this harness (about 26-29 frames/s, 2.1-2.3x
  slower than the 16.74 ms frame time).** Ablation: skipping the byte gate entirely saves
  only about 9%, so the cost is the native call path itself (about 10,000 hook entries
  per frame at about 3.5 us each), not verification. It could not be attributed further
  (profiler and debugger attach are blocked on this host). This is a performance, not a
  correctness, finding; it is unmeasured in the shipped binary because the NES cannot be
  entered from the real game without a completed game.
* After the NES quit, Part 2's fail-closed gate rejects 1,100-1,750 entries per frame
  (8,303,618 `verify_fail` by frame 9,000, `invoke_fail=0`, `no_corpus=0`); MZM frames then
  cost about 5.3 ms in the harness against about 3.3 ms without the NES, so the rejection
  path costs about 2 ms/frame and is not worth optimising.

## Audio (activity PASS, accuracy PARTIAL, unchanged)

Measured, not listened to. NES title loop, 10,000 frames: 10,965,128 host samples,
10,064,378 non-zero, 334 one-second windows all non-constant, peak -13,440..15,360, RMS
4,608, no clipping, 76 distinct values, 2,982,338 sample changes, FIFO A only. Full chain
(NES, then MZM), 6,000 frames: 6,564,046 samples, 4,866,918 non-zero, 187/200 windows
non-constant, peak +-32,767 with 4 clipped samples (49 by frame 9,000), 325 distinct
values, FIFO A and B both active after the MZM reboot, mean -15..-236 (no host DC offset on
MZM audio). The shipped host, run windowed under SDL dummy drivers in real time for 44 s,
reported `bridge_underrun=0`, `overflow_drops=0`. Limits: output is mono at 65,536 Hz by
GBARecomp design (left/right panning collapses); the real audio device was not exercised;
nothing here proves musical correctness. No oracle, so no fidelity PASS.

## Save / load

SAVE-LOAD-1 re-run and green. New: the shipped host's `.sav` path is exercised with a
real NES-written image (see the matrix row); the shipped host reports
`save_loaded 32768/32768` and `save_flushed`, the NES region survives byte for byte.
Same-process, GbaSave process-restart and host-disk persistence are now three separate
passing paths. Not covered: the shipped host *writing* a NES save (unreachable) and a save
with real game progress (the scripted run dies before collecting an item).

## Host stack

446,912-459,168 B at every length from 100 to 10,000 frames, against 8 MiB (Linux default)
and 16 MiB (Windows PE reserve). The residual (direct known-target `b` cycles in Part 5,
`name(); return;`) is bounded by the VBlank yield: technical debt, not a correctness risk,
unless a guest loop without a yield contains that pattern (none found). No GBARecomp
change proposed.

## Fail-closed audit

* Every dispatch hook path returns "not handled" on a byte mismatch, an unmatched image or
  a missing private entry, and the runtime then raises a strict dispatch miss (verified by
  the scope-policy tests and by the 8.3 M fail-closed Part 2 rejections with zero
  mis-invocations).
* Strict mode rejects `GBARECOMP_FORCE_INTERP`. The launcher sets
  `GBARECOMP_STRICT_STATIC=1` only when the user presses PLAY. **Direct invocations of the
  host binary (`--rom ...`, `--no-launcher`, `GBARECOMP_NO_LAUNCHER`) run non-strict**
  (GBARecomp default: self-heal compile on, interpreter bridge allowed but logged).
  That is a product decision to record, not a defect found in the gates.
* **Headless / TCP / input-replay runs unwind at VBlank; the shipped windowed path presents
  in place** ("frame-boundary resume misses eliminated structurally"). Consequence: a
  scripted START at the MZM title in the headless/replay path hits a strict miss at
  `0x08003286` (interior of `DmaTransfer`, after the DMA enable store; the unit that
  M4-RESUME-2 audited and did not publish). The same route in the windowed host ran 4,456
  frames strict-clean. So the resume units matter for the test harness, not for play;
  adding `DmaTransfer` is the next harness-only unit if a headless route needs it.

## Environment flags

Production: `GBARECOMP_STRICT_STATIC` (set by the launcher), `GBARECOMP_NO_LAUNCHER`,
`MZM_ROM`/`MZM_BIOS` style build inputs. Diagnostic in the shipped binary:
`MZM_TRACE_RAM_DISPATCH`, `MZM_DISABLE_HAZE_RAM_DISPATCH` (makes haze fail closed),
`MZM_M4_CAPTURE_FIRST_HAZE`, `MZM_MILESTONE_TRACE`, `MZM_LAUNCHER_*`. Test-only: all
`MZM_NES_*`. Upstream GBARecomp exposes about 100 `GBARECOMP_*` variables (force-interp,
BIOS HLE, self-heal, idle elision default ON with cycle credit, ...); strict mode
overrides the dangerous ones that matter (force-interp is rejected). No `DIAG_FORCE`
variable remains (`MZM_NES_DIAG_FORCE_FIFO32` is gone). No TODO/FIXME/HACK/XXX/WORKAROUND
in `src/`, `scripts/`, `tests/`, `configs/`; the only DIAG hits are test-only
(`MZM_NES_DIAG_FRAMES`, `MZM_NES_AUDIO_DIAG`).

## Artifact / legal hygiene

Tracked files (112 at the start of the audit): none ROM, BIOS, payload, capture, screenshot or save data; no byte
arrays or long hex strings (hashes only); icons are the project's own. Absolute developer
paths remain in `docs/M4-HAZE-RAM-CODE.md` and `docs/VISUAL-IDENTITY.md` (documentation
only). The packaged `configs/mzm-us.toml` carries the build-time
`path = ".local/nes-payload-usa.bin"` string (relative, inert at runtime).

## Windows package static audit (read-only)

`MZMRecompiled-Beta-2-Windows-x64-RUNTIME-UNVERIFIED.zip` SHA-256
`d6ac634bfb760da545f401a9721a76e6eaf701447f97bc6c94a39f58a9f82b55` (matches), branch
`feat/windows-beta-2` at `8892ba7`, worktree clean. 38 entries; PE32+ x86-64 for
`MZMRecomp.exe` and the three DLLs; `MZMRecomp.exe` SHA-256 equals `BUILD-INFO.txt`; BUILD-INFO
records MZM `8892ba7`, GBARecomp `0b9d032`, PE stack reserve 16 MiB, `Windows runtime
smoke: PENDING`; no ROM, BIOS, `.sav`, source or generated dump in the archive; no developer
path in the executable. The Windows runtime itself remains **PENDING**.

## Findings that need a decision

1. NES throughput is below real time in the harness (about 27 fps).
2. Direct host invocations are non-strict (launcher only enforces strict).
3. The headless/replay path needs `DmaTransfer` as a resume unit for a START-at-title route.
4. `recomp-ui` submodule uninitialised in this worktree silently drops the launcher build and test.

## Roadmap to RC

**P0 (blocks Beta 2 / RC)**

* Real Windows smoke of the frozen ZIP (launcher, audio device, stack, NES entry); record
  the NES frame rate there. If the NES is visibly below full speed on the tester's machine,
  item P1-1 becomes P0 for the NES claim.

**P1 (before 1.0)**

1. NES per-call cost: profile `runtime_invoke_private_entry_in_image` (about 10,000
   calls per frame) and the hook; upstream write-epoch/caching if it dominates.
2. Decide and document strict-by-default for direct host invocations.
3. Automate a scripted route to gameplay and a Save Room write through the shipped
   windowed (present-in-place) path, with `GBARECOMP_AUDIO_PROBE` underruns, to replace
   the six manual historical PASS rows' evidence.
4. Initialise `recomp-ui` in release builds and make a missing submodule a configure error
   for release.
5. Real haze BG3 and Chozodia checkpoints (manual capture), then the Power Bomb swap.
6. Broader playthrough evidence (areas, bosses, ending, Zero Suit) and an audio oracle or an
   explicit "audio fidelity unverified" release note.

**P2 (after 1.0)**

* Game Pak prefetch (BLOCKED), Europe corpus, Fusion Link, rewind qualification, isolated
  traces per executable-RAM copy, `DmaTransfer` resume unit for the harness, stereo output
  upstream.
