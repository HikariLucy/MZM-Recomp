# M0 Exit Decision — GO

**Project:** MZM-Recomp  
**Date:** 2026-09-28  
**Decision:** **GO — proceed to M1 Static Bootstrap**

## Scope of this decision

M0 answered one question:

> Is the verified USA release of Metroid: Zero Mission a technically viable, reproducible GBARecomp target under the project's legal/distribution boundaries?

The answer is **yes**.

This decision does **not** claim that the game is playable, that the generated C++ already links into a complete host application, or that strict-static runtime closure has been achieved.

## Evidence summary

### Cartridge / decomp

- USA cartridge: `BMXE`, revision 0, 8 MiB.
- SHA-1: `5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8`.
- Pinned `metroidret/mzm` rebuild is byte-for-byte identical to the verified ROM.

### Semantic layer

- reproducible ROM address → ARM/Thumb/Data → symbol → object/source mapping;
- boot, IRQ, HALT/VBlank, Intro, Title and File Select anchors captured;
- 2,722 decomp function seeds imported into GBARecomp.

### Framework baseline

Pinned GBARecomp:

```text
e7728148c6829ba526f682876430a0c9022dc6c0
```

Linux qualification:

```text
Build: PASS
CTest: 34/34 PASS
SDL2: 2.30.0
gba_recompile: OK
gba_scan: OK
bios_smoke: OK
```

### Static analysis

First configured MZM discovery/codegen pass found no undefined CPU instructions.

Final M0.6 regeneration:

```text
discovered=28411 translation roots
ARM=82
Thumb=28329
indirect=9560
undefined=0
branch_targets=99930
auto_jump_tables=335
auto_jump_targets=8675
code_copies=5
TOTAL emitted=28411
codegen_shards=16
exit=0
```

### Fixed executable-RAM copies represented

```text
0x03000C7C <- 0x08000104  gInterruptCode / IntrMain       ARM
0x03003B90 <- 0x08004464  gSoundCodeA / CallSoundCodeA   Thumb
0x030041EC <- 0x08004310  gSoundCodeB / CallSoundCodeB   Thumb
0x03004294 <- 0x080043B4  gSoundCodeC / CallSoundCodeC   Thumb
0x030016C4 <- 0x08057F7C  clipdataCode / ClipdataConvertToCollision Thumb
```

The final generated corpus contains a dispatch entry for `0x030016C4`, confirming that the normal-gameplay clipdata copy is materialized as native translated code.

## Residual risks accepted into later milestones

These are real issues, but none is an M0 no-go:

| Risk | Disposition |
|---|---|
| PPU mosaic rendering missing | generic GBARecomp PPU work; validate before visually accurate affected scenes |
| multiple haze routines overwrite one RAM PC | hybrid allowed during bring-up; generic variant-aware static solution required before strict-static closure |
| WAITCNT/prefetch accuracy | differential/timing validation in runtime milestones |
| SRAM stack-local copied helpers | hybrid/observed-target work before strict-static save-path closure |
| Chozodia HBlank RAM copy | M4 compatibility |
| NES Metroid dynamic IWRAM/EWRAM/VRAM code | dedicated M4 subproject |
| Europe differences | secondary target after USA architecture is proven |

## Exit-criteria decision

All M0 exit criteria are satisfied:

1. byte-identical USA decomp rebuild — **PASS**;
2. clean pinned GBARecomp Linux baseline — **PASS**;
3. no fundamental normal boot/gameplay execution blocker — **PASS**;
4. ordinary gaps have explicit ownership/acceptance plans — **PASS**;
5. repository excludes ROM/BIOS/generated copyrighted outputs — **PASS**;
6. related/upstream projects and differentiation are documented — **PASS**.

## Transition

**M0 is closed.**

The active milestone is now:

> **M1 — Static Bootstrap**

The next engineering objective is not more feasibility research. It is to compile the generated MZM translation corpus together with the pinned GBARecomp runtime into the first Linux host executable, then prove deterministic execution of the first recompiled MZM instruction stream.
