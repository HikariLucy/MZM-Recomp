# Upstreams and Related Work

**Audit date:** 2026-09-28

This document records the projects MZM-Recomp depends on, learns from, or must distinguish itself from.

## 1. Primary upstreams

### metroidret/mzm

Repository: https://github.com/metroidret/mzm

Role:

- semantic/source-level reference;
- symbols and data structures;
- region differences;
- build reproducibility target;
- MZM-specific reverse-engineering knowledge.

Pinned M0 baseline:

```text
43b7fd52f552e4d38c1521ff9d4df5ee57e61493
```

At the audit date upstream reports 2718/2721 functions decompiled and complete tracked non-blob data.

License at the audited revision: **MIT**.

Contribution policy:

- general MZM decomp/source improvements should be proposed upstream when appropriate;
- MZM-Recomp should not become a competing decomp fork.

### mstan/gbarecomp

Repository: https://github.com/mstan/gbarecomp

Role:

- ARM7TDMI static analyzer/recompiler;
- generated C++ emitter;
- shared GBA hardware runtime;
- host integration foundation;
- strict-static/discovery tooling.

Pinned M0 baseline:

```text
e7728148c6829ba526f682876430a0c9022dc6c0
```

M4 integration revision used by the current MZM build:

```text
e0c7cb26c1f3814327ed7872f6e1c33bc7cccb21
```

This local revision combines MOSAIC, WAITCNT, private relocated entry,
non-returning calls (`returns = false`), terminal SoftReset control-flow,
multi-image `[[executable_image]]` architecture, secondary image private_entry
propagation, and VRAM execution support in `g_runtime_ram_dispatch_hook` on the M0 base.
`CMakeLists.txt` and `scripts/generate-m1.sh` check the exact SHA through the supplied
`GBARECOMP_ROOT`; no branch name or local absolute path is part of the dependency pin.

License at the audited revision: **PolyForm Noncommercial License 1.0.0**.

Contribution policy:

- generic GBA/runtime/recompiler fixes belong upstream whenever reasonably separable;
- MZM-Recomp should avoid carrying permanent generic hardware fixes privately.

### jiangzhengwenjz/agbcc

Repository: https://github.com/jiangzhengwenjz/agbcc

Role:

- compiler required for matching reconstruction of the decompilation baseline.

Pinned M0 baseline:

```text
59b966ed1b8f371856dcf99f1546c2fe89c678ca
```

This dependency is primarily relevant to M0 decomp reproducibility, not to the final host executable architecture.

## 2. Important reference integration

### MegaManZeroRecomp

Repository: https://github.com/mstan/MegaManZeroRecomp

Why it matters:

- fast 2D action on GBA;
- ARM/Thumb interworking;
- IRQ and DMA;
- audio;
- SRAM;
- indirect callbacks/jump tables;
- code copied into RAM;
- strict-static closure methodology.

Its patterns should be studied before inventing MZM-specific conventions.

## 3. Existing Zero Mission native work

### Nanikoss/MZM-Reprimed

Repository: https://github.com/Nanikoss/MZM-Reprimed

Public description: an early native Windows PC port/proof of concept.

At the audit date its README reports:

- Windows x64 native executable;
- original MZM game logic initialization;
- keyboard input;
- GBA memory/register compatibility layers;
- native DMA and BIOS decompression replacements;
- data loaded from a user-supplied USA ROM;
- opening intro execution;
- software-rendered intro sprites;
- incomplete rendering/audio/game-mode support.

It targets the same verified USA SHA-1 used by MZM-Recomp.

### Project-positioning rule

MZM-Recomp **must not** claim:

- "first native Metroid: Zero Mission port";
- "first Zero Mission PC project";
- equivalent precedence claims.

MZM-Recomp's intended differentiation is:

- GBARecomp-based static recompilation;
- Linux-first development;
- reproducible cartridge verification;
- decomp-assisted semantic mapping;
- strict-static qualification;
- explicit USA/EU architecture;
- evidence-driven milestone claims.

## 4. Non-equivalent related projects

During the audit, other projects were found that are useful context but not direct duplicates.

Examples include:

- emulator-packaged "PC edition" work built around VBA/VBA-M;
- forks of `metroidret/mzm` whose names contain "native" but remain decomp forks.

These do not remove the value of a GBARecomp integration, but they should be rechecked before major public announcements.

## 5. Pinning policy

Milestone evidence must identify exact upstream commits.

Do not write documentation such as "latest GBARecomp" in a reproducibility claim. Record:

```text
repository
commit SHA
build/tool versions
date
host platform
result
```

Upstream pins may advance deliberately, but an old passing baseline must remain identifiable.

## 6. Ownership matrix

| Change | Preferred destination |
|---|---|
| Generic ARM/Thumb recompiler bug | GBARecomp upstream |
| Generic DMA/IRQ/timer/PPU/audio bug | GBARecomp upstream |
| Generic Linux runtime/packaging improvement | GBARecomp upstream |
| MZM decomp naming/matching/source improvement | metroidret/mzm upstream |
| MZM ROM identity/config | MZM-Recomp |
| MZM code-copy declarations | MZM-Recomp unless framework-general |
| MZM callback/jump-table bounds | MZM-Recomp |
| MZM deterministic route tests | MZM-Recomp |
| MZM-specific enhancement | MZM-Recomp |

## 7. Re-audit points

Repeat the duplicate/upstream audit:

- before M1 implementation is announced publicly;
- before the first downloadable playable build;
- before claiming a novel feature;
- before beginning a large framework change that another project may already have solved.
