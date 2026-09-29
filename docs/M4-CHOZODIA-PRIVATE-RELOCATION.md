# M4 Chozodia private relocation experiment

Date: 2026-09-29. MZM remains on the official GBARecomp pin `e7728148`.
The separate local worktree `GBARecomp-mzm-private-reloc` is based on that pin.
Its results are experimental; MZM's committed RAM hook still reports `native=0`.
No Chozodia gameplay checkpoint exists.

## Relocation semantics and generated body

The pinned `[[extra_func]]` contract already accepts `addr=0x03001730`,
`source_addr=0x08087938`, `mode="thumb"`. `FunctionFinder::discover_one`
maps each runtime walk address through the source/runtime bias to read ROM
opcodes. `emit_function_body_str` applies the same bias to ROM bytes but passes
the **runtime** PC to `ThumbDecoder::decode` and the code generator. No
`[[code_copy]]` is needed for this entry; the existing clipdata mapping at
`0x030016C4..0x03001944` can coexist with the explicit source bias.

A local overlay with that one `[[extra_func]]` emitted
`gf_chozodia_hblank_ram` in `recompiled_015.cpp`. Its first instruction sets
`g_cpu.R[15]=0x03001730`; the function entry hook receives `0x03001730`.
The load at `0x03001732` uses `(0x03001736 & ~3) + 0x28`, reaching the copied
literal at `0x0300175C`. Trace events and the WIN0H store use `0x03001752`.
The body ends after `bx r0` at `0x03001758` and unwinds through
`runtime_call_should_return`. The decomp symbol's `0x3C` size includes the
literal pool; only `0x2A` bytes decode as instructions. The exact matcher
still checks all `0x40` copied bytes, including four bytes past the symbol.

## Why config alone is unsafe

The pinned generator also adds `{0x03001730, Thumb,
gf_chozodia_hblank_ram}` to `kDispatchTable`. `runtime_dispatch` invokes the
RAM hook first, but a declined hook falls through to that table. With a wrong
RAM image, `runtime_dispatch(0x03001731)` would therefore enter the Chozodia
native body without validating the opcodes. This fails the negative safety
criterion. `[[exclude_func]]` removes the body as well as the table entry.
No existing private emission option was found in the pinned config, finder,
emitter, or dispatch writer.

The isolated GBARecomp branch adds generic `dispatch = false` to
`[[extra_func]]`: it emits the named function and its direct CFG descendants,
but omits their roots and interior resume aliases from the ordinary table.
The synthetic test puts wrong bytes at its RAM address; a byte-checking hook
declines, and `runtime_dispatch` aborts on a strict-static miss. With exact
bytes, the same hook invokes the private native symbol. This branch has not
changed MZM's official pin or generated corpus.

## Clipdata overlap

The current MZM dispatch table has clipdata entries at `0x030016C4`,
`0x030017C0`, `0x030017C8`, `0x030017EA`, and `0x030017F8`. It has no entry
at `0x03001730`. The generated clipdata root branches to `0x030017C0`;
searching its generated CFG found no call, branch, label, or resume alias at
`0x03001730`. `static_resume_all` is disabled in MZM. This rules out a known
static clipdata dispatch/resume collision at that PC. It does not prove that
every future clipdata runtime path is impossible; the byte gate is required
because the union is mutable.

## Execution probes

A fully synthetic upstream fixture places Thumb source bytes at
`0x08000100` and copies them to `0x03001000`. It contains two PC-relative
literal loads, an internal branch to `0x0300100C`, a WIN0H store, and a
`pop {pc}` return. The private root and CFG child are absent from
`runtime_has_static_entry`. The generated private body is invoked through a
byte-checking RAM hook; MMIO capture records WIN0H `0x34` at `0x0300100C`.
The source translation records `0x0800010C`. R0–R14, CPSR, stack memory and
WIN0H value agree. The local timing model measured 19 cycles for RAM and 33
for ROM, reflecting data literal access costs; it does not model accurate
IWRAM-versus-ROM **instruction fetch** or full Game Pak prefetch behavior.

In the same fixture, an outer IRQ switches to System mode and runs the
private callback. A pending IRQ is delivered by `runtime_tick` during the
callback. The nested IRQ records return PC `0x03001002` and System/Thumb
SPSR `0x3F`. Both IRQ levels complete, CPSR returns to `0x3F`, and the
call-return stack reaches zero. A pending VBlank yield does not unwind the
callback while `g_irq_nest_depth` is nonzero. A separate debug-breakpoint
yield at `0x03001004` resumes through a private interior alias and the
byte-checking hook; its final state and 19-cycle count match uninterrupted
execution.

A local-only test compiled the actual Chozodia ROM and relocated native
functions, copied the verified `0x40`-byte local ROM window to `0x03001730`,
and initialized one haze halfword to `0x1234`. MMIO capture recorded the
WIN0H write at `0x0808795A` for the ROM translation and at `0x03001752`
for the relocated translation. R0–R14, CPSR, stack memory and WIN0H value
matched. The current timing model measured 81 ROM versus 53 RAM cycles.
The same local fixture enters the relocated body through a 64-byte-checking
RAM hook. After changing one byte at `0x03001730`, its
`runtime_dispatch(0x03001731)` test declines the hook and aborts on a
strict-static miss at `0x03001730`, without entering the native body.
The fixture and all ROM-derived generated files stayed under `/tmp`; no
Nintendo bytes were committed.

## Integration gate

MZM remains `native=0`: the official pin ignores the experimental
`dispatch = false` key, so adding the overlay to the normal config would
reintroduce the unsafe global entry. Integration requires a reviewed
GBARecomp revision with private emission and an explicit generation/build
gate, followed by a MZM hook that verifies all 64 bytes at both the initial
entry and any private interior resume PC. There is still no real Chozodia
HBlank callback, repeated execution, or scene qualification.
