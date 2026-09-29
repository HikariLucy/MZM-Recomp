# M4 next target: Game Pak prefetch-buffer timing

Dynamic WAITCNT waitstates are implemented and tested in the separate local
GBARecomp branch `mzm/waitcnt-timing`; the official pin remains `e7728148`.
See [M4 WAITCNT](M4-WAITCNT.md) for RED→GREEN, MZM trace, SRAM, regression,
and oracle limits. The MZM decomp writes `0x45B4` in `InitializeGame`, which
enables the hardware prefetch buffer on bit 14. Its `WAIT_GAMEPACK_CGB`
name is misleading relative to GBATEK.

**Selected next target: A, Game Pak prefetch-buffer timing.** MZM enables it,
and a 1400-step probe shows changed cycle counts after the dynamic waitstate
fix. The missing buffer state is now a direct timing gap on a verified MZM
route. Executable RAM haze and Chozodia remain important but are less directly
exercised by the current reproducible route. Prefetch should have its own
isolated change after fetch timing and oracle qualification; no prefetch
implementation began in the WAITCNT branch.
