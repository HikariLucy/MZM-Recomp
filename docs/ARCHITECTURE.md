# Architecture

## 1. Design principle

MZM-Recomp separates four concerns that are easy to accidentally mix:

1. **Original cartridge behavior** — authoritative machine code/data from the user's legally obtained ROM.
2. **Semantic understanding** — names, structures and source-level knowledge from `metroidret/mzm`.
3. **Static recompilation/runtime** — GBARecomp analyzer, emitter and shared hardware model.
4. **MZM-specific integration** — configuration, bounded target declarations, code-copy metadata, tests and narrowly scoped compatibility glue.

The project should not fork large upstream components merely for convenience.

## 2. Intended runtime pipeline

```text
                  +----------------------+
                  | Verified MZM ROM     |
                  | USA first / EU later |
                  +----------+-----------+
                             |
                             v
                  +----------------------+
                  | GBARecomp analyzer   |
                  | ARM / Thumb / CFG    |
                  +----------+-----------+
                             |
                  static code discovery
                             |
                             v
                  +----------------------+
                  | generated C++        |
                  | LOCAL / untracked    |
                  +----------+-----------+
                             |
                             +-------------------+
                             |                   |
                             v                   v
                  +------------------+   +-------------------+
                  | shared GBA       |   | MZM integration   |
                  | hardware runtime |   | config / metadata |
                  +--------+---------+   +---------+---------+
                           |                       |
                           +-----------+-----------+
                                       |
                                       v
                              +----------------+
                              | native host    |
                              | Linux first    |
                              +----------------+
```

## 3. Semantic side channel

The decompilation is not the executable input to static recompilation. It is used to turn opaque addresses into engineering meaning.

Target tooling should provide:

```text
0x080xxxxx
   ↓
ARM/Thumb state
   ↓
symbol
   ↓
decomp source file / function
   ↓
recomp metadata / tests / notes
```

This prevents repeated blind investigation of raw addresses.

## 4. Repository layout

Target layout:

```text
MZM-Recomp/
├── README.md
├── STATUS.md
├── ROADMAP.md
├── configs/
│   ├── mzm-us.toml
│   └── mzm-eu.toml
├── runtime/
│   └── ... MZM-specific host/runtime integration only
├── scripts/
│   ├── verify-rom.*
│   ├── sync-upstreams.*
│   ├── build-generated.*
│   └── validation tooling
├── tests/
│   ├── static/
│   ├── runtime/
│   └── differential/
└── docs/
    ├── M0-FEASIBILITY.md
    ├── ARCHITECTURE.md
    ├── UPSTREAMS.md
    ├── LEGAL.md
    └── BUILD-LINUX.md
```

Generated C++, extracted assets, ROMs, BIOS and saves remain outside tracked source.

## 5. Region architecture

USA and Europe should share the runtime/integration architecture while keeping region-specific identity and control-flow metadata explicit.

Preferred model:

```text
shared runtime + shared tests
          |
          +-- configs/mzm-us.toml
          |
          +-- configs/mzm-eu.toml
```

Region-specific data may include:

- ROM hashes;
- entry points;
- function addresses;
- callback/jump-table addresses;
- copied-code source/destination ranges;
- resume aliases;
- symbols;
- validated route traces.

A region should not be implemented by scattering USA/EU address checks throughout host runtime code.

## 6. Boot path to qualify

Initial semantic path:

```text
cartridge entry
   ↓
startup / crt0
   ↓
agbmain
   ↓
InitializeGame
   ├── memory clear
   ├── graphics RAM clear
   ├── LoadInterruptCode
   │      └── IntrMain ROM → IWRAM
   ├── VBlank callback
   ├── SRAM read
   ├── audio init
   └── IRQ configuration
   ↓
main loop
   ├── game-mode dispatch
   ├── input
   ├── audio
   └── HALT → VBlank/IRQ wake
```

M2 is specifically about making this execution path correct.

## 7. Static control-flow closure

MZM is expected to need explicit metadata for:

- function pointers;
- callback tables;
- jump tables;
- copied executable code;
- RAM entry points;
- interior resume points;
- ARM/Thumb aliases;
- IRQ resume addresses.

The workflow is:

1. discover using static analysis plus bounded runtime observation;
2. map addresses to decomp semantics;
3. encode the smallest correct static target set;
4. regenerate;
5. validate;
6. enable strict-static mode for the qualified route.

The interpreter/self-healing tier is a **discovery tool**, not proof of static completeness.

## 8. Code-copy handling

MZM copies executable interrupt code into IWRAM during startup. Later compatibility work, especially NES Metroid, introduces more dynamic code placement.

Principles:

- describe known copies declaratively where the framework supports it;
- do not replace original game behavior with host shortcuts without a documented reason;
- preserve source/destination semantics;
- record ARM/Thumb state for copied entry points;
- test that strict-static execution can enter the copied ranges correctly.

## 9. Hardware ownership

The default ownership rule is:

**Generic GBA behavior belongs upstream in GBARecomp.**

Examples:

- PPU timing;
- DMA semantics;
- timer behavior;
- IRQ scheduling;
- serial hardware;
- cartridge save devices.

**MZM-specific knowledge belongs here.**

Examples:

- verified cartridge identity;
- MZM code-copy declarations;
- MZM callback target bounds;
- MZM route tests;
- region-specific symbol/address maps;
- MZM-specific presentation enhancements.

## 10. Validation architecture

Three layers are planned.

### Static validation

- expected ROM identity;
- config schema;
- target address ranges;
- ARM/Thumb alignment/state;
- symbol resolution;
- generated-code compilation.

### Runtime validation

- dispatch misses;
- interpreted-instruction count where available;
- unmapped bus access;
- unhandled I/O;
- IRQ/DMA/timer traces;
- save persistence;
- audio/video sanity.

### Differential validation

Where practical, compare a deterministic route against a trusted GBA reference execution:

- memory checkpoints;
- registers;
- relevant I/O state;
- frame hashes/screenshots;
- save data;
- event traces.

## 11. Faithful path vs enhancements

The original 240×160 behavior is the validation baseline.

Enhancements must be layered so they can be disabled. Widescreen, high refresh, save states and other host features must never become dependencies for basic compatibility tests.
