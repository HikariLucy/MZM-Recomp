# M4 next target: generic RAM-PC semantics for Chozodia IRQ code

The haze BG3 capture remains pending a human run. There is no
`.local/m4-checkpoints/haze-bg3.state`; cases 01/02/03 do not require it.

The [Chozodia HBlank audit](M4-CHOZODIA-RAM-CODE.md) now identifies the
exact 0x40-byte DMA image at `0x03001730`, and the native ROM translation
`gf_ChozodiaEscapeHBlank` already exists. The function's instructions are
position independent after copying. Native dispatch is still blocked:
generated code publishes ROM PC during every instruction, while an IRQ
preemption or resume of the RAM callback requires RAM PC. Literal memory
timing is ROM-relative too. Byte identity alone does not repair either issue.

**Selected next engineering target:** establish a generic runtime seam for
logical PC and fetch/memory timing of native translations entered from
byte-verified RAM copies, including synchronous nested IRQs and interior
resume. Test that seam with an isolated HBlank IRQ path before enabling
Chozodia native dispatch. This is shared GBARecomp work if an implementation
is warranted, not an MZM-specific IRQ simulation. The current pinned
GBARecomp PPU produces HBlank events and requests IF_HBLANK; its IRQ driver
is implemented, but no upstream end-to-end test proves delivery through a
guest callback. There is no demonstrated HBlank event-generation gap.

The later real-game qualification needs a private state at the start of
Chozodia Escape after `ChozodiaEscapeSetHBlank` and
`ChozodiaEscapeSetupHBlankRegisters`, before the explosion animation.
That state is not created here. It must demonstrate repeated callback entry,
native return, IRQ exit and re-entry, with strict-static error counters zero.
Until then the Chozodia scene remains unverified.

NES Metroid executable RAM remains a later audit target. The concrete
Chozodia PC/resume issue takes precedence. Game Pak prefetch remains a
separate timing track; the official upstream pin is unchanged.
