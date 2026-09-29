# M4 Chozodia Escape HBlank RAM code audit (BMXE rev 0)

> MZM now pins GBARecomp integration revision `984957a4f1c70379e9ce6717c1fd080aecf7e37d`.
> Private native dispatch is synthetically qualified. No real Escape scene or
> HBlank callback has been observed.

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

The integrated generator emits `gf_chozodia_hblank_ram` with RAM guest PCs
and ROM-backed opcodes. Its root and 20 decoded interior PCs are private;
`runtime_has_static_entry` is false for them. The MZM hook compares all 0x40
bytes on every root or interior resume, then calls the generated private
target for that exact PC. A wrong image returns unhandled and strict-static
dispatch misses. The full copy includes literal data, but no alias is emitted
for it. The ordinary ROM translation remains public for ROM calls; it is not
used to execute the copied callback.

The native test captures the WIN0H write with guest PC `0x03001752` and
returns with balanced call stack. The upstream synthetic nested IRQ fixture
records RAM return PC, SPSR and private resume through the same generated
API. These tests establish infrastructure; they do not establish actual
HBlank callback delivery or exact hardware timing in the Escape scene.

## Qualification matrix

| Layer | Current evidence | Status |
|---|---|---|
| RAM image identification | Full 0x40-byte synthetic and local USA ROM tests, including negatives | PASS |
| Private native dispatch | MZM hook and generated private body execute only after complete image match | PASS, SYNTHETIC |
| HBlank IRQ delivery | PPU event, IF request, IE/IME/CPSR gate and IRQ driver audited; no full-path test | PARTIAL |
| Repeated callback execution | No late-game checkpoint | UNVERIFIED |
| IRQ/resume correctness | Upstream nested IRQ and MZM interior WIN0H resume pass; real callback chain awaits checkpoint | PASS, SYNTHETIC |
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

`mzm_chozodia_resolver_test` checks root and interior image matches, ARM mode,
changed bytes, a function-length-only copy, and unrelated union contents.
Its local-ROM mode also places actual clipdata bytes in the overlapping
window and confirms rejection. `mzm_chozodia_native_test` links the real
generated MZM corpus and production hook; local-ROM execution checks wrong
root/interior strict misses, correct private entry/interior execution and
WIN0H PC identity. No game bytes are stored in Git.

The integrated build passes harness 01/02/03. Case 03 retains 317 PPU frames,
`0x45B4` WAITCNT write, `final_pc=0x000001B4`, and four zero strict-static
counters. This boot probe does not visit Chozodia.
