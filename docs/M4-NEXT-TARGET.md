# M4 next target: a real haze strict-static checkpoint

The MZM-specific haze resolver now distinguishes all seven DMA-copied
`hazeCode` images by comparing their full 512-byte ROM and RAM windows. Six
variants are called from RAM, and all generated native targets exist. The
[haze audit](M4-HAZE-RAM-CODE.md) records the decomp control flow,
position-independence evidence and local ROM test. The current three M4
regression cases pass but do not execute haze.

**Selected next qualification target:** obtain a legal local checkpoint for
an early BG3 scene, with Brinstar room 12 (`EFFECT_WEAK_ACID`) as the first
topological candidate, and run it
with the haze hook in strict-static mode. Record a hook hit plus zero dispatch
misses, interpreted instructions, unmapped accesses and unhandled IO. Run the
same checkpoint without haze handling to show the dispatch difference. If
that succeeds, add `04_haze_ram_dispatch` to the harness with the checkpoint
kept under ignored `.local/m4-checkpoints/`. A Power Bomb checkpoint should
follow to test expansion→retraction replacement in the same buffer.

`scripts/capture-m4-first-haze.py` and `build-m4-haze` are prepared for the
one manual session. The wrapper waits for a byte-verified native BG3 hit and
requests the snapshot at a clean runtime boundary. See the exact command and
limits in [M4 HAZE RAM CODE](M4-HAZE-RAM-CODE.md). Until that checkpoint
exists, strict-static haze execution, IRQ/resume and Power Bomb remain
unverified; the matrix counts stay unchanged.

Chozodia executable RAM remains the next distinct RAM-code target after haze
execution is qualified. Game Pak prefetch remains a separate timing track
waiting for a shared CPU fetch seam and an independent cycle oracle; the
official upstream pin is unchanged. No work on the next target begins here.
