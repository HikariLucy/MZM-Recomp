# M4 next target

The integrated GBARecomp revision `644ec842f8b2106f21fdef6ae05ae997c8e49869`
provides private relocated native entry and MOSAIC/WAITCNT fixes. MZM's
Chozodia hook is byte gated and synthetically qualified, including interior
resume and WIN0H PC identity. Harness cases 01/02/03 pass, with case 03 at
317 frames.

There is no Chozodia HBlank checkpoint. Real callback delivery, repeated
callbacks, and the Escape scene remain **UNVERIFIED**. Further Chozodia
qualification should wait for a genuine state captured after
`ChozodiaEscapeSetHBlank` and `ChozodiaEscapeSetupHBlankRegisters`.

The haze BG3 checkpoint is also pending a manual run. Keep its seven-image
resolver as-is. Game Pak prefetch has no implementation in this integration.

**Next target: NES-1a, blocked at the pinned GBARecomp generator.** The
[NES audit and implementation probe](M4-NES-METROID.md) mapped the local USA
ROM's executable islands precisely. A one-word ARM trampoline at
`0x087D8000` branches to `0x087D80D4`; `0x087D8004` is data. With this
boundary enforced, the finder follows a false BL fallthrough into that data.
Its far-BL analysis also stops at the loader's `MSR CPSR`, leaving a false
host return frame to `0x087D8004`. The Thumb reset path similarly walks
past non-returning `SoftReset` into literals at `0x087D8140`. The local
config cannot safely override these control-flow facts. Keep the imported
data exclusion and production corpus unchanged until a generic, reviewed
finder/codegen fix is available; no GBARecomp files were changed here.

After that fix, finish NES-1a with only the proven ROM instruction islands,
public dispatch/negative-data checks, and a local-ROM fixture that captures
the first expected dynamic frontier at `0x03007400` ARM. It must prove the
BIOS LZ77 output against a fresh local reconstruction, capture CPU state,
and report zero misses before the frontier and zero interpreted instructions.
This qualifies the **ROM boot chain**, not NES gameplay.

Only after NES-1a passes, **NES-1b** is the next target: run the `0x214`-byte
payload at `0x03007400` strict-static. The image comes from a compressed ROM
stream and has no usable direct `source_addr`; choose between external
executable-image input, a generator-owned local derived image, or an existing
capability established during NES-1a. Do not implement NES-1b as part of
the NES-1a fix.
