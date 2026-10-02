# MZM Recompiled — Enhancements (ENHANCEMENTS-1)

> **ENHANCEMENTS-2** (Presentation Refresh, Alt+Enter, controller navigation, CRT Soft/Strong/Custom)
> is documented in `docs/ENHANCEMENTS-2.md`. Where that document and the "Planned" / "120 / 144 Hz" /
> "no gamepad navigation, no Alt+Enter" notes below disagree, ENHANCEMENTS-2 is current.

> **Integration note (RC-INTEGRATION-PRECHECK-1):** on branch `feat/rc-integration-precheck` the framework and
> generator pin is `2acbc2b` (this document's `bc65c55` / `0b9d032` figures are the standalone
> ENHANCEMENTS-1 evidence). See `docs/RC-INTEGRATION-PRECHECK.md`.

ESC during play opens **MZM Recompiled / Enhancements**, a host-side menu with
Display, Graphics and Performance pages. ESC again closes it; ESC never quits the
application. The first release is **presentation only**.

## Two clocks, never mixed

| | EMULATION | PRESENTATION |
|---|---|---|
| What | CPU, PPU, DMA, timers, IRQ, VBlank, audio clock, generated guest code | where and how the finished 240x160 frame is shown: window, rectangle, filter, post-process |
| Rate | the GBA's 59.7275 Hz, always | whatever the host swaps at |
| Touched by Enhancements | **no** | yes |

**Guest timing is not presentation FPS.** A 144 Hz monitor, VSync on or off, a larger
window or CRT Lite never make the game run faster or slower. The only emulation-side
behaviour added is the menu pause (below), which holds the guest still and releases it
without a time jump. Strict-static semantics are unchanged: qualified routes still report
`dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0`.

**No real widescreen yet.** The guest resolution stays 240x160 (3:2). Enhancements never
add pixels the game did not draw; a wider view would need camera/content work and is out
of scope.

## Implemented

* **ESC menu** (keyboard, mouse; shared recomp-ui model). Page memory survives close/open.
* **Pause**: while the menu is open the guest is held (same path as the pause hotkey): the
  window keeps pumping events and re-presenting (about 100 Hz idle), no audio is produced,
  and the frame pacer is realigned on resume so the guest never fast-forwards to repay the
  time spent in the menu. Audio-ring starvation while held is excluded from the underrun
  counter.
* **Scale**: window size 1x-6x (240x160 ... 1440x960), resizable window, **Fit** (largest
  size at 3:2, letter/pillarboxed) and **Integer scaling** (largest whole multiple of
  240x160, centred, never cropped; falls back to Fit if the window is smaller than 1x).
* **Aspect**: Native 3:2 (default) or Stretch (opt-in, distorts).
* **Window mode**: Windowed / Borderless fullscreen (Exclusive exists in the settings model
  and is reachable through the existing fullscreen control).
* **Filtering**: Nearest (default) / Linear, switched live. Nearest + Integer scaling gives
  clean pixel art.
* **VSync**: opt-in (default off, see below).
* **Show FPS**: window title reports `EMU` (guest frames/s) and `PRESENT` (host frames/s)
  as separate numbers.
* **Performance overlay (F10)**: EMU fps, PRESENT fps, display refresh, frame time
  (avg/max), audio underruns, strict-static counters. Debug aid, off by default. (F1-F9
  are save-state slots, hence F10.)
* **CRT Lite** with **Scanline strength** (0-100%, default 40): soft cosine scanlines
  locked to the guest raster, capped so no row is darker than 50%, no blur, no curvature,
  no mask. Needs a window of at least 2x (below that the effect turns itself off).
  Strength 0 is visually identical to CRT off.
* **Color correction**: Off (default) / **GBA-like (approx.)**. Uses the project's
  existing gamma-4.0 GBA colour model; it is an approximation of the handheld look, not an
  emulation of a particular LCD revision.
* **Persistence**: `[Display]` section of the host `config.ini`; other sections (key map,
  ROM/BIOS references) are preserved byte for byte; unknown or invalid values are ignored
  key by key and fall back to defaults; **Restore display defaults** resets and saves. ROM,
  BIOS and SRAM files are never involved.
* Defaults reproduce the pre-Enhancements image exactly (Nearest, Native 3:2, Fit, 3x
  window, VSync off, CRT off, Color Off): the default path is pixel-identical (verified
  by the QA script against the explicit-layout path).

## Verification

* `mzm-display-settings` (CTest, no GPU): defaults, parsing, serialisation round-trip,
  invalid-value fallback, `[Display]` persistence text, Native/Fit/Integer/Stretch
  rectangles, scanline maths, EMU/PRESENT meters.
* `mzm-host-display` (CTest, skipped when Xephyr/python-xlib/Pillow are missing): the
  shipped `MZMRecomp` in a real SDL window inside a private nested X server. ESC open/close,
  pause and resume (frame counter), 25 rapid open/close cycles with no catch-up burst,
  destination rectangles, Linear/CRT/colour pixel checks with exact restore on turning them
  off, overlay, persistence across restart, invalid config, restore defaults, and the
  strict-static counters afterwards.
* `mzm-nes-pause` (CTest, same skip rule): pause/resume inside the real NES, see below.
* Not testable without a GPU/display and therefore not faked: true fullscreen on a real
  monitor, real refresh-rate reporting, VSync pacing on hardware.

## Closing qualification (ENHANCEMENTS-1)

**GBARecomp upstream suite** at framework `bc65c55` (the pinned head): **56 / 56 pass,
0 fail, 0 not run**, in both the plain configuration and the one built with the runtime UI
(pre-Enhancements baseline: 56/56). Covers tail dispatch, FIFO DMA, multi-image,
image-scoped CFG, private relocation, conditional PC load, return continuation, runtime
tests. (The standalone tool `bios_smoke` does not link when the runtime UI is enabled
without a host application; this happens identically at `0b9d032` and is not a test.)

**NES pause/resume** (`scripts/run-nes-pause.py`, no guest memory written): the qualified
harness plays the real route (title, START, gameplay input) and serialises the machine at
guest frame 2000 through the framework's own save-state container (test-only
`MZM_NES_SAVE_STATE`); the shipped windowed host loads it and the player's route (ESC menu,
pause hotkey) is exercised inside the NES.

| | frames / 1.5 s | notes |
|---|---|---|
| before the hold | 37 | the windowed NES runs below 60 Hz (known, separate limitation) |
| held (menu), 25 holds | 0 (0 over all holds) | IWRAM/EWRAM/VRAM/PAL/OAM hash identical across a 1.5 s hold; menu still drawn |
| after the holds | 37 | no catch-up burst (366 frames in the run, ceiling 971) |
| Shift+P hold | 0 | resumes at the same rate |

Audio (dummy SDL driver): bridge pushes continue after the cycles; the corrected
underrun counter stays 0 and flat while the raw bridge counter (about 1.4 M, all during the
intentional holds) is excluded; MZM (`mzm-host-display`) behaves the same. The counter's
sensitivity to a *real* underrun was not demonstrated (none could be induced). Strict
counters at exit of a second session with the same route and 10 cycles: all zero.

**Default overhead.** The earlier "about 3%" came from comparing against a differently
built binary. With MZM sources, compiler flags and recomp-ui held identical and only the
framework changed (`0b9d032` vs `bc65c55`, defaults, menu closed, CRT off, FPS off), the
CPU cost over 1800 windowed frames is indistinguishable from run-to-run noise: 7 interleaved
runs each, mean 13.54 s vs 13.76 s (+1.6%, spread about 1 s); headless (no present path)
5 runs each, 4.77 s vs 4.82 s. No component was isolated because none is measurable. The
per-frame default path was audited: a settings-struct copy and one layout compare in
`present()`, one compare in `note_emulated_frames`, one menu-open check in the pause loop; no
INI parsing (it happens only on an accepted menu change and at start), no allocation,
string formatting, shader work or GL state change while everything is off.

**CRT Lite cost.** Mean 19.63 s vs 13.90 s for the baseline in the same round (about +5.7 s
of CPU over 1800 frames, roughly +3 ms/frame) under software GL (llvmpipe in a nested X
server). That is a software-rasteriser figure; it says nothing about a GPU.

## Known limitations
* NES throughput in the windowed host is below real time (about 37 fps here); unchanged by
  Enhancements.
* No Windows run, no real fullscreen/VSync/high-refresh run, no gamepad menu navigation, no
  Alt+Enter, no measured LCD profile (all ENHANCEMENTS-2).
* Audio verified with the dummy driver only.
* Performance figures are software-GL CPU time in Xephyr.

## Experimental / known limits

* **VSync defaults off.** A blocking monitor vsync in series with the frame pacer measurably
  cost frame budget on high-refresh displays. It does not change game speed.
* **CRT Lite cost.** Scanlines are drawn at presentation resolution; under software GL it
  costs a few CPU percent more than the plain path (measured in the report). Not a concern
  on a GPU-backed renderer, unmeasured there.
* **Windows**: the code is plain SDL2/OpenGL with no platform calls, but this feature has
  **not** been run on Windows. The frozen Beta 2 package predates it.

## 120 / 144 Hz presentation (analysis, not implemented)

Decoupling presentation from the guest is feasible without touching emulation: the paused
path already re-presents the last framebuffer at an independent rate. Presenting the same
frame repeatedly while the guest advances at 59.7 Hz would need the pacer's sleep sliced
into present ticks. Without interpolation this brings **no smoother motion** (the guest
only produces 59.7 distinct frames per second); the possible gains are lower display
latency for host UI and tearing control. Doing it correctly also needs a measured monitor
refresh (shown on the overlay as `display n/a` when unknown) and a pacing test on real
high-refresh hardware. It is therefore left as ENHANCEMENTS-2 work behind a
"Presentation: Native / Monitor Refresh" option. The guest, VBlank, timers and audio clock
must stay on the 59.7275 Hz emulation clock in every case.

## Planned (ENHANCEMENTS-2: done except interpolation and a measured colour profile)

* Presentation refresh option (above) with a hardware pacing qualification.
* Frame interpolation (experimental; needs motion vectors or a two-frame blend with an
  explicit latency cost; never default).
* A measured colour profile (colorimeter data) before claiming anything beyond "approx.".
* Gamepad navigation of the menu, Alt+Enter, more CRT presets (mask/curvature opt-in).

## Non-goals

* Changing guest timing, VBlank, DMA/IRQ timing, audio clock, or generated code.
* Real widescreen or any content/camera change.
* External shaders with unclear licences.
* A claim of "GBA LCD accurate" colour.
