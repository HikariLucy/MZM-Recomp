# M4 next target: qualify PPU mosaic

## Problem

The pinned GBARecomp scanline renderer does not apply BG or OBJ mosaic even though MZM writes `REG_MOSAIC` and uses mosaic sprite states. This prevents a visual-accuracy claim for affected scenes.

## Evidence

Pin `e7728148c6829ba526f682876430a0c9022dc6c0` is clean. Its `GBA_ACCURACY_BURNDOWN.md` lists mosaic as missing. `src/gba/gba_ppu.cpp` has no mosaic register read or BG/OBJ mosaic sampling; diagnostic register output is observational only. MZM relevance is documented in `docs/M0.6-HARDWARE-MATRIX.md`. No MZM scene/pixel reference is yet captured, so the next task is qualification, not a claim of a corrected renderer.

## Ownership

Generic rendering belongs in GBARecomp upstream: `src/gba/gba_ppu.cpp`, with deterministic coverage in `tests/ppu_smoke/test_main.cpp`. MZM-Recomp owns a local case/checkpoint and comparison against a trusted GBA reference for an actual affected scene. No MZM-only graphics override is proposed.

## Proposed fix

First create a small upstream PPU fixture for BG mosaic and OBJ mosaic, covering horizontal and vertical sizes, enable bits, and boundary behavior. Confirm it fails at the current pin. Capture one MZM scene that writes nonzero MOSAIC and retain its local checkpoint under `.local/m4-checkpoints/`. Then propose an upstream patch that samples the correct mosaic source coordinate before tile/affine/OBJ fetch; keep normal rendering unchanged when mosaic is disabled. Do not implement that upstream patch in this workstream yet.

## Validation plan

1. Inspect MZM's `REG_MOSAIC` writes and select a reachable early effect; if none is reachable without long play, qualify the generic fixture first and leave MZM scene UNVERIFIED.
2. Record a trusted reference frame, MOSAIC register state, and local checkpoint provenance. Store ROM-derived state outside Git.
3. Run the same checkpoint with the M4 harness under strict-static enforcement, confirm zero counters, and compare relevant pixels/regions with the reference.
4. Apply any proposed generic fix in a separate upstream workstream, rerun upstream PPU tests and the MZM checkpoint, and review visual differences.

## Risk

Scanline-latched register state may not reproduce mid-scanline changes; affine BG and OBJ-window interactions can add edge cases. A passing synthetic PPU test alone would not qualify MZM visuals. The observed raw `--steps 1000` dispatch miss remains separate static-coverage debt.
