# M2A — First Runtime Miss Classification

**Date:** 2026-09-28  
**State:** **ACTIVE**

## First hybrid session

The first MZMRecomp session completed successfully with the static-recompiled cart backend, but coverage was not fully static:

```text
dispatch_misses=18
healed_native=9
failed=9
self_heal_coverage=NOT_STATIC
```

## Group A — BIOS misses

Observed ARM PCs:

```text
0x00000008
0x00000018
0x00000128
0x00000138
0x00000140
0x00000170
0x000001A0
0x000001AC
0x000001B4
```

These are **not MZM cartridge roots**.

The pinned GBARecomp `bios/gba_bios.toml` already contains the relevant BIOS vector/function/resume coverage, including the SWI/IRQ vectors and the `0x000001A0..0x000001B8` resume range.

Root cause: the M1 minimal host linked GBARecomp's placeholder BIOS dispatch because no locally generated `bios_recompiled.cpp` was present.

Disposition:

1. generate the BIOS corpus from the user's verified 16 KiB BIOS;
2. keep the generated BIOS under `.local/generated-bios/`;
3. configure CMake with `GBARECOMP_GENERATED_BIOS_DIR`;
4. rebuild and re-run before considering any BIOS miss proposal.

Do **not** merge these addresses into `configs/mzm-us.toml`.

## Group B — high-IWRAM misses

Observed Thumb PCs:

```text
0x03007D08
0x03007D14
0x03007D18
0x03007D20
0x03007D28
0x03007D38
0x03007D50
0x03007D8C
0x03007D90
```

The proposal heuristically labels these a jump-table candidate, but that classification is not accepted.

MZM's crt0 sets the system stack to:

```text
SP_SYS = 0x03007E60
```

The misses lie only `0xD0..0x158` bytes below that stack top.

MZM's SRAM implementation deliberately copies executable Thumb helpers into local stack arrays:

```c
SramWriteUnchecked:
    u16 code[0x40];   // 0x80 bytes

SramCheck:
    u16 code[0x60];   // 0xC0 bytes
```

and calls those arrays as Thumb functions.

This address geometry is strongly consistent with stack-local SRAM helper execution.

However, the project will not convert the proposal to `[[extra_func]]` or `[[jump_table]]` from geometry alone. Exact disassembly/runtime correspondence must be captured first.

## Self-heal cache observation

The first cache inventory contains compiled heal artifacts for the nine BIOS misses. The nine high-IWRAM misses did not produce successful native heal artifacts in the displayed cache inventory.

That is consistent with fixed BIOS code being recompilable by PC while mutable/stack-copied RAM code requires source-byte-aware handling.

## Next gate

1. statically recompile and link the BIOS;
2. re-run the same MZM route;
3. expect the BIOS miss group to disappear;
4. disassemble `SramWriteUnchecked` / `SramCheck` and correlate their stack-buffer PCs with the remaining `0x03007Dxx` misses;
5. only then choose a generic handling strategy for the stack-local copied code.
