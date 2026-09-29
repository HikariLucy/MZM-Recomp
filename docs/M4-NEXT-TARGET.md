# M4 next target

The integrated GBARecomp revision `984957a4f1c70379e9ce6717c1fd080aecf7e37d`
provides private relocated native entry, MOSAIC/WAITCNT, non-returning calls,
and SoftReset control-flow termination. MZM's Chozodia hook is byte gated and
synthetically qualified, including interior resume and WIN0H PC identity.
Harness cases 01/02/03 pass, with case 03 at 317 frames.

There is no Chozodia HBlank checkpoint. Real callback delivery, repeated
callbacks, and the Escape scene remain **UNVERIFIED**. Further Chozodia
qualification should wait for a genuine state captured after
`ChozodiaEscapeSetHBlank` and `ChozodiaEscapeSetupHBlankRegisters`.

The haze BG3 checkpoint is also pending a manual run. Keep its seven-image
resolver as-is. Game Pak prefetch has no implementation in this integration.

**NES-1a is QUALIFIED (PASS).** The ROM boot trampoline at `0x087D8000` (ARM),
the loader at `0x087D80D4` (ARM, `returns = false`), the LZ77 decompression
continuation at `0x087D8110` (ARM), the reset ARM stub at `0x087D8124`,
the reset Thumb stub at `0x087D812C`, and the SoftReset continuation at
`0x087D813E` are fully generated and verified in `tests/m4/nes_loader_frontier_test.cpp`.
Execution reaches the first dynamic frontier at `0x03007400` ARM with depth 0
host return stack, zero dispatch misses, zero interpreted instructions, and zero
unmapped/IO errors. Guest IWRAM payload matches the independently reconstructed
LZ77 stream byte-for-byte (`0x214` bytes, SHA-256
`e94f6dba7b7ec0dd183335fa2efdd5bb5a1f4dc1f7593d8e8961b1e2ce681f44`). The static
quit/reset stub cleanly services `RegisterRamReset` and `SoftReset` back to
cartridge re-entry at `0x08000000` ARM without executing adjacent literal pools.

**NES-1b is QUALIFIED (PASS).** Strict-static native execution of the derived ARM
payload (`0x03007400..0x03007614`, 532 bytes, SHA-256 `e94f6dba7b7ec0dd183335fa2efdd5bb5a1f4dc1f7593d8e8961b1e2ce681f44`)
is fully verified in `tests/m4/nes_payload_frontier_test.cpp`.
- GBARecomp `[[executable_image]]` multi-image architecture ingests `.local/nes-payload-usa.bin` at build time without committing Nintendo binaries.
- All 133 ARM instructions of the payload are recompiled into static private entries with full resume aliases (`kPrivateDispatchTable`).
- Strict-static payload execution executes natively, writes WAITCNT `0x0014` at `0x03007408`, drives DMA setup and transfers, and reaches the first dynamic frontier posterior to the payload at `0x06006558` ARM (VRAM).
- Host call stack depth is 0; dispatch misses: 0; interpreted instructions: 0; unmapped bus/IO: 0.

**NES-2 is QUALIFIED (PASS) for the initialisation phase.** From `0x087D8000` the real chain runs native and strict-static through loader, payload, `0x06006558`, and cross-image transfers among Parts 1/6/5/4 with per-entry byte gates (six guest images byte-identical to ROM-derived images), reaching a reproducible frontier at **`0x03000488` Thumb (`EmulatorAudio_Initialize`, Part 2, IWRAM)** with `dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0`. See `docs/M4-NES-METROID.md` (image/mutability table, frontier, gaps).

**Next target (from the real frontier): GBARecomp image-scoped private CFG (Gap B).** Part 2 cannot be generated because its IWRAM addresses alias MZM's public `code_copy` entries (`[finder] private CFG conflicts with public entry at 0x030041EC`) and `data_range`/`visited_` are address-global. This needs a reviewed generic GBARecomp change (key private CFG nodes and data ranges by image; drop the private-vs-public conflict when images differ) with the synthetic test in the NES-2 doc, then re-declaring Part 2 and re-running `mzm-nes-emulator-frontier`. The second, smaller gap (Part 5 `ldrne pc,[pc,Rn,lsl #2]` table idiom at `0x0600E474`) should be fixed in the same round. NES-3 (first frame, IRQ delivery, audio) follows.
