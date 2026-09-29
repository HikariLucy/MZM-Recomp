# M4 Chozodia Escape HBlank RAM code audit (BMXE rev 0)

## Layout and copied image

The USA decomp's `src/globals1.c` defines `gNonGameplayRam`. The linked
`mzm_us.map` and ELF put it at `0x030016C4` (size `0x628`). The
`chozodiaEscape` union member starts there. The **linked instructions** in
`ChozodiaEscapeSetHBlank` load `sNonGameplayRamPointer`, add `0x6C` for the
DMA destination, and add `0x6D` for the callback pointer. The pointer's ROM
initializer is `&gNonGameplayRam`; therefore `hblankCode` starts at
`0x03001730`, is 4-byte aligned, and the installed Thumb pointer is
`0x03001731`. `runtime_dispatch` clears bit 0 before its RAM hook, which
receives `0x03001730` with `thumb=1`. This derives the offset from the linked
code as well as the struct, not from `sizeof` alone.

`include/structs/chozodia_escape.h` declares `u8 hblankCode[128]` (0x80
bytes). `DMA3_COPY_16(src,dst,count)` expands through `DMA_SET` to a DMA
control word whose low count field is the number of **16-bit transfers**.
The argument `0x20` therefore copies **0x40 bytes**. The ELF's Thumb symbol
`ChozodiaEscapeHBlank` is `0x08087939`, size `0x3C`; its aligned source is
`0x08087938`. The copy ends at `0x08087977`, four bytes into the next
function's prologue. Byte verification must include all 0x40 bytes, though
only 0x3C belong to the HBlank function. The remaining 0x40 bytes in the
destination buffer are unrelated union contents and are not matched.

## Callback and IRQ route

`ChozodiaEscapeShipHeatingUp` at its `CONVERT_SECONDS(2.5f + 1.f / 30)`
switch case calls `ChozodiaEscapeSetHBlank`, then
`ChozodiaEscapeSetupHBlankRegisters`. The former copies the image using DMA3
and calls `CallbackSetHblank(0x03001731)`, which stores the pointer in
`gHBlankCallback` (`0x03001CF0`). The latter enables `DSTAT_IF_HBLANK` in
DISPSTAT and `IF_HBLANK` in IE while temporarily clearing IME, then sets IME.
This is the useful future checkpoint boundary: just after both setup calls,
before the subsequent explosion animation consumes `explosionHazeValues`.
The callback is disabled later in `ChozodiaEscapeShipHeatingUp`.

The pinned GBARecomp PPU reports `hblank_started` at the visible-to-HBlank
transition. `runtime_bus_bridge.cpp::tick_devices` requests `IrqHBlank` when
DISPSTAT bit 4 enables it. `GbaIo::request_irq` sets IF bit 1; `irq_pending`
requires IE and IME, and `runtime_tick` also checks CPSR.I before calling
`runtime_irq`. The latter enters the BIOS IRQ vector and drives its generated
handler to exception return. MZM's copied ARM `IntrMain` selects HBlank as
the fifth entry in `sIntrTable` (`0x0808CA9C + 0x10`), calls
`CallbackCallHblank` (`0x08000B00` Thumb), and that function calls the
installed RAM Thumb pointer, then acknowledges IF. Control returns through
`CallbackCallHblank`, `IntrMain`'s System-to-IRQ restore, and the BIOS
exception return. `RuntimeRamDispatchHook` sees the RAM PC at the indirect
callback call, *inside* this IRQ host stack. Existing code implements all
these layers; no isolated upstream test currently proves the full
DISPSTAT→IF→IRQ→MZM callback path, and no real Chozodia scene has reached it.

## Instruction and position analysis

The ELF disassembly at `0x08087938..0x08087973` has no internal branch, BL,
or ADR. It begins `push {r4,lr}`, reads `REG_VCOUNT` (`0x04000006`), loads
`REG_WIN0H` (`0x04000040`), dereferences the ROM variable
`sNonGameplayRamPointer` (`0x08754BC4`), indexes its
`explosionHazeValues[hazeBufferReadingId][vcount]`, writes WIN0H, then
returns with `pop {r4}; pop {r0}; bx r0`. Four PC-relative `ldr` instructions
at source offsets `+2,+6,+8,+E` read literal words at `+0x2C,+0x30,+0x34,+0x38`.
The RAM destination and ROM source are both word aligned, so the same
instruction bytes address the copied literal words at the corresponding RAM
offsets. The 0x40-byte copy includes all four words. At the ARM instruction
level this image is position independent for the intended data accesses.

**Native canonicalization currently needs RAM-PC semantics.** The generated
`gf_ChozodiaEscapeHBlank` exists (`generated/recompiled.h`, source
`0x08087938`; no seed or generated edit is required), but it writes ROM
addresses to `g_cpu.R[15]` at every instruction and calculates its literal
reads from ROM addresses. Byte identity makes the literal *values* equal,
and its guest LR/stack operations and Thumb `bx` return can still find the
callback's return address. It does not make the PC or fetch/timing behavior
equal. `runtime_tick` can synchronously enter another IRQ using
`g_cpu.R[15]` as its return address. During this native body, that address
would be ROM, where the guest's interrupted callback PC is RAM. An unwind
inside the IRQ also re-dispatches `g_cpu.R[15]`; the ROM function has only a
static entry at `0x08087938`, not interior resume entries. `runtime_should_yield`
blocks ordinary VBlank yield while `g_irq_nest_depth > 0`, but other unwind
conditions exist. CPSR Thumb mode and guest LR are not themselves rewritten
by the image resolver; the PC mismatch remains. ROM-relative memory-cycle
costs for literal loads also differ from RAM. A byte matcher alone cannot
qualify IRQ nesting, exception return, precise HBlank timing, or resume.

The resolver currently **identifies** the full 0x40-byte image and records
one `MZM_TRACE_RAM_DISPATCH=1` match with `kind=chozodia_hblank`, real
`runtime_pc`, `source_pc=0x08087938`, and `native=0`; its summary aggregates
attempts and matches. It returns unhandled to strict-static dispatch. This
is intentional until the generic runtime preserves the logical RAM PC and
timing through native execution. It never prints per-scanline match spam.

## Qualification matrix

| Layer | Current evidence | Status |
|---|---|---|
| RAM image identification | Full 0x40-byte synthetic and local USA ROM tests, including negatives | PASS |
| Native dispatch | Generated target exists; ROM-PC exposure at each instruction | BLOCKED |
| HBlank IRQ delivery | PPU event, IF request, IE/IME/CPSR gate and IRQ driver audited; no full-path test | PARTIAL |
| Repeated callback execution | No late-game checkpoint | UNVERIFIED |
| IRQ/resume correctness | RAM entry→return→IRQ exit→next HBlank has no real run; ROM-PC issue | BLOCKED |
| Real scene qualification | No Chozodia Escape state | UNVERIFIED |

The future private checkpoint is
`.local/m4-checkpoints/chozodia-hblank.state` (not created). Capture after
the callback pointer and HBlank enables are installed, before the explosion
animation. A successful future strict-static run must show callback hits
greater than one, approximately one per enabled scanline as appropriate for
that scene, repeated RAM entry→native return→IRQ exit→next-HBlank re-entry,
and zero `dispatch_misses`, `interpreted_insns`, `unmapped`, and
`io_unhandled`. No fixed hit count is asserted before studying the scene.

## Local verification

`mzm_chozodia_resolver_test` synthesizes the copy and rejects wrong PC,
ARM mode, a changed byte, a function-length-only copy, and unrelated union
contents. `--rom /path/to/local-USA.gba` reads the 0x40-byte source window
directly from a local 8 MiB cartridge image, copies it into synthetic RAM,
and tests identification only. No game bytes are stored in Git.

The existing case 03 expectation (317 PPU frames) matches the M4 build
linked to the local WAITCNT worktree. A separate rebuild against the
unmodified `e772814` upstream pin reproducibly reports 314 frames and
67,641,054 cycles at the same 1,400-step limit, with the expected PC and
all four strict-static counters zero. Cases 01/02 pass there. This is a
timing-baseline difference, not evidence that the Chozodia image path ran.
