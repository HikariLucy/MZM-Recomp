# MZM Recompiled — Enhancements 2

High-refresh presentation, controller UX and more CRT presets. Presentation and host
UX only: nothing here changes guest semantics. Qualified on top of NES-PERF-1 +
ENHANCEMENTS-1 + HOST-DISPLAY-STABILITY-1 (GBARecomp pin `266f82f`). Not a release
candidate; no Windows run (the frozen Beta 2 line `8892ba7` predates this).

## The one rule

**The guest runs at 59.7275 Hz. Always.** Presentation (what the host shows and how
often) is a separate clock.

* **Native** (default): one present per guest frame.
* **Monitor Refresh**: while the host waits for the next guest frame it re-presents the
  *current* framebuffer at the monitor's refresh rate. A repeated present does not run
  the guest, does not drain or push audio, and never touches machine state. It shows the
  identical pixels again.

**Repeated frames are not interpolated frames, and 144 Hz is not 144 unique gameplay
FPS.** 144 / 59.73 = 2.41, so each guest frame is shown two or three times, irregularly.
That is expected without interpolation and is not hidden by changing guest speed. What
Monitor Refresh improves is host responsiveness: the menu, window changes and pacing on
a high-refresh display. Frame interpolation remains future/experimental work (below).

## EMU vs PRESENT

| Label | Meaning |
|---|---|
| EMU | guest frames per second (PPU frame counter delta) |
| PRESENT | host presents per second (guest-frame presents + repeats) |

`EMU 59.73 / PRESENT 143.9` means the guest runs at its own rate and the host presented
at the monitor's. It does **not** mean "the game runs at 144 FPS". The window title
(`Show FPS`) and the F10 overlay report both; with Monitor Refresh the title also shows
the repeat-present cost and the overlay adds the PRESENT interval, its jitter and the
refresh target, plus a note that the extra frames are repeats.

## Monitor refresh detection

SDL2 `SDL_GetCurrentDisplayMode` of the window's display (what the monitor really runs,
follows fullscreen-desktop), `SDL_GetDesktopDisplayMode` as a fallback. Re-read on open,
fullscreen changes, window move/size and display-changed events, so it follows a window
dragged to another monitor. Any integer in 24..1000 is accepted (60, 75, 120, 144, 165,
240 ... nothing is hard-coded); 0, negative, out-of-range = unknown. Unknown falls back
to Native behaviour. SDL reports integers (143.98 Hz reads as 144). Repeats start at
1.5x the guest rate (about 90 Hz): on a 60 or 75 Hz panel Monitor Refresh resolves to
Native because one present per guest frame already tracks the refresh. Targets are
capped at 360 presents/s.

`GBARECOMP_FORCE_REFRESH_HZ=<n>` overrides the detected value for tests/diagnostics only
(a nested X server reports nothing useful). Evidence gathered with it is synthetic.

## Frame pacing

`FramePacer` still owns the guest deadline. In Monitor Refresh the wait before that
deadline is spent presenting, on a fixed grid of ticks (no accumulated drift) on a
monotonic clock (`steady_clock`; coarse sleep then a 0.4 ms yield tail, no unbounded
spin):

* A repeat is issued only if its predicted end (tick + smoothed measured cost of a
  present, including any blocking swap) lands before the guest deadline, so a repeat
  never delays the guest. The guest then waits for its deadline exactly as before.
* A present that happens close to a tick (the guest-frame present) swallows that tick.
* A stall longer than a few periods resynchronises; there is no catch-up burst.
* Fast-forward, an uncapped pacer and Native bypass all of it.
* Menu/pause path: EMU 0; with Monitor Refresh the held loop presents at the monitor's
  tick instead of the historical ~100 Hz idle. Closing the menu realigns the pacer (no
  catch-up), exactly as before.

**Single-threaded host limit.** SDL_Renderer calls stay on the guest thread, so ticks that
fall while the guest is computing a frame cannot be served. Achieved PRESENT is therefore
the monitor rate minus roughly (compute time / guest period). On a real 144 Hz panel with
MZM the measured result is about 141-143 presents/s. A separate present thread would be the
fix and is out of scope here.

## VSync interaction

| VSync | Presentation | Behaviour |
|---|---|---|
| off (default) | Native | one present per guest frame; FramePacer is the only clock |
| off | Monitor Refresh | software tick grid paces the repeats |
| on | Native | the guest-frame swap blocks on the monitor (historical, can cost frame budget) |
| on | Monitor Refresh | the blocking swap is the clock; its measured cost is included in the guard above, so repeats shrink to what fits before the guest deadline and never delay it |

VSync defaults off for the reason recorded in ENHANCEMENTS-1 (a blocking swap in series
with the pacer cost budget on high-refresh panels). It was not requalified on real vsync
hardware in this milestone.

## Alt+Enter

Alt+Enter (the `Fullscreen` hotkey in `config.ini [KeyMap]`, default `Alt+Return`)
toggles **Windowed <-> Borderless fullscreen**. Edge-triggered: key auto-repeat does not
re-toggle. Persisted to `[Display] window_mode`. Works with the menu open or closed and
never doubles as "accept". Exclusive fullscreen is not part of the toggle (leaving it goes
to Windowed). ESC never changes fullscreen. A latent bug is fixed on the way: with the
shared menu enabled, every key event was swallowed even with the menu closed, so Alt+Enter
never reached the hotkey layer.

## Controller

The menu is operable from a game controller (Guide opens it). Semantic actions over SDL's
controller vocabulary (positions, not glyphs: a Cross/Circle pad behaves like an Xbox pad):

| Input | Action |
|---|---|
| D-pad / left stick | navigate (400 ms initial delay, then every 120 ms; stick with hysteresis) |
| south face (A / Cross) | select / toggle / cycle |
| east face (B / Circle) | back out of a section, then close |
| Start | resume (close) |
| Guide | open / close |

A direction already held when the menu opens does not scroll it. **Input ownership:** while
the menu is open the guest reads "all released"; a button still held when the menu closes
(the B that closed it) stays masked until it is released once, so closing the menu never
presses a game button. With the menu closed the controller maps to the guest exactly as
before. Pad menus without a Guide button can still use the keyboard/mouse to open it.

## CRT presets

`CRT: Off / Lite / Soft / Strong / Custom` plus the 0-100 `Scanline strength` slider.

| Preset | Strength | Profile |
|---|---|---|
| Lite | 40 | plain cosine (the ENHANCEMENTS-1 look, unchanged) |
| Soft | 22 | wider, gentler band |
| Strong | 70 | narrower, crisper lines (peak darkening 35%, never darker than 65%) |
| Custom | the slider | plain cosine |

Moving the slider by hand switches the preset to Custom, so the UI never contradicts
itself. Strength 0 is visually off at every preset. No curvature, no phosphor mask. CRT
stays off by default and needs a window of at least 2x.

## Color profile

`GBA-like (approx.)` is kept and still labelled approximate. It uses the existing
gamma-4.0 GBA colour model; no measured LCD data exists in the repository and no
external matrices were copied, so no claim of accuracy is made. Analysis only; no change.

## Settings and migration

New key `[Display] presentation = native|monitor`; `crt_preset` accepts `lite|soft|strong|
custom`. Missing keys keep defaults, invalid values fall back per key, nothing needs
deleting. A file from ENHANCEMENTS-1 loads unchanged; a named preset whose strength no
longer matches (a hand-tuned slider under `lite`) is read as Custom with that strength; a
preset without a strength means that preset's look.

## Verification

* GBARecomp `presentation_pacing_tests` (fake clock, no sleeps): mode parsing, refresh
  selection (60/75/90/120/144/165/240/500/unknown/absurd), deadline maths over 10-20 s
  simulations (no drift, guest never late, no burst after a stall, costly presents),
  Alt+Enter transitions, navigation repeat/hysteresis, input latch, CRT preset mapping,
  settings round trip and migration, interval jitter, EMU vs PRESENT meters.
* `mzm-host-display` section G (Xephyr, private): Alt+Enter (enter/leave, persistence,
  held key = one toggle, with the menu open); controller via an SDL *virtual* joystick
  (`GBARECOMP_TEST_VIRTUAL_PAD`, no system-wide input device): menu navigation, one row
  per tap, a held stick moves exactly one row, A/B/Start/D-pad, CRT preset cycling; guest
  input ownership read from the guest KEYINPUT trace; Presentation Refresh with the monitor
  forced to 144 Hz (**synthetic / nested-display evidence**): PRESENT rises while the guest
  counter stays at ~59.7 Hz, the machine hash (cycles, IWRAM/EWRAM/VRAM/PAL/OAM) is identical
  across repeated presents, repeated presents show an identical picture, restart persistence;
  ENHANCEMENTS-1 config migration; strict-static at exit.
* `mzm-nes-pause`: CRT Soft and Monitor Refresh inside the NES with the guest hash unchanged.
* Existing checks were not weakened. Updated for the new UI only: the CRT toggle became a
  preset choice (an extra press cycles instead of switching off), two `display_settings`
  test inputs that built an inconsistent "Lite + hand strength" state now say Custom.

## Future: frame interpolation (not implemented)

Nothing here blends frames. A future experiment would sit entirely after the finished guest
frame: it needs two consecutive guest frames (one frame of added display latency, an explicit
and visible cost), an optional motion estimate, and a present stage that is aware of the
repeat schedule above; it must be opt-in, off by default, never touch guest timing/audio/
state and be labelled as synthetic frames. Optical flow, motion vectors, temporal blending
and AI interpolation are all explicitly out of this milestone.

## Known limitations

* Single-threaded presentation (above): PRESENT is below the monitor rate by the guest's
  compute share.
* No Windows run, no real vsync or >144 Hz hardware, no exclusive fullscreen work.
* Controller verified through an SDL virtual joystick, not a physical pad.
* Window size/position are not persisted beyond the existing scale.
* Performance figures: see the report; Xephyr/llvmpipe numbers are software-GL CPU and are
  not comparable with the real-GPU run.
