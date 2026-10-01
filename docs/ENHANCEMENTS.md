# MZM Recompiled — Enhancements (ENHANCEMENTS-1)

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
* Not testable without a GPU/display and therefore not faked: true fullscreen on a real
  monitor, real refresh-rate reporting, VSync pacing on hardware.

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

## Planned (ENHANCEMENTS-2)

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
