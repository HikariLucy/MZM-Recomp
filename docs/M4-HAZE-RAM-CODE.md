# M4 haze RAM code qualification

Audit date: 2026-09-28. Scope: MZM USA BMXE revision 0, verified local ROM
SHA-1 `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8`. No ROM bytes are
committed. The official GBARecomp pin remains unchanged.

## Layout and control flow

`gNonGameplayRam` begins at `0x030016C4` in the decomp map and generated
symbol map. `struct InGameData` starts with `clipdataCode[640]` (`0x280`
bytes), then `hazeCode[512]`; therefore haze starts at **`0x03001944`**,
is 4-byte aligned, and has a **`0x200`-byte** window. `HazeSetupCode` uses
DMA3 in 16-bit mode to copy `sizeof(hazeCode)` from a ROM function entry.
It sets `gHazeProcessCodePointer` (stored at `0x0300572C`) to
`hazeCode + 1 = 0x03001945`, a Thumb function pointer. The central
`runtime_dispatch` strips bit 0 before invoking `RuntimeRamDispatchHook`, so
the hook receives aligned `0x03001944` with `thumb=1`. This is confirmed by
the runtime source and a structural test; no real haze call was captured yet.

| Source function | ROM start | Function size | DMA copy | Runtime destination / mode | HAZE_VALUE / room effect | `HazeProcess` path |
|---|---:|---:|---:|---|---|---|
| `Haze_Bg3` | `0x0805D768` | `0xC0` | `0x200` | `0x03001944` Thumb | BG3; water, lava, weak/strong acid | RAM pointer call |
| `Haze_Bg3StrongWeak` | `0x0805D828` | `0x118` | `0x200` | `0x03001944` Thumb | BG3_STRONG_WEAK; lava heat haze | RAM pointer call |
| `Haze_Bg3NoneWeak` | `0x0805D940` | `0x70` | `0x200` | `0x03001944` Thumb | BG3_NONE_WEAK; heat BG3 haze | RAM pointer call |
| `Haze_Bg3Bg2StrongWeakMedium` | `0x0805D9B0` | `0x90` | `0x200` | `0x03001944` Thumb | BG3_BG2_STRONG_WEAK_MEDIUM; heat BG2/BG3 haze | RAM pointer call |
| `Haze_Bg3Bg2Bg1` | `0x0805DA40` | `0xEC` | `0x200` | `0x03001944` Thumb | BG3_BG2_BG1; BG1/BG2/BG3 haze | **Direct ROM call** after RAM copy |
| `Haze_PowerBombExpanding` | `0x0805DB2C` | `0x118` | `0x200` | `0x03001944` Thumb | POWER_BOMB_EXPANDING; gameplay action | RAM pointer call |
| `Haze_PowerBombRetracting` | `0x0805DC44` | `0x118` | `0x200` | `0x03001944` Thumb | POWER_BOMB_RETRACTING; expansion completion | RAM pointer call |

The function sizes come from `arm-none-eabi-nm -S mzm_us.elf` and the
imported function boundaries; all seven native targets `gf_Haze_*` exist in
`generated/recompiled.h`. The fifth copy is real, but its RAM image is not
executed by the decomp's `HazeProcess`: it calls `Haze_Bg3Bg2Bg1()` directly.
Both Power Bomb stages use the same destination in sequence; a PC-only map
would not distinguish them.

## Dispatch strategy and position independence

`[[code_copy]]` maps one ROM source onto a fixed RAM span. Adding seven
overlapping declarations cannot make a PC-keyed dispatch table select the
current byte image. The existing `RuntimeRamDispatchHook` runs before the
static table for RAM PCs and already byte-verifies two transient stack SRAM
helpers. This MZM-specific hook is the appropriate entry seam; GBARecomp
changes are **none**.

`mzm_haze::identify` accepts only aligned `0x03001944` in Thumb mode and
compares all **512 live RAM bytes** against each immutable 512-byte ROM source
window. It rejects a changed byte, wrong address or ARM mode. Comparing only
the function size would ignore bytes DMA really copied; a short prologue
would be too weak. The seven windows are distinguishable in the local ROM.
The hook dispatches to the corresponding generated native function after a
full match. The old high-IWRAM stack-helper path remains separate.

The audited Thumb disassembly contains only PC-relative literal loads as
direct PC uses: all target words lie inside each copied window. Per variant
literal-load counts are 9, 12, 5, 5, 6, 6, 6 in table order. There are no
ADR instructions or BL calls in these seven bodies; branches stay within
the function and each exits via BX. Thus source-ROM translation sees the same
literal **values** as the RAM copy for ordinary synchronous execution. This
is `SAFE TO CANONICALIZE` for those function-level data/control effects in
all seven cases (the fifth remains called from ROM by MZM).

The generated functions nevertheless report ROM PCs while running. Precise
RAM-PC identity during an interrupt, mid-function yield/resume, traces and
instruction-fetch timing is **not qualified** by that functional audit.
Those routes may need runtime-PC translation or a real scene comparison.
Consequently, variant identification is PASS, but strict-static haze scene
execution is UNVERIFIED. No hardware-accurate timing claim follows.

## Tests and local ROM qualification

`mzm_haze_resolver_test` uses synthetic source windows for seven positive
mappings plus wrong Thumb pointer/alignment, wrong PC, ARM mode, unrelated
stack RAM, and a corruption at the final copied byte. It runs without a ROM
as a CTest. The same executable with `--rom <local-USA.gba>` copies each
actual 512-byte source window into its test RAM view; all **7/7** selected
the expected template and the negative cases were rejected. It tests the
resolver, not full native function execution or guest CPU state.

The M4 candidate build against the separate dynamic WAITCNT worktree passed
`01_boot_headless`, `02_static_120_frames`, and
`03_initialize_game_timing`, all with zero strict-static error counters.
These cases do not activate haze. The existing launcher CTest in `build-m1`
passed 1/1, the new resolver CTest passed 1/1, and the Python M4 harness
tests passed 5/5. No `04_haze_ram_dispatch` case or
RED→GREEN scene comparison was added without a reproducible scene.

## Scene search and next qualification

The decomp's `sHazeData` maps `EFFECT_WATER`, `EFFECT_LAVA`,
`EFFECT_WEAK_ACID` and `EFFECT_STRONG_ACID` to BG3. Examples in
`rooms_data.c` are Crateria room 7 (water), Brinstar room 12 (weak acid),
and Norfair room 1 (lava). Norfair room 10 selects the StrongWeak lava-heat
variant. The heat BG3, heat BG2/BG3 and three-layer effect values occur in
`sHazeData`, but no matching static `rooms_data.c` entry was found in this
audit; dynamic selection remains possible. Power Bomb stages are gameplay
actions, not fixed room effects. No local checkpoint for any of these
scenes was available, and no save was fabricated.

**Next gate:** capture a legal local checkpoint in `.local/m4-checkpoints/`
for an early BG3 room, record its room/state and prove a hook hit with zero
strict-static misses/interpreter instructions. Then compare the same scene
without the haze hook for a real RED→GREEN dispatch result. A later Power
Bomb checkpoint should prove expansion→retraction swaps in one buffer.
Power Bomb swap and full haze function execution remain UNVERIFIED.
