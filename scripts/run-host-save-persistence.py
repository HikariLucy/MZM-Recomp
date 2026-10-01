#!/usr/bin/env python3
"""
Disk persistence of the NES save through the shipped MZMRecomp host binary.

  1. harness process P1 plays the real NES route to the GAME OVER save (guest-written
     SRAM, nothing fabricated) and dumps its cartridge SRAM image (GbaSave API);
  2. the SHIPPED host binary boots MZM (headless, strict-static) beside a ROM copy whose
     `.sav` is that image: it must report save_loaded 32768/32768, save_flushed, and the
     NES region [0x7FB0,0x8000) of the file it flushes must equal the loaded one; a second
     host process must leave it unchanged again;
  3. harness process P2 loads the file the host flushed (not the original dump), re-enters
     the NES, CONTINUE shows the saved password; its frames equal a control P2 that loads the
     original dump directly.

Not covered: the shipped host *writing* the NES save (the NES route cannot be entered through
the host binary without a completed game).
"""
import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name, fname):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fname))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


rt = load("rt", "run-nes-save-roundtrip.py")
rnb = rt.rnb
check = rnb.check
NES_LO, SRAM_SIZE = 0x7FB0, 0x8000


def host_run(host, config, rom, bios, workdir, frames):
    env = dict(os.environ, GBARECOMP_STRICT_STATIC="1")
    p = subprocess.run([host, "--bios", bios, "--rom", rom, "--config", config, "--frames",
                        str(frames)], cwd=workdir, env=env, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("harness")
    ap.add_argument("host")
    ap.add_argument("config")
    ap.add_argument("rom")
    ap.add_argument("bios")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    for k in ("harness", "host", "config", "rom", "bios"):
        setattr(a, k, os.path.abspath(getattr(a, k)))
    tmp = tempfile.mkdtemp(prefix="mzm-host-sav-")
    failures = []
    dump = os.path.join(tmp, "p1.sav")
    s1 = rt.session1_input(rt.ENTER1)
    env_p1 = dict(rt.base_env(4200, "390,3490,3900,4200"), MZM_NES_ENTER_AT=str(rt.ENTER1),
                  MZM_NES_INPUT=",".join(s1), MZM_NES_SAVE_DUMP=dump)
    print("== P1: NES save in the harness (guest-written SRAM)")
    p1 = rnb.run(a.harness, a.rom, a.bios, env_p1)
    check(p1[0] == 0, "P1 exit 0", failures)
    saved = open(dump, "rb").read() if os.path.exists(dump) else b""
    check(len(saved) == SRAM_SIZE, "P1 dumped a 32 KiB cartridge image", failures)
    blk = saved[rt.SLOT_B:rt.SLOT_B + rt.BLOCK]
    check(len(blk) == rt.BLOCK and blk != b"\xff" * rt.BLOCK, "slot B holds a non-erased NES save", failures)
    if failures:
        print("host save persistence FAIL (precondition)")
        return 1

    print("== shipped host binary: .sav load -> MZM boot -> flush (twice)")
    hdir = os.path.join(tmp, "host")
    os.makedirs(hdir)
    rom_copy = os.path.join(hdir, "game.gba")
    shutil.copyfile(a.rom, rom_copy)
    sav = os.path.join(hdir, "game.sav")
    shutil.copyfile(dump, sav)
    rc1, out1 = host_run(a.host, a.config, rom_copy, a.bios, hdir, 600)
    check(rc1 == 0, "host run 1 exit 0", failures)
    check(re.search(r"save_loaded path=\S+ size=32768/32768", out1) is not None,
          "host run 1 loaded the 32768-byte .sav from disk", failures)
    check("save_flushed" in out1, "host run 1 flushed the .sav back to disk", failures)
    check("dispatch_misses=0 interpreted_insns=0" in out1, "host run 1 strict-static clean", failures)
    after1 = open(sav, "rb").read()
    check(len(after1) == SRAM_SIZE, "flushed file is 32768 bytes", failures)
    check(after1[NES_LO:] == saved[NES_LO:], "NES region [0x7FB0,0x8000) survives host load + MZM boot + flush byte for byte", failures)
    check(after1[rt.SLOT_B:rt.SLOT_B + rt.BLOCK] == blk, "slot B still equals the block SaveToSram wrote", failures)
    shutil.copyfile(sav, os.path.join(tmp, "host1.sav"))
    rc2, out2 = host_run(a.host, a.config, rom_copy, a.bios, hdir, 600)
    check(rc2 == 0 and "save_loaded" in out2, "host run 2 loaded the file written by run 1", failures)
    after2 = open(sav, "rb").read()
    check(after2[NES_LO:] == saved[NES_LO:], "NES region unchanged after the second host process", failures)
    # control: a fresh .sav is not read (no save_loaded) and has no NES save
    fresh = os.path.join(tmp, "fresh")
    os.makedirs(fresh)
    shutil.copyfile(a.rom, os.path.join(fresh, "game.gba"))
    rcf, outf = host_run(a.host, a.config, os.path.join(fresh, "game.gba"), a.bios, fresh, 600)
    check(rcf == 0 and "save_loaded" not in outf, "control: fresh run found no .sav to load", failures)
    freshsav = open(os.path.join(fresh, "game.sav"), "rb").read()
    check(freshsav[rt.SLOT_B:rt.SLOT_B + rt.BLOCK] != blk, "control: a fresh .sav does not contain the NES save", failures)

    print("== P2: NES CONTINUE restores the save from the file the host flushed")
    env_fresh = dict(rt.base_env(1700, "390,1190,1390,1600"), MZM_NES_ENTER_AT=str(rt.ENTER1),
                     MZM_NES_INPUT=",".join(rt.continue_input(rt.ENTER1)))
    env_host = dict(env_fresh, MZM_NES_SAVE_LOAD=os.path.join(tmp, "host1.sav"))
    env_direct = dict(env_fresh, MZM_NES_SAVE_LOAD=dump)
    with ThreadPoolExecutor(max_workers=2) as ex:
        fh = ex.submit(rnb.run, a.harness, a.rom, a.bios, env_host)
        fd = ex.submit(rnb.run, a.harness, a.rom, a.bios, env_direct)
        ph, pd = fh.result(), fd.result()
    for label, r in (("P2(host file)", ph), ("P2(direct dump)", pd)):
        check(r[0] == 0, f"{label} exit 0", failures)
        check("dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0 self_heal=disabled"
              in rnb.strict_line(r[1]), f"{label} strict counters zero", failures)
    b = rt.parse(ph[1])
    loads = [c for c in b["calls"] if c["name"] == "LoadFromSram"]
    check(len(loads) == 2 and loads[1]["r0"] == 0x0E000000 + rt.SLOT_B and loads[1]["payload"] == blk,
          "NES LoadFromSram read slot B == the saved block, from the host-flushed file", failures)
    pw = b["pw"].get(1390)
    exp = rt.decode_password(blk[0x10:0x22])
    check(pw and pw["screen"] == 1 and pw["pw"] == blk[0x10:0x22] and pw["chars"] == exp,
          "CONTINUE shows PASSWORD PLEASE with the 24 saved characters", failures)
    hh, hd = rnb.hashes(ph[1]), rnb.hashes(pd[1])
    check(hh.get(1390) and hh.get(1390) == hd.get(1390) and hh.get(1600) == hd.get(1600),
          "host-file and direct-dump P2 frames (password screen, post-accept) are identical", failures)
    if a.keep:
        print("kept", tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    print("host save persistence PASS" if not failures else f"host save persistence FAIL ({len(failures)})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
