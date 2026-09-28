# M1 — Static Bootstrap

**Started:** 2026-09-28  
**State:** **ACTIVE**

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

### M1B gate

**Deterministic first recompiled MZM execution with trace evidence.**

Hybrid/self-heal is allowed in M1B. Strict-static closure is explicitly not required until later milestone gates.

## Minimal host

The first host intentionally excludes:

- launcher UI;
- mods;
- widescreen/extended view;
- achievements;
- packaging;
- game-specific enhancements.

This keeps bootstrap failures attributable to MZM translation/runtime integration rather than optional product surfaces.
