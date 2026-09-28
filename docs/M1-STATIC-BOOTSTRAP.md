# M1 — Static Bootstrap

**Started:** 2026-09-28  
**State:** **M1A PASSED / M1B PASSED (HYBRID)**

M0 established feasibility. M1 changes the question from "can this architecture represent MZM?" to "can the generated MZM corpus be compiled and executed as a native Linux host process?"

## M1A — Generation and host build

### Inputs

- verified USA ROM, SHA-1 `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8`;
- pinned GBARecomp `e7728148c6829ba526f682876430a0c9022dc6c0`;
- `configs/mzm-us.toml`;
- generated decomp symbol overlay;
- imported semantic function/data symbols.

### Generated-code policy

`generated/` is local ROM-derived output and remains ignored by Git.

Never hand-edit generated translation units. Fix configuration, analyzer/runtime behavior, or source metadata and regenerate.

### M1A acceptance criteria

1. generation exits 0;
2. `undefined=0`;
3. five currently modeled fixed code copies are present;
4. 16 generated shards are produced;
5. CMake configures against the pinned GBARecomp checkout;
6. all generated shards compile without manual edits;
7. `MZMRecomp` links successfully on Linux.

M1A result:

```text
16 generated shards compiled
MZMRecomp linked at 100%
output: Linux x86-64 ELF
size: ~49 MiB
--help: PASS
```

M1A is **CONFIRMED / PASSED**.

A successful M1A build is **not** yet evidence that guest execution works.

## M1B — First execution

Run the host with the user's verified BIOS and ROM:

```text
MZMRecomp --bios <user BIOS> --rom <verified BMXE ROM> --config <mzm-us.toml>
```

Initial M1B evidence should answer:

- does ROM identity verification pass?
- does BIOS identity verification pass?
- does the runtime reach guest dispatch?
- what is the first MZM PC executed natively?
- are any dispatch misses bridged by the hybrid tier?
- does execution reach cartridge entry `0x08000000`, startup `0x080000C0`, and `agbmain` `0x0800023C`?
- what is the first deterministic stop/crash/divergence, if any?

### M1B result

The first interactive session succeeded with:

```text
cpu_backend=static-recompiled
frames_presented=43921
ppu_frames=43952
native_calls=5193795
unmapped=0
io_unhandled=0

self_heal_coverage=NOT_STATIC
dispatch_misses=18
interpreted_insns=1180314
healed_native=9
failed=9
```

The process remained operational through a long session, proving that the linked host is executing MZM through the static-recompiled backend.

Miss inventory:

```text
BIOS ARM:
0x00000008
0x00000018
0x00000128
0x00000138
0x00000140
0x00000170
0x000001A0
0x000001AC
0x000001B4

high-IWRAM Thumb:
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

The high-IWRAM cluster is consistent with the previously identified stack-local SRAM copied-code mechanism, but the project will not merge those PCs blindly. The generated miss proposal must be reviewed against source/runtime evidence first.

### M1B gate

**PASSED 2026-09-28 — deterministic hybrid native execution demonstrated.**

Hybrid/self-heal was explicitly allowed in M1B. Strict-static closure remains a later gate.

## Minimal host

The first host intentionally excludes:

- launcher UI;
- mods;
- widescreen/extended view;
- achievements;
- packaging;
- game-specific enhancements.

This keeps bootstrap failures attributable to MZM translation/runtime integration rather than optional product surfaces.
