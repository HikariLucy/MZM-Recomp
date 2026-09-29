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

**Next proposed audit:** NES Metroid's executable-RAM subsystem. Audit its
payload regions and dispatch requirements before implementing anything.
