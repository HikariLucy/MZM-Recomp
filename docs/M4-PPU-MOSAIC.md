# M4: PPU MOSAIC investigation

## Register and enable semantics

`REG_MOSAIC` (`0x0400004C`, write-only) encodes BG H in bits 0–3, BG V in 4–7, OBJ H in 8–11, and OBJ V in 12–15. Each nibble stores the block dimension minus one, so zero means a 1-pixel dimension. A BG layer uses mosaic only when BGxCNT bit 6 is set. An OBJ uses it only when OAM attribute 0 bit 12 is set. BG and OBJ sizes are independent. Pixel blocks sample their upper-left pixel; horizontal block origins align to display columns 0, size, 2×size. Vertical groups similarly start at scanline 0. For a nonaffine BG, sample the block-origin screen coordinate before scroll, tile/map lookup and tile flips. For affine BG, transform the block-origin coordinate, rather than rounding the transformed texel. For OBJ, determine the block-origin screen coordinate before sprite flips or affine transform; an object whose anchor texel is transparent remains transparent for that block.

Sources: [GBATEK mosaic register](https://rust-console.github.io/gbatek-gbaonly/#lcd-io-mosaic-function), [GBATEK BGxCNT and OBJ attributes](https://rust-console.github.io/gbatek-gbaonly/), [Tonc mosaic explanation](https://gbadev.net/tonc/gfx.html), [Tonc BGxCNT table](https://gbadev.net/tonc/regbg.html), and the [mGBA BG](https://github.com/mgba-emu/mgba/blob/master/src/gba/renderers/software-bg.c) and [OBJ](https://github.com/mgba-emu/mgba/blob/master/src/gba/renderers/software-obj.c) renderer code as a cross-check. Tonc's mosaic prose says BGxCNT bit 7, but its register table says bit 6, matching GBATEK and MZM's `BGCNT_MOSAIC (1 << 6)`; the prose is treated as a typo. Tonc warns that historical emulators differed in mosaic output, so synthetic tests should assert register semantics and concrete pixels, not copy an emulator screenshot.

## Renderer ownership and actual gap

The pinned `e7728148` GBARecomp checkout stores MOSAIC in generic IO state and exposes it through diagnostics, but `src/gba/gba_ppu.cpp` never reads `io[0x4C..0x4D]` to render it. The faithful renderer's `render_scanline_internal` produces text BG pixels in `render_regular_bg`, affine BG pixels in `render_affine_bg`, bitmap BG pixels in `render_bitmap_bg`, then OBJ pixels in the OAM loop. All submit candidates with a layer/priority key; window masks are evaluated for the **destination** pixel, and final blending follows candidate ordering. Mosaic sampling belongs before candidate submission; applying it to the final RGB frame would incorrectly mix layers, windows and blends. OBJ-window stencil sampling is a separate earlier path, `mark_obj_window_scanline`, and must be audited with the same rule.

The extended-view renderer has a separate `render_scanline_wide` path. Its native 240×160 center must remain compatible with the faithful renderer, but extrapolated margins are a presentation extension, not GBA hardware. The initial qualification covers native width; margin behavior requires its own assertion before claiming enhanced-view parity.

## Test and implementation strategy

Create redistributable fixtures in upstream `tests/mosaic/test_main.cpp` with synthetic IO, VRAM, palette and OAM. Assert enabled/disabled BG and OBJ H/V, size 1×1, mixed dimensions, affine BG, and a priority/blending interaction. Record RED failures on `e7728148` before editing `gba_ppu.cpp`. Read MOSAIC once per scanline and choose source screen coordinates for the enabled layer before tile/affine/OBJ fetch. Keep window control and candidate submission at the original destination coordinate. Test exact no-mosaic equality and run full upstream tests. No MZM addresses or game assets belong in the fix.

OBJ overlap/transparency, mid-scanline register writes, and unusual affine cases need explicit tests or a stated qualification limit. A synthetic PPU PASS does not qualify any MZM scene. Local MZM scene/checkpoint evidence is tracked separately below.

## RED and GREEN evidence

At base `e7728148`, before the renderer change, 9 of the initial 12 synthetic cases failed on exact pixel assertions: BG horizontal/vertical, OBJ horizontal/vertical, mixed sizes, affine BG, OBJ H flip/affine, and blend after mosaic. BG disabled, OBJ disabled, and 1×1 equivalence passed. The separate OBJ-window case was added next and failed on its stencil pixel before that path was changed; bitmap BG passed once the general BG fix was in place. These failures show that the tests exercise the missing feature, while the disabled cases guard old behavior.

The local upstream branch `mzm/ppu-mosaic` reads MOSAIC once per scanline. Text BG samples the mosaic origin before scroll/tile lookup; affine and bitmap BG use the origin before coordinate transformation. Normal and affine OBJ, including OBJ-window stencils, use the origin before flips or affine transform. Destination coordinates still select windows, priorities and blend targets. BGxCNT bit 6 and OBJ attribute 0 bit 12 gate the behavior independently; zero nibbles preserve 1×1 output. No game-specific address or asset is used. `ctest --test-dir build-mosaic --output-on-failure` passed 48/48, including all 14 MOSAIC cases and the existing PPU smoke test. `git diff --check` passed.

The faithful native 240×160 renderer is qualified by these tests. The extended-view margin renderer is a separate presentation path and has no MOSAIC pixel assertions yet. Mid-scanline MOSAIC writes and overlapping OBJ edge cases have no hardware capture here; the current tests validate the listed register, sampling, and composition semantics, not exhaustive hardware equivalence.

## MZM usage and scene qualification

The MZM decomp writes `REG_MOSAIC` in `src/in_game.c:423` from `gWrittenToMosaic_H/L` and in pause VBlank at `src/menus/pause_screen.c:2166`; pause exit clears it. The two globals initialize to zero. A source/assembly search found no nonzero assignment and no use of `BGCNT_MOSAIC` or `OBJ_MOSAIC` in game sources. `SPRITE_STATUS_MOSAIC` appears in Ridley, Kraid, Metroid, Rinka, Space Pirate, and other AI functions, but `src/sprite.c:1547` explicitly uses it to select affine matrix slots without enabling hardware mosaic. These are **false candidates** for an on-screen MOSAIC scene. Data-driven register values or later routes still need runtime observation before saying the game never enables it.

| Function | Effect/scene | Hardware layer | Trigger | Manual checkpoint needed |
|---|---|---|---|---|
| `in_game.c` VBlank register writer | General in-game setup; no nonzero source found | BG/OBJ register only | In-game VBlank | YES, if a nonzero source and enable bit are found |
| `pause_screen.c` VBlank/exit | Pause register setup/reset | BG/OBJ register only | Pause menu | YES, if an enabled layer is observed |
| `sprite.c` sprite OAM conversion | Matrix slot selection for `SPRITE_STATUS_MOSAIC` | Neither: not OAM MOSAIC | Boss/sprite AI state | NO; false hardware-mosaic lead |

No legitimate late-scene state or trusted pixel reference is available. Real MZM scene qualification is **UNVERIFIED**. Once an actual `REG_MOSAIC` size above 1 and BGxCNT/OBJ enable bit are observed together, record its route and create a private `.local/m4-checkpoints/mosaic-<scene>.state`. The same checkpoint/frame should be compared under the pinned and patched runtimes and against a hardware or trusted reference. No state is in Git.

The patched MZM build used a separate `build-m4-mosaic` with `GBARECOMP_ROOT` pointing to this upstream worktree. Both existing strict-static cases passed with all four error counters zero; the baseline-pin versus patched comparison returned `UNCHANGED` twice. This proves boot regression stability, not a visible MZM mosaic scene.
