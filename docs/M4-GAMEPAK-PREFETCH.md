# M4 Game Pak instruction prefetch audit

Audit date: 2026-09-28. Upstream isolated branch: `mzm/gamepak-prefetch` at
`6ab52a2`, based on the dynamic WAITCNT fix. This is an architecture and
validation note; **no Game Pak prefetch timing implementation was made**.
The official MZM pin is still `e7728148`.

## Hardware semantics and evidence boundary

[GBATEK's WAITCNT and Game Pak prefetch description](https://mgba-emu.github.io/gbatek/)
identifies bit 14 as prefetch enable, an eight-halfword opcode buffer, and
16-bit Game Pak transfer units. Thumb fetch consumes one halfword; ARM fetch
consumes two. The buffer can advance while the CPU performs internal work and
the Game Pak bus is free. Sequential and nonsequential fetches have different
WAITCNT costs, and a branch changes the instruction stream. The dynamic
WAITCNT branch already applies the programmed WS0/WS1/WS2 and SRAM costs to
explicit bus accesses. Enabling bit 14 there only stores the bit.

The prefetch buffer applies to **instruction fetches from Game Pak ROM**.
ROM data reads, SRAM traffic and DMA can claim the same external bus; they
cannot be treated as free buffer-fill time. Waitstate writes and disabling
prefetch must affect any pending fill and queued opcodes. Fetches across Game
Pak regions and 128 KiB nonsequential boundaries need explicit tests.
[NBA's implementation and release history](https://github.com/nba-emu/NanoBoyAdvance)
also call out subtle DMA and first-post-DMA fetch behavior. These sources
give a model outline, but the exact cycle ordering for simultaneous refill,
data access, DMA takeover, and WAITCNT changes has not been qualified here
against hardware or an independent cycle oracle. No cycle constants were
invented from that outline.

`GbaBus::prefetch_word()` and `bios_prefetch_` are **BIOS/open-bus value**
machinery. They synthesize the value returned by otherwise unreadable memory;
they do not count Game Pak opcode fetch cycles and cannot be reused as the
cartridge instruction queue.

## Current architecture

The actual ARM IR, code generator and interpreter are in the
`external/arm-recomp-core/profiles/armv4t_gba/` submodule. The `src/armv4t`
headers expose them; there are no `src/recompile/arm_codegen.*` files.

| Path | Current cycle accounting |
|---|---|
| `arm_ir.cpp::instr_cycle_base` | Fixed 1S opcode fetch; branch/SWI 3 cycles for refill, loads add an internal cycle, and PC writes add a fixed refill adjustment. |
| `arm_codegen.cpp::emit_instr` | Sets `g_cpu.R[15]` to current PC, checks yield, accumulates static base plus explicit memory costs, then calls `runtime_tick`. Some early exits tick separately; the condition-NV path ticks one cycle without `runtime_should_yield`. |
| `interpreter.cpp::Interpreter::step` | Uses the same fixed IR base and calls its bus adapter for explicit data access. Its condition-fail and PC-write paths require equivalent fetch treatment. |
| `runtime_mem_cycles` in `runtime_bus_bridge.cpp` | Calls `GbaBus::access_cycles(addr,width,sequential)` for explicit data accesses. It receives no fetch PC or ARM/Thumb stream information. |
| `runtime_tick` / `g_runtime_cycles` | Charges the resulting instruction total, catches up devices and DMA, handles IRQ and runs cosim checkpoints. The total does not identify which cycles left the Game Pak bus idle. |
| DMA | `GbaIo::dma_transfer_cost` uses `access_cycles`; bridge DMA steal adds its cycles separately. DMA can interrupt an instruction stream. |

The current PC and ARM/Thumb bit are available at an instruction boundary in
`g_cpu`/CPSR and in the interpreter state. A branch is known by the decoded
operation and resulting PC, but the generated path can tick before normal
epilogue on branches, SWI, PC writes and exceptions. Sequential/nonsequential
flags currently describe *data* transfer order, not opcode fetch order.

**`GbaBus::access_cycles()` alone is insufficient.** It never sees ordinary
opcode fetches, while `instr_cycle_base` embeds their assumed fixed cost.
Changing ROM data-access results would alter the wrong traffic. Putting
prefetch into `runtime_should_yield()` is also insufficient: bridge interpreter
loops and one generated condition path do not call it for each instruction,
and it may return for a scheduler yield before execution.

## Required execution seam and state

A correct implementation needs one shared instruction-fetch timing operation
used by generated code **and** every interpreter/fallback path. At minimum it
must receive the instruction PC, ARM/Thumb width, prior stream transition or
sequential status, and the timing of internal cycles versus explicit data
accesses. It must replace, rather than add on top of, the static 1S/refill
component in `instr_cycle_base`. Branch, BL/BX, PC writes, SWI, IRQ entry and
return, conditional execution, and resume paths need parity tests. Changes to
generated code would also require the overlay ABI and the arm-recomp-core
submodule to change together; hand-editing generated MZM output is invalid.

The natural owner of any eventual queue is each machine's `GbaBus` or a
machine-owned CPU timing object bound to that bus. A process-global queue
would leak across the multiplayer scheduler's machine switches. Required
state includes the next opcode halfword/address, queued halfwords (up to
eight), the stream validity/sequential state and partial-fill progress. The
exact fields, refill order and delay counters must be derived from a tested
event model rather than chosen from their names. Runtime totals alone cannot
reconstruct this state.

When implemented, it must be captured by normal save states, rewind, and
`SimulationStateCodec` multiplayer snapshots. Runtime timing capture/restore
must preserve the current machine's pending work. Cosim currently labels a
hash `prefetch` but hashes the BIOS open-bus latch; Game Pak queue state needs
its own hash component and snapshot compatibility/version handling.

## Flush, fill and consume rules to qualify

- Qualify empty-buffer and disabled-bit opcode fetches against programmed
  WS0/WS1/WS2, including ARM's two 16-bit transfers.
- Qualify fill during internal cycles and while the Game Pak bus is idle;
  distinguish those intervals from ROM/SRAM data and DMA ownership.
- Qualify consumption of one Thumb or two ARM halfwords, including partial
  availability and the eight-halfword limit.
- Qualify branch/nonsequential stream changes, 128 KiB boundaries, exception
  vectors, PC writes and ARM/Thumb transitions.
- Qualify queue invalidation and in-progress transfer handling when WAITCNT
  changes, bit 14 clears, or DMA starts/ends. The first CPU fetch after DMA
  needs its own oracle comparison.

These are test obligations, **not** claimed behavior of a new implementation.
The exact ordering of the pending-fill cases remains unresolved.

## Validation strategy and current result

First add redistributable, spec-derived RED tests around the shared fetch
seam: disabled/empty, ARM/Thumb streams, internal cycles, branch, data and
DMA contention, WAITCNT changes, and WS0/1/2. Add a short instruction-stream
fixture and compare generated/interpreter cycle stamps. Then test snapshot
capture, advancement, restore and replay in normal and multiplayer contexts.
Run codegen, bus, DMA, WAITCNT, cosim and full applicable suites. Hardware or
an independent cycle oracle must resolve ambiguous event ordering before an
accuracy PASS. Neither tests nor an implementation were added upstream because
the seam and those rules are not yet qualified.

The local oracle audit found no NanoBoyAdvance executable/debug server in the
available worktrees or caches, and no listeners on the configured ports.
`oracle/diff_cycle_nba.py` is available upstream but requires that server.
No emulator was installed or downloaded. With a valid local NBA, follow the
script's documented setup, compare the first cycle divergence with PC,
WAITCNT and DMA state, and fix causes rather than copying NBA constants.

## MZM route and comparison

The new `03_initialize_game_timing` case runs 1400 steps with
`GBARECOMP_YIELD_ON_VBLANK=0`. Three direct runs against the WAITCNT-only
`6ab52a2` build agreed: `WAITCNT=0x45B4` was written, final PC `0x000001b4`,
`67667408` guest cycles, 317 PPU frames, and zero dispatch misses,
interpreted instructions, unmapped accesses or unhandled IO. The formal
three-case collection also passed, with `0` and `24600475` guest cycles in
cases 01 and 02. Case 03 observes the write, not prefetch correctness.
There is no prefetch candidate to compare, and no host-performance delta.

**Verdict:** dynamic WAITCNT is implemented and spec-tested in its own branch;
Game Pak prefetch timing is **BLOCKED / not implemented / oracle unverified**.
MZM's reproducible `InitializeGame` route is qualified as a strict-static
execution and WAITCNT-write gate, not a timing-accuracy gate.
