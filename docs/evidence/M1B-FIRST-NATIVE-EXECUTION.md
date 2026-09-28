# M1B Evidence — First Native Execution

**Date:** 2026-09-28  
**State:** **PASSED — HYBRID / NOT STATIC**

## Inputs

- MZM USA ROM SHA-1: `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8`
- GBA BIOS size: `16384` bytes
- GBA BIOS SHA-1: `300c20df6731a33952ded8c436f7f186d25d3492`
- host binary: `MZMRecomp`
- backend: `static-recompiled`

## Runtime result

```text
cpu_backend=static-recompiled
host_window renderer=opengl
present-in-place ON
self_heal_recompile=ENABLED

final_pc=0x000001B4
unmapped=0
io_unhandled=0
steps=1614165
cycles=10345585742
ppu_vcount=47
ppu_frames=43952
frames_presented=43921

pal_nonzero=603/1024
vram_nonzero=71754/98304
oam_nonzero=301/1024

self_heal_coverage=NOT_STATIC
dispatch_misses=18
interpreted_insns=1180314
healed_native=9
native_calls=5193795
inflight=0
failed=9
```

The long-running session demonstrates successful native-host execution of MZM with the static-recompiled backend.

## Coverage debt

BIOS misses:

```text
0x00000008 ARM
0x00000018 ARM
0x00000128 ARM
0x00000138 ARM
0x00000140 ARM
0x00000170 ARM
0x000001A0 ARM
0x000001AC ARM
0x000001B4 ARM
```

All nine BIOS misses healed to native during the session.

High-IWRAM misses:

```text
0x03007D08 Thumb
0x03007D14 Thumb
0x03007D18 Thumb
0x03007D20 Thumb
0x03007D28 Thumb
0x03007D38 Thumb
0x03007D50 Thumb
0x03007D8C Thumb
0x03007D90 Thumb
```

These did not heal in the first session. Their location is consistent with the known MZM pattern that copies SRAM helper code into a local stack buffer and executes it from RAM. This is an engineering hypothesis pending review of the generated miss proposal and source correspondence.

## Verdict

M1B passes because its gate permits hybrid/self-heal and asks only for deterministic first recompiled execution with trace evidence.

This run does **not** satisfy a strict-static gate. The next work item is to review and classify all 18 misses, merge only evidence-backed static coverage, and rerun.
