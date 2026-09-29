# M4 next target: executable RAM haze variants

The prefetch audit in [M4 Game Pak prefetch](M4-GAMEPAK-PREFETCH.md) found
that `GbaBus::access_cycles()` cannot correct instruction timing by itself:
both CPU engines embed a fixed instruction fetch cost, and no shared dynamic
fetch seam exists. Exact queue fill, DMA contention and midstream WAITCNT
ordering still need an independent oracle. The isolated upstream
`mzm/gamepak-prefetch` worktree remains at dynamic WAITCNT commit `6ab52a2`
with no prefetch implementation. The official MZM pin remains `e7728148`.

**Selected next MZM compatibility target: executable RAM `hazeCode` variants.**
The [compatibility matrix](M4-COMPATIBILITY-MATRIX.md) records this as the
other demonstrated engine-level `BLOCKED` gap: different code bodies can
occupy the same RAM PC, while current fixed mapping chooses one body. A
variant-aware native dispatch qualification would advance MZM compatibility
without asserting unverified cycle precision. Chozodia and NES Metroid remain
later routes. No haze implementation begins in this audit.

Game Pak prefetch remains a separate timing track. Its next prerequisites are
a shared generated/interpreter instruction-fetch seam, per-machine snapshot
design, source-grounded event ordering tests, and a local cycle oracle such as
NBA through `oracle/diff_cycle_nba.py`. MZM case
`03_initialize_game_timing` now provides a repeatable strict-static route to
the `WAITCNT=0x45B4` write for that future comparison. Passing it alone does
not qualify Game Pak prefetch timing.
