# M4 private relocated native entry

Date: 2026-09-29. MZM uses local GBARecomp integration revision
`644ec842f8b2106f21fdef6ae05ae997c8e49869`, based on
`e7728148c6829ba526f682876430a0c9022dc6c0`. It cherry-picks the
MOSAIC tests/fix, WAITCNT tests/fix, and private relocation commits. The
private branch ends at `5760837cce012eaa4af5320a9582bd64508cbfda`.
No remote merge or push was performed.

## Generic contract

`[[extra_func]]` with `addr`, `source_addr`, `mode`, and `dispatch = false`
decodes opcodes from the immutable source but emits a native body whose guest
PC is `addr`. Omission of `dispatch` defaults to `true`. A private root and
its decoded interior instruction PCs enter `kPrivateDispatchTable`, never
`kDispatchTable`. `runtime_dispatch()` and `runtime_has_static_entry()` only
consult the public table. The game-owned RAM hook must verify the current
complete mutable image before each explicit
`runtime_invoke_private_entry(pc, thumb)` call. A declined hook follows the
normal dispatch miss path.

The finder keeps private status through direct RAM CFG descendants and seed
deduplication. An indirect call to a public ROM function remains public.
Contradictory explicit policies at one address/source/mode, or a collision
between public and private root/resume entries, abort generation. Resume
aliases derive from successfully decoded instruction PCs. Chozodia's literal
pool and the extra bytes copied by DMA have no private alias.

## Chozodia generation and MZM hook

MZM declares runtime `0x03001730`, source `0x08087938`, Thumb, and
`dispatch = false`. The generated `gf_chozodia_hblank_ram` has 21 private
entries: the root and 20 interior PCs `0x03001732..0x03001758` in steps of
two. Neither the root nor its aliases is in public `kDispatchTable`.

The hook admits only Thumb PCs in `[0x03001730,0x0300176C)`, compares the
entire 64-byte image `[0x03001730,0x03001770)` with ROM on **every** entry
and resume, then asks the private table for that exact PC. The table rejects
non-instruction PCs such as `0x0300175A`. The function range, decoded code
range, and DMA copy range have distinct purposes.

The local-ROM MZM native CTest enters through ordinary `runtime_dispatch` and
the production hook. Wrong RAM bytes at either root or `0x03001752` produce a
strict-static miss, while correct bytes enter the private body. The interior
resume writes WIN0H with captured guest PC `0x03001752` and returns with a
balanced host call stack. A separate resolver test puts the actual clipdata
ROM bytes into the overlapping union window and confirms rejection. Haze's
seven-image resolver is unchanged and its test still passes.

The upstream synthetic fixture uses the same generated private API for a
root, nested IRQ, RAM return PC, SPSR restoration, debug yield, interior
resume, and balanced return. The private branch passes 35/35 applicable
tests; the combined integration branch passes 50/50. MZM's three local tests
and harness cases 01/02/03 pass; case 03 remains at 317 frames with four
strict-static error counters zero.

## Remaining qualification

There is no `.local/m4-checkpoints/chozodia-hblank.state`. Real HBlank
callback delivery, repeated callbacks, and the actual Escape scene remain
**UNVERIFIED**. The native route is synthetically qualified for a future
strict-static scene test; no gameplay result is inferred from this fixture.
