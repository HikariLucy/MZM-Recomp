# M2A Evidence — Static BIOS and Stack-local SRAM Code

**Date:** 2026-09-28  
**State:** **CONFIRMED / VALIDATED**

## Static BIOS result

A locally generated BIOS corpus was built from the verified BIOS and linked into MZMRecomp.

The resulting host linked successfully. In the following cache-free runtime session, all previously observed BIOS misses disappeared.

Previous:

```text
9 BIOS misses
9 high-IWRAM misses
```

After static BIOS linkage:

```text
0 BIOS misses
8 high-IWRAM misses
```

The remaining misses were:

```text
0x03007D08
0x03007D14
0x03007D18
0x03007D28
0x03007D38
0x03007D50
0x03007D8C
0x03007D90
```

All are Thumb.

## Why these are not a jump table

The runtime proposal heuristically grouped the eight addresses as a possible jump table because they are close same-mode PCs. Source/disassembly evidence contradicts that interpretation.

MZM initializes the System stack to:

```text
SP_SYS = 0x03007E60
```

`SramWriteUnchecked()`:

```text
push {r4,r5,r6,lr}     ; -0x10
sub sp,#0x80           ; local u16 code[0x40]
...
mov r3,sp
add r3,#1              ; Thumb function pointer
bl _call_via_r3
```

Source template:

```text
SramWriteUncheckedInternal = 0x080051D4
SramWriteUnchecked         = 0x080051F8
copy length                = 0x24
```

`SramCheck()`:

```text
push {r4,r5,r6,lr}     ; -0x10
sub sp,#0xC0           ; local u16 code[0x60]
...
mov r3,sp
add r3,#1              ; Thumb function pointer
bl _call_via_r3
```

Source template:

```text
SramCheckInternal = 0x0800529C
SramCheck         = 0x080052CC
copy length       = 0x30
```

At the initial System SP:

```text
0x03007E60 - 0x10 - 0xC0 = 0x03007D90
```

which exactly matches one observed dispatch miss. Deeper caller frames naturally move the same copied helper downward to the other observed `0x03007Dxx` addresses.

## Framework-supported solution

The pinned GBARecomp runtime exposes:

```text
RuntimeRamDispatchHook
g_runtime_ram_dispatch_hook
```

for position-independent code copied to transient RAM addresses, explicitly including moving stack frames.

MZM-Recomp uses this hook rather than adding absolute stack PCs to TOML.

For a high-IWRAM Thumb dispatch, the hook compares guest RAM bytes byte-for-byte against the verified ROM templates above. Only on an exact match does it call the corresponding canonical generated native function.

This preserves two important invariants:

1. a different stack placement does not require a new address seed;
2. stale or unrelated RAM bytes can never be silently treated as the SRAM helper.

## Validation gate

A fresh run with no overlay cache must show:

- static BIOS remains miss-free;
- no `0x03007Dxx` dispatch misses from these helpers;
- no new unexpected misses on the tested boot/title route;
- `unmapped=0`;
- `io_unhandled=0`.

Validation result:

```text
cpu_backend=static-recompiled
unmapped=0
io_unhandled=0
ppu_frames=4131
frames_presented=4129
self_heal_coverage=FULLY_STATIC
dispatch_misses=0
interpreted_insns=0
healed_native=0
```

The static BIOS remained miss-free and every observed stack-local SRAM helper dispatch was handled by the byte-verified RAM canonicalizer. No new unexpected dispatch miss appeared.

This closes the SRAM/BIOS coverage problem for the tested route and advances M2 to explicit strict-static title qualification.
