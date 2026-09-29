# M4 next target: qualify programmed WAITCNT timing

## Problem

MZM writes a nondefault `REG_WAITCNT` value during `InitializeGame`, while the pinned GBARecomp bus timing path uses default cartridge/SRAM access cycles. The next bounded task is to establish the expected programmed timing and a generic synthetic differential test before proposing a runtime fix. This does not require a late-game checkpoint.

## Evidence

`mzm_us.elf` disassembly and `src/init_game.c` show the WAITCNT write immediately after interrupt setup. The pin defines/stores the MMIO register, but `src/gba/gba_bus.cpp::access_cycles` currently uses default waitstate costs. The M4 matrix marks timing PARTIAL. The `0x080006CA` strict-static probe was a VBlank-unwind artifact: disabling that yield lets 1000 steps finish with zero counters, so it is not the next static-coverage target. The local MOSAIC branch passes 48/48 synthetic upstream tests, but MZM has no verified active MOSAIC scene; its remaining qualification needs runtime scene evidence.

## Ownership

GBARecomp owns generic cartridge/SRAM timing and prefetch behavior in `src/gba/gba_bus.cpp` and related IO state. MZM-Recomp owns recording the concrete WAITCNT value used by the USA build and checking strict-static regressions. No ROM-address or game-specific timing shortcut belongs upstream.

## Proposed fix

First add synthetic bus tests that write distinct WAITCNT SRAM/WS0/WS1/WS2 values and assert sequential/nonsequential access cycles. Record the current RED differences. Audit the prefetch enable bit and bus-sequence state separately before changing timing. After expectations are confirmed against GBATEK/hardware behavior, implement generic register-dependent timing upstream in an isolated branch. This document selects the target; implementation has not started.

## Validation plan

1. Decode MZM's programmed WAITCNT value from the verified decomp/ROM build.
2. Compare synthetic cycle counts across default and programmed values, including sequential accesses and SRAM, against a trusted hardware reference.
3. Run GBARecomp bus/PPU/full tests and the two M4 strict-static cases after any future generic fix.
4. Keep prefetch separately qualified if its pipeline behavior cannot be shown by access-cycle tests alone.

## Risk

Changing timing can alter IRQ/DMA/audio phase even if static dispatch remains zero. The current M4 passive cases are only boot gates, so gameplay timing still needs its own oracle. The official GBARecomp pin remains unchanged until an explicit update workstream.
