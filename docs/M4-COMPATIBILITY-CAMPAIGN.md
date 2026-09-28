# M4 — USA Compatibility Campaign

**Started:** 2026-09-28  
**State:** **ACTIVE**

M0–M3 proved the architecture and one strict-static gameplay route. M4 expands from that proof route toward broad/full-game correctness.

## Working rule

Do not guess which subsystem fails next.

Continue from known-good saves under `GBARECOMP_STRICT_STATIC=1`. Every play session is an evidence campaign. The first deterministic problem encountered becomes the next engineering target.

## Session acceptance baseline

A session remains static-clean when it ends with:

```text
self_heal_coverage=FULLY_STATIC
dispatch_misses=0
interpreted_insns=0
healed_native=0
unmapped=0
io_unhandled=0
```

Visual, audio, timing, progression, or save defects can still exist with zero dispatch misses, so these counters prove coverage rather than total accuracy.

## Campaign checkpoints

### M4.1 — Save lifecycle

- [x] Save from gameplay.
- [x] Persist 32 KiB SRAM file.
- [x] Exit process.
- [x] Relaunch strict-static.
- [x] Existing save is recognized.
- [x] Load save and return to gameplay.
- [x] Reload session remains zero-miss / zero-interpreter.

### M4.2 — Main USA progression campaign

For every substantial route segment record:

- last known-good save/checkpoint;
- areas/rooms traversed;
- major items acquired;
- bosses/minibosses defeated;
- doors/elevators/transitions exercised;
- menus/map/status used;
- cutscenes encountered;
- saves/reloads performed;
- visible rendering anomalies;
- audio anomalies;
- strict-static summary.

A route segment passes when gameplay behavior is usable and no unexpected static-coverage failure occurs. Fidelity defects remain open even when coverage passes.

### M4.3 — Known engine gaps

Track and qualify deliberately:

- PPU MOSAIC rendering;
- mutable same-PC `hazeCode`;
- WAITCNT/prefetch timing accuracy;
- late Chozodia HBlank copied code.

Do not patch these speculatively. First capture a real MZM route that exercises each behavior, then implement/test against that evidence.

### M4.4 — USA completion

Before claiming USA full-game compatibility, qualify:

- all required progression areas;
- all bosses;
- critical items and progression flags;
- major cutscenes;
- death/restart;
- save/load at multiple points;
- final sequence and ending;
- postgame paths needed for the supported scope;
- no known progression blocker;
- documented visual/audio deviations.

### M4.5 — Secondary compatibility tracks

After the USA main game is broadly qualified:

- Europe region;
- Fusion Gallery/link behavior;
- unlockable NES Metroid dynamic-code subproject.

NES Metroid remains a separate high-risk compatibility target and is not implied by completion of the ordinary Zero Mission campaign.

## Evidence policy

Local sessions, ROMs, BIOS files, saves, screenshots, traces, and generated ROM-derived source remain untracked under `.local/` or the separate research workspace.

Public evidence records only:

- hashes;
- addresses;
- tool/framework revisions;
- counters;
- conclusions;
- non-copyrighted diagnostic excerpts.
