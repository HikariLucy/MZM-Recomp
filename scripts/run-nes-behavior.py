#!/usr/bin/env python3
"""
Drives tests/m4/nes_behavior_test.cpp (NES-3 behavioural qualification).

  qualify (default, CTest mzm-nes-behavior): three concurrent strict-static runs
    ctl      START (title -> menu), START (menu -> game); no other input
    flow     same + SELECT round trip in the menu, RIGHT held and A in the game
    frontier a longer scripted play session; PINS the current first frontier
             (see docs/M4-NES-METROID.md, NES-3b/3c): Game Over -> password -> save
             runs the copied SramCheckInternal natively at the IWRAM alias
             0x03827110, SaveToSram writes SRAM, the emulator quits through the
             loader reset stub and MZM's ROM restarts; the first miss is then
             0x080006CA (InitializeGame interior). Part 1 resident handlers are
             re-entered natively after the init half was overwritten.
  soak: two runs of N (default 10000) no-input frames; compares every "NES3 " line

Input is injected by the test through bus.io().set_keyinput() (the production
host-poll / input-replay call); guest memory is never written.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

TITLE_600 = "CB0431A65E6BD988"   # NES-2b title frame (same value the NES-2b test recorded)
MENU_790 = "16E0720AC211F04B"    # START / CONTINUE menu
GAME_1300 = "401276D456096B86"   # first gameplay frame stable state


def run(binary, rom, bios, env_extra):
    env = dict(os.environ, **{k: str(v) for k, v in env_extra.items()})
    p = subprocess.run([binary, rom, bios], env=env, capture_output=True, text=True)
    return p.returncode, [l for l in p.stdout.splitlines() if l.startswith("NES3 ")], p.stdout + p.stderr


def hashes(lines):
    out = {}
    for l in lines:
        m = re.match(r"NES3 hash frame=(\d+) .* fb=([0-9A-F]+)", l)
        if m:
            out[int(m.group(1))] = m.group(2)
    return out


def strict_line(lines):
    return next(l for l in lines if l.startswith("NES3 strict "))


def check(cond, msg, failures):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        failures.append(msg)


def clean(lines, failures, label):
    s = strict_line(lines)
    check("dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0" in s, f"{label}: strict counters zero", failures)
    check(any(l.startswith("NES3 DONE") for l in lines), f"{label}: no frontier/stall", failures)
    check(any("host_depth=0 irq_depth=0" in l for l in lines if l.startswith("NES3 final")),
          f"{label}: host return depth 0, IRQ depth 0", failures)
    check(any("resolver_fail=0" in l for l in lines if l.startswith("NES3 final")),
          f"{label}: image resolver failures 0", failures)


def qualify(binary, rom, bios):
    failures = []
    hf = "690,790,850,950,1300,1640,1700,1760,1830"
    common = {"MZM_NES_FRAMES": 1900, "MZM_NES_CHECKPOINT": 950, "MZM_NES_HASH_FRAMES": hf}
    play = []
    f = 1700
    while f < 5600:
        play += [f"{f}:RIGHT:100", f"{f+100}:RIGHT+A:20", f"{f+130}:B:6", f"{f+150}:LEFT:60",
                 f"{f+170}:A:15", f"{f+200}:DOWN:10", f"{f+215}:UP:10"]
        f += 240
    play += ["3010:START:8", "3200:START:8"]
    jobs = {
        "ctl": dict(common, MZM_NES_INPUT="700:START:8,1000:START:8"),
        "flow": dict(common, MZM_NES_INPUT="700:START:8,800:SELECT:8,900:SELECT:8,1000:START:8,"
                                            "1650:RIGHT:100,1800:A:25"),
        "frontier": {"MZM_NES_WATCH_IMAGE_WRITES": 1, "MZM_NES_FRAMES": 3400, "MZM_NES_CHECKPOINT": 1000, "MZM_NES_HASH_FRAMES": "1300",
                     "MZM_NES_INPUT": "700:START:8,1000:START:8," + ",".join(play)},
    }
    with ThreadPoolExecutor(max_workers=3) as ex:
        futs = {k: ex.submit(run, binary, rom, bios, v) for k, v in jobs.items()}
        res = {k: f.result() for k, f in futs.items()}
    ctl, flow, fr = res["ctl"], res["flow"], res["frontier"]
    print("== ctl / flow (strict-static, input via set_keyinput)")
    for name in ("ctl", "flow"):
        check(res[name][0] == 0, f"{name}: exit 0", failures)
        clean(res[name][1], failures, name)
    hc, hfl = hashes(ctl[1]), hashes(flow[1])
    check(hc[690] != hc[790], "START leaves the title (frame hash changes)", failures)
    check(hc[790] == MENU_790 == hc[850] == hc[950], "post-START state is a stable START/CONTINUE menu", failures)
    check(hfl[850] != hfl[790] and hfl[950] == hfl[790], "SELECT toggles the menu and toggles back", failures)
    check(hc[1300] == GAME_1300 and hc[1300] != hc[790], "second START enters the game (state != menu)", failures)
    check(hc[1640] == hfl[1640], "SELECT round trip does not perturb the game", failures)
    check(all(hc[k] != hfl[k] for k in (1700, 1760, 1830)), "RIGHT/A change the game vs the no-input control", failures)
    check(len({hc[1640], hc[1700], hc[1760], hc[1830]}) == 4, "game state evolves without input (not frozen)", failures)
    print("== frontier pin (scripted play session)")
    check(fr[0] == 3, "frontier run exits 3 (stall/miss)", failures)
    stall = next((l for l in fr[1] if l.startswith("NES3 STALL")), "")
    check("frame=3205" in stall and "misses=1" in stall, "first frontier at frame 3205 (" + stall[:70] + ")", failures)
    misses = next((l for l in fr[1] if l.startswith("NES3 stop_misses")), "")
    check('"pc":"0x080006CA"' in misses and '"mode":"thumb"' in misses and '"distinct_misses":1' in misses,
          "first (only) miss is MZM ROM 0x080006CA Thumb (InitializeGame interior, after the NES emulator quit)", failures)
    print("== copied SRAM helper (NES-3c)")
    helper = [l for l in fr[1] if l.startswith("NES3 stack_helper ") and "SramCheckInternal" in l]
    enters = [l for l in helper if " enter " in l]
    exits = [l for l in helper if " exit " in l]
    check(len(enters) == 2 and all("pc=0x03827110" in l for l in enters),
          "SramCheckInternal entered twice through the IWRAM alias 0x03827110", failures)
    check(len(exits) == 2 and all("r0=0x00000000" in l for l in exits),
          "both checks return 0 (the SRAM write verified)", failures)
    check(any(l.startswith("NES3 stack_helper_stats SramCheckInternal attempts=2 matches=2 mirror_matches=2") for l in fr[1]),
          "resolver counted 2 verified mirror matches", failures)
    check("NES3 stack_helper_rejects=0" in fr[1], "no in-window entry was rejected", failures)
    check(any(l.startswith("NES3 sram_diff [0x7FD8,0x8000) changed_bytes=40") for l in fr[1]),
          "SaveToSram wrote exactly SRAM [0x7FD8,0x8000)", failures)
    check(any(l.startswith("NES3 entries sram_SaveToSram") and l.endswith("=1") for l in fr[1]),
          "EmulatorSaveToSram ran once", failures)
    check(any("pc=0x087D813E" in l for l in fr[1] if l.startswith("NES3 stop_trace")),
          "the emulator quit through the loader reset stub 0x087D813E", failures)
    print("== Part 1 resident re-entry (NES-3b)")
    p1 = next((l for l in fr[1] if l.startswith("NES3 part part1")), "")
    m1 = re.search(r"verified=(\d+) matches=(\d+) verify_fail=(\d+)", p1)
    check(m1 and int(m1.group(2)) >= 1 and m1.group(3) == "0",
          "Part 1 entered natively after init was overwritten, no gate failure (" + p1[15:] + ")", failures)
    p6 = next((l for l in fr[1] if l.startswith("NES3 part part6")), "")
    check("verify_fail=0" in p6, "Part 6 gate never failed (sPasswordBytes is excluded, the rest still gated)", failures)
    chunks = {}
    for l in fr[1]:
        m = re.match(r"NES3 p1_write chunk=0x([0-9A-F]+) first_frame=(\d+) first_pc=0x([0-9A-F]+)", l)
        if m:
            chunks[int(m.group(1), 16)] = (int(m.group(2)), int(m.group(3), 16))
    before_quit = {a: v for a, v in chunks.items() if v[0] < 3200}
    check(len(before_quit) == 54 and max(before_quit) < 0x06006DC0 and all(v[0] <= 20 for v in before_quit.values()),
          "before the quit only 54 chunks of [0x06006000,0x06006DC0) were stored to, all from frames 12..18 (graphics)", failures)
    after_quit = {a: v for a, v in chunks.items() if v[0] >= 3200}
    check(all(v[1] == 0x0C08 for v in after_quit.values()) and 0x06006E00 in after_quit and 0x06007200 in after_quit,
          "the resident half [0x06006E00,0x06007240) was first stored to only after the quit, by the BIOS reset (PC 0x00000C08)", failures)
    if failures:
        print(f"NES-3 qualify FAIL ({len(failures)})")
        return 1
    print("NES-3 qualify PASS")
    return 0


def soak(binary, rom, bios, frames):
    env = {"MZM_NES_FRAMES": frames, "MZM_NES_CHECKPOINT": 1000,
           "MZM_NES_HASH_FRAMES": "600,1200,3000,6000,10000"}
    with ThreadPoolExecutor(max_workers=2) as ex:
        a, b = [ex.submit(run, binary, rom, bios, env) for _ in range(2)]
        ra, rb = a.result(), b.result()
    failures = []
    for i, r in enumerate((ra, rb), 1):
        check(r[0] == 0, f"soak run {i}: exit 0", failures)
        clean(r[1], failures, f"soak run {i}")
    check(ra[1] == rb[1], "two runs: every NES3 line identical (hashes, counters, audio)", failures)
    final = next(l for l in ra[1] if l.startswith("NES3 final"))
    print(f"frame hashes: {hashes(ra[1])} final: {final.split('fb=')[1]}")
    print("NES-3 soak PASS" if not failures else f"NES-3 soak FAIL ({len(failures)})")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("rom")
    ap.add_argument("bios")
    ap.add_argument("--mode", choices=["qualify", "soak"], default="qualify")
    ap.add_argument("--frames", type=int, default=10000)
    a = ap.parse_args()
    if a.mode == "soak":
        return soak(a.binary, a.rom, a.bios, a.frames)
    return qualify(a.binary, a.rom, a.bios)


if __name__ == "__main__":
    sys.exit(main())
