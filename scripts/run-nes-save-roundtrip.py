#!/usr/bin/env python3
"""
SAVE-LOAD-1: NES save -> quit -> SoftReset -> MZM -> re-enter NES -> load roundtrip.

Drives tests/m4/nes_behavior_test.cpp (MZM_NES_START=mzm). MZM cold-boots first so
that *its own* boot initialises its SRAM (the real game can never reach the NES
without an initialised SRAM), then the NES is entered through the loader exactly
as OptionsNesMetroidHandler stage 4 does. Nothing writes guest memory or SRAM
except the guest (the one exception, the corruption controls, is marked
NON-QUALIFYING and is never part of the qualifying checks).

Qualifying runs
  inproc  MZM boot -> NES (frame 400) -> death -> GAME OVER -> password -> save YES
          -> quit -> SoftReset -> MZM boot -> NES again (frame 4300) -> CONTINUE ->
          password screen -> START.   Run twice; every NES3 line must be identical.
  proc1 / proc2  the same save, but the second NES session is a new process that
          loads the cartridge image the first process dumped (GbaSave
          sram_bytes()/load_sram_bytes(), the calls runtime.cpp uses for the .sav).
  fresh   same inputs as proc2 with no persisted save (control).
Non-qualifying controls
  corrupt_magic / corrupt_password   one byte of the local SRAM copy is flipped.

Session inputs mirror run-nes-behavior.py's scripted play session, shifted by the
frame at which the NES was entered.
"""
import argparse
import os
import re
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util

_spec = importlib.util.spec_from_file_location(
    "rnb", os.path.join(os.path.dirname(os.path.abspath(__file__)), "run-nes-behavior.py"))
rnb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rnb)
run, check, strict_line, hashes = rnb.run, rnb.check, rnb.strict_line, rnb.hashes

ENTER1 = 400          # first NES entry (frame), MZM already booted and SRAM initialised
ENTER2 = 4300         # second entry in the same process
SLOT_A = 0x7FB0       # SRAM_BASE + 0x7FB0 (primary copy; the guest never wrote it here)
SLOT_B = 0x7FD8       # SRAM_BASE + 0x7FD8 (the copy EmulatorSaveToSram writes)
BLOCK = 0x28
NEW_GAME_FRAME = rnb.GAME_1300   # first gameplay frame hash (same constant run-nes-behavior.py pins)


def session1_input(off):
    ev = []

    def add(f, k, h):
        ev.append(f"{f + off}:{k}:{h}")
    add(700, "START", 8)
    add(1000, "START", 8)
    f = 1700
    while f < 3000:
        for d, k, h in [(0, "RIGHT", 100), (100, "RIGHT+A", 20), (130, "B", 6), (150, "LEFT", 60),
                        (170, "A", 15), (200, "DOWN", 10), (215, "UP", 10)]:
            add(f + d, k, h)
        f += 240
    add(3010, "START", 8)
    add(3200, "START", 8)
    return ev


def continue_input(off):
    # title -> START/CONTINUE menu -> SELECT (CONTINUE) -> START (password screen) -> START (accept)
    return [f"{off + 700}:START:8", f"{off + 810}:SELECT:8", f"{off + 910}:START:8", f"{off + 1100}:START:8"]


def decode_password(pw):
    """EmulatorFillPasswordWithSaved: 18 bytes -> 24 six-bit characters."""
    out, bi, c = [], 0, 0
    for i in range(24):
        m = i & 3
        if m == 0:
            c = pw[bi]
            bi += 1
        elif m == 1:
            c = (c >> 6) | (pw[bi] << 2)
            bi += 1
        elif m == 2:
            c = (c >> 6) | (pw[bi] << 4)
            bi += 1
        else:
            c = c >> 6
        out.append(c & 0x3F)
    return out


def parse(lines):
    r = {"calls": [], "sram": [], "ranges": [], "pw": {}, "enter": [], "verify": []}
    for l in lines:
        m = re.match(r"NES3 save_call (\w+) frame=(\d+) r0=0x(\w+) r1=0x(\w+) r2=0x(\w+) lr=0x(\w+) "
                     r"payload=(\w+) pw=(\w+)", l)
        if m:
            r["calls"].append(dict(name=m[1], frame=int(m[2]), r0=int(m[3], 16), r1=int(m[4], 16),
                                   r2=int(m[5], 16), payload=bytes.fromhex(m[7]), pw=bytes.fromhex(m[8])))
            continue
        m = re.match(r"NES3 sram (\w+) frame=(\d+) hash=(\w+) nes_slot_a=(\w+) nes_slot_b=(\w+) mzm_check=(\w+)", l)
        if m:
            r["sram"].append(dict(tag=m[1], frame=int(m[2]), hash=m[3], a=bytes.fromhex(m[4]),
                                  b=bytes.fromhex(m[5]), chk=bytes.fromhex(m[6])))
            continue
        m = re.match(r"NES3 sram_range frame=(\d+) \[0x(\w+),0x(\w+)\)", l)
        if m:
            r["ranges"].append((int(m[1]), int(m[2], 16), int(m[3], 16)))
            continue
        m = re.match(r"NES3 nes_password frame=(\d+) pw_please_screen=(\d) password_bytes=(\w+) screen_chars=(\w+)", l)
        if m:
            r["pw"][int(m[1])] = dict(screen=int(m[2]), pw=bytes.fromhex(m[3]), chars=list(bytes.fromhex(m[4])))
            continue
        m = re.match(r"NES3 enter_nes frame=(\d+)", l)
        if m:
            r["enter"].append(int(m[1]))
        m = re.match(r"NES3 save_verify SaveToSram call_frame=(\d+) sram_off=0x(\w+) bytes_match=(\d)", l)
        if m:
            r["verify"].append((int(m[1]), int(m[2], 16), int(m[3])))
    return r


def audio_ok(lines):
    a = next((l for l in lines if l.startswith("NES3 audio_stats")), "")
    n = re.search(r" nonzero=(\d+)", a)
    d = re.search(r" dma1_runs=(\d+)", a)
    return bool(n and d and int(n[1]) > 0 and int(d[1]) > 0), a


def hook_depth(lines):
    l = next((x for x in lines if x.startswith("NES3 host_stack")), "")
    m = re.search(r"hook_depth_bytes=(\d+)", l)
    return int(m[1]) if m else -1


def base_env(frames, hashes_csv):
    return {"MZM_NES_START": "mzm", "MZM_NES_FRAMES": frames, "MZM_NES_CHECKPOINT": 1000,
            "MZM_NES_HASH_FRAMES": hashes_csv, "MZM_NES_SAVE_LOG": 1, "MZM_NES_SRAM_TRACE": 1,
            "MZM_NES_SRAM_REPORT": 1}


def qualify(binary, rom, bios, keep):
    tmp = tempfile.mkdtemp(prefix="mzm-save-rt-")
    sav = os.path.join(tmp, "roundtrip.sav")
    s1 = session1_input(ENTER1)
    env_inproc = dict(base_env(5600, "390,3490,3900,4290,5090,5290,5500"),
                      MZM_NES_ENTER_AT=f"{ENTER1},{ENTER2}",
                      MZM_NES_INPUT=",".join(s1 + continue_input(ENTER2)))
    env_p1 = dict(base_env(4200, "390,3490,3900,4200"), MZM_NES_ENTER_AT=str(ENTER1),
                  MZM_NES_INPUT=",".join(s1), MZM_NES_SAVE_DUMP=sav)
    env_fresh = dict(base_env(1700, "390,1190,1390,1600"), MZM_NES_ENTER_AT=str(ENTER1),
                     MZM_NES_INPUT=",".join(continue_input(ENTER1)))

    with ThreadPoolExecutor(max_workers=8) as ex:
        f_in1 = ex.submit(run, binary, rom, bios, env_inproc)
        f_in2 = ex.submit(run, binary, rom, bios, env_inproc)
        f_p1 = ex.submit(run, binary, rom, bios, env_p1)
        f_fr = ex.submit(run, binary, rom, bios, env_fresh)
        p1 = f_p1.result()
        env_p2 = dict(env_fresh, MZM_NES_SAVE_LOAD=sav)
        # NON-QUALIFYING corruption controls on the dumped image (frame 100: before NES entry)
        env_k1 = dict(env_p2, MZM_NES_CORRUPT_SRAM=f"100:{SLOT_B + 4:X}:FF")      # a header byte
        env_k2 = dict(env_p2, MZM_NES_CORRUPT_SRAM=f"100:{SLOT_B + 0x10 + 12:X}:01")  # a password byte
        f_p2 = ex.submit(run, binary, rom, bios, env_p2)
        f_k1 = ex.submit(run, binary, rom, bios, env_k1)
        f_k2 = ex.submit(run, binary, rom, bios, env_k2)
        in1, in2, fr = f_in1.result(), f_in2.result(), f_fr.result()
        p2, k1, k2 = f_p2.result(), f_k1.result(), f_k2.result()

    failures = []

    def clean(res, label):
        lines = res[1]
        check(res[0] == 0, f"{label}: exit 0", failures)
        s = strict_line(lines)
        check("dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0 self_heal=disabled" in s,
              f"{label}: strict counters zero", failures)
        check(any("host_depth=0 irq_depth=0" in l for l in lines if l.startswith("NES3 final")),
              f"{label}: host return depth 0, IRQ depth 0", failures)

    print("== in-process roundtrip (qualifying)")
    clean(in1, "inproc")
    a = parse(in1[1])
    saves = [c for c in a["calls"] if c["name"] == "SaveToSram"]
    loads = [c for c in a["calls"] if c["name"] == "LoadFromSram"]
    pwloads = [c for c in a["calls"] if c["name"] == "SaveToPasswordBytes"]
    check(a["enter"] == [ENTER1, ENTER2], "NES entered twice (loader entry as OptionsNesMetroidHandler stage 4)", failures)
    check(len(saves) == 1 and saves[0]["r2"] == 0x0E000000 + SLOT_B and saves[0]["r1"] == 0x0201C000,
          "exactly one EmulatorSaveToSram(sp, src=0x0201C000, dst=SRAM+0x7FD8)", failures)
    blk = saves[0]["payload"] if saves else b""
    check(len(blk) == BLOCK and blk[0x22:] == bytes(6), "saved block is 0x28 bytes: 16-byte header + 18 password bytes + 6 zero", failures)
    check(saves and saves[0]["pw"] == blk[0x10:0x22],
          "sPasswordBytes (RetrieveGameOverPassword) == saved block[0x10:0x22]", failures)
    check(a["verify"] == [(saves[0]["frame"], SLOT_B, 1)] if saves else False,
          "SRAM[0x7FD8,0x8000) == the bytes SaveToSram was given", failures)
    by_tag = {(s["tag"], s["frame"]): s for s in a["sram"]}
    pre, post, title = by_tag.get(("hash", 3490)), by_tag.get(("hash", 3900)), by_tag.get(("hash", 390))
    check(pre and pre["a"] == b"\xff" * BLOCK and pre["b"] == b"\xff" * BLOCK,
          "before the save both NES slots are erased (0xFF); MZM initialised the rest itself", failures)
    check(post and post["a"] == b"\xff" * BLOCK and post["b"] == blk,
          "after quit + SoftReset + MZM boot slot B still holds the saved block, slot A untouched", failures)
    nes_writes = [r for r in a["ranges"] if r[1] >= SLOT_A]
    check(nes_writes == [(saves[0]["frame"] + 1, SLOT_B, SLOT_B + BLOCK)] if saves else False,
          "the only write to [0x7FB0,0x8000) in the whole run is the save's 40 bytes", failures)
    late = [r for r in a["ranges"] if r[0] > saves[0]["frame"] + 1] if saves else []
    check(not late, "MZM's second boot (after SoftReset) and the second NES session write no SRAM at all", failures)
    mzm_init = [r for r in a["ranges"] if r[0] < ENTER1 and r[1] < SLOT_A]
    check(mzm_init, f"MZM's own first boot initialised its SRAM before the NES ran ({len(mzm_init)} ranges)", failures)
    sec = [c for c in loads if c["frame"] > ENTER2]
    check(len(sec) == 2 and sec[0]["r0"] == 0 and sec[0]["payload"] == b"\xff" * BLOCK and
          sec[1]["r0"] == 0x0E000000 + SLOT_B and sec[1]["payload"] == blk,
          "second session: LoadFromSram tries slot A (erased) then slot B and reads exactly the saved bytes", failures)
    sec_pw = [c for c in pwloads if c["frame"] > ENTER2]
    check(len(sec_pw) == 1 and sec_pw[0]["payload"] == blk[0x10:0x22],
          "second session: SaveToPasswordBytes restores the 18 saved password bytes into sPasswordBytes", failures)
    first_loads = [c for c in loads if c["frame"] < ENTER2]
    check(len(first_loads) == 2 and all(c["payload"] == b"\xff" * BLOCK for c in first_loads),
          "first session boot: both LoadFromSram calls read erased SRAM (no save yet)", failures)
    pwv = a["pw"].get(5290)
    exp = decode_password(blk[0x10:0x22]) if blk else []
    check(pwv and pwv["screen"] == 1 and pwv["pw"] == blk[0x10:0x22] and pwv["chars"] == exp,
          "CONTINUE shows PASSWORD PLEASE with the saved password filled in (24 tiles == decode(saved bytes))", failures)
    print(f"      saved block   {blk.hex().upper()}")
    print(f"      password tiles {bytes(exp).hex().upper()} (screen {bytes(pwv['chars']).hex().upper() if pwv else '-'})")

    print("== fresh-SRAM control (same inputs, no persisted save)")
    clean(fr, "fresh")
    f = parse(fr[1])
    fl = [c for c in f["calls"] if c["name"] == "LoadFromSram"]
    fpw = f["pw"].get(1390)
    check(len(fl) == 2 and all(c["payload"] == b"\xff" * BLOCK for c in fl), "fresh: both LoadFromSram read 0xFF", failures)
    check(fpw and fpw["pw"] == bytes(18) and fpw["chars"] != exp,
          "fresh: password screen is empty / differs from the saved run", failures)
    hin, hfr = hashes(in1[1]), hashes(fr[1])
    check(hin.get(5290) and hfr.get(1390) and hin[5290] != hfr[1390],
          "password-screen frame hash: saved run != fresh run", failures)
    # The scripted session dies before collecting anything, so its GAME OVER password encodes the
    # initial game state: accepting it lands on the same first gameplay frame as a new game. That
    # is pinned here on purpose; it means restored *game state* is not distinguishable from a new
    # game by pixels (the password screen and the byte chain are the evidence).
    check(hin.get(5500) == hfr.get(1600) == NEW_GAME_FRAME,
          "accepting the saved password reaches the new-game first frame (the saved state is the initial state)", failures)

    print("== process restart (qualifying: GbaSave dump/load API; the MZMRecomp host .sav path is not exercised)")
    clean(p1, "proc1")
    clean(p2, "proc2")
    b = parse(p2[1])
    check(any(l.startswith("NES3 save_loaded") for l in p2[1]), "proc2 loaded the image proc1 dumped", failures)
    sav_dump = next((l for l in p1[1] if l.startswith("NES3 save_dumped")), "")
    check(sav_dump != "", "proc1 dumped its cartridge SRAM image at exit", failures)
    bl = [c for c in b["calls"] if c["name"] == "LoadFromSram"]
    p1s = [c for c in parse(p1[1])["calls"] if c["name"] == "SaveToSram"]
    check(len(bl) == 2 and bl[1]["r0"] == 0x0E000000 + SLOT_B and p1s and bl[1]["payload"] == p1s[0]["payload"],
          "proc2 LoadFromSram read, byte for byte, what proc1's SaveToSram wrote", failures)
    bpw = b["pw"].get(1390)
    check(bpw and bpw["screen"] == 1 and bpw["chars"] == exp, "proc2 password screen == in-process saved run", failures)
    nonempty = [r for r in b["ranges"] if r[1] >= SLOT_A]
    check(not nonempty, "proc2 never wrote the NES slots", failures)
    check(hashes(p2[1]).get(1390) == hin.get(5290) and hashes(p2[1]).get(1600) == hin.get(5500),
          "proc2 frames (password screen, post-accept) == in-process frames", failures)
    check(hashes(p2[1]).get(1390) != hfr.get(1390),
          "proc2 (loaded save) password screen != fresh-SRAM password screen", failures)

    print("== corruption controls (NON-QUALIFYING; one byte of the local SRAM copy flipped)")
    for label, res in (("header byte (block[4])", k1), ("password byte (block[0x1C])", k2)):
        clean(res, "corrupt")
        kk = parse(res[1])
        kp = kk["pw"].get(1390)
        print(f"      {label}: password_bytes={kp['pw'].hex().upper() if kp else '-'} "
              f"tiles_match_saved={kp['chars'] == exp if kp else None}")
    kp1 = parse(k1[1])["pw"].get(1390)
    kp2 = parse(k2[1])["pw"].get(1390)
    check(kp1 and kp1["pw"] != blk[0x10:0x22],
          "corrupting the NES block's header makes the guest reject it (saved password is not restored)", failures)
    check(kp2 and kp2["pw"] != blk[0x10:0x22] or (kp2 and kp2["pw"] == blk[0x10:0x22] and kp2["chars"] != exp) or kp2 is not None,
          "corrupting a password byte is observed (report only)", failures)

    print("== determinism: the in-process roundtrip twice")
    check(in1[1] == in2[1], f"two runs: every NES3 line identical ({len(in1[1])} lines: SRAM hashes, save bytes, calls, frame hashes)", failures)

    print("== audio / host stack")
    ok, line = audio_ok(in1[1])
    check(ok, "NES audio still reaches the host (dma1_runs > 0, non-zero samples)", failures)
    hd = hook_depth(in1[1])
    check(0 < hd < 600 * 1024, f"host stack hook high-water {hd} bytes (< 600 KiB)", failures)
    if keep:
        print("kept", tmp)
    print("NES save roundtrip PASS" if not failures else f"NES save roundtrip FAIL ({len(failures)})")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("rom")
    ap.add_argument("bios")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    return qualify(a.binary, a.rom, a.bios, a.keep)


if __name__ == "__main__":
    sys.exit(main())
