# M3 Evidence — Strict-static Gameplay Proof

**Date:** 2026-09-28  
**State:** **CONFIRMED / PASSED**

## Qualification mode

The run was launched with:

```text
GBARECOMP_STRICT_STATIC=1
MZM_MILESTONE_TRACE=1
```

GBARecomp strict-static mode disables overlay cache loading, background healing, and interpreter bridging. Any missing dispatch entry aborts immediately.

## Runtime result

```text
cpu_backend=static-recompiled
final_pc=0x000001B4
unmapped=0
io_unhandled=0
steps=181013
cycles=1151698528
ppu_vcount=70
ppu_frames=6891
frames_presented=6889

self_heal_coverage=FULLY_STATIC
dispatch_misses=0
interpreted_insns=0
healed_native=0

mzm_milestones intro_handler=YES intro_hits=97
               title_handler=YES title_hits=140
```

The process did not abort under strict-static enforcement.

## Observed gameplay route

During this same run, the operator progressed through:

```text
Boot
→ Intro
→ Title
→ New Game
→ opening
→ controllable Samus
→ multiple early rooms
→ Save Room
→ save command
```

This exceeds the original M3 route requirement of reaching controllable Samus in the first playable room.

## SRAM write evidence

After the in-game Save Room operation, the runtime-owned save file existed beside the verified ROM:

```text
size=32768 bytes
modified=2026-09-28 14:46:26 -03:00
SHA-256=471f0af7a3b315f8ac39b7185e157e6f3fe4ef70be165961a986eaab6ede3c67
```

This confirms that the exercised SRAM write path completed and persisted to disk while the session remained fully static.

It does **not** yet prove:

- reloading this save reproduces the same in-game state;
- all save slots / erase / copy behavior;
- cross-version or emulator/native save interchange;
- full-game save compatibility.

Those are M4 compatibility items.

## Verdict

### M3A — gameplay proof

**PASSED.**

Controllable gameplay, room traversal, and an encountered save path were exercised.

### M3B — static-verified gameplay

**PASSED for the qualified early-game route.**

The same gameplay route ran under explicit strict-static enforcement with:

```text
dispatch_misses=0
interpreted_insns=0
unmapped=0
io_unhandled=0
```

No interpreter/self-heal fallback was available to hide missing static coverage.

## Transition

**M3 — Gameplay proof is closed.**

The active milestone is now **M4 — Compatibility**, which expands from one proven route to broad/full-game correctness.
