# M2B Strict-static Title Qualification

**Date:** 2026-09-28  
**State:** **READY FOR QUALIFICATION**

## Preconditions already demonstrated

A clean-cache normal run has already reported:

```text
self_heal_coverage=FULLY_STATIC
dispatch_misses=0
interpreted_insns=0
healed_native=0
unmapped=0
io_unhandled=0
```

The locally recompiled BIOS is linked and the stack-local SRAM helpers are handled through a byte-verified transient-RAM canonicalizer.

## Final M2B gate

The same route must now be executed with:

```text
GBARECOMP_STRICT_STATIC=1
MZM_MILESTONE_TRACE=1
```

The optional MZM milestone probe uses GBARecomp's generated function-entry observability hook. It records only these semantic anchors:

```text
IntroHandler       0x0808117C
TitleScreenHandler 0x080771A0
```

It synthesizes no input; its per-frame callback returns the inactive GBA keypad mask `0x03FF`.

### Pass criteria

The session must show:

```text
self_heal_coverage=FULLY_STATIC
dispatch_misses=0
interpreted_insns=0
unmapped=0
io_unhandled=0

mzm_milestones intro_handler=YES ...
               title_handler=YES ...
```

and must not abort under `GBARECOMP_STRICT_STATIC=1`.

If these criteria pass, **M2B and M2 Boot/Title may be closed** for the qualified USA route.
