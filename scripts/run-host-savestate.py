#!/usr/bin/env python3
"""
MZM save-state determinism through the shipped MZMRecomp host (GBARecomp --tcp server).

  A  boot, run to frame 1200, `savestate_save`, run 600 more frames ;
  B  new process, `savestate_load` of that file: the restored machine hash must equal A's
     hash at the save point, and the same 600 frames must end in A's final hash;
  C  control: a third process runs 1800 frames straight from boot with the same frames
     (no save/load): its final hash must equal A's and B's.

The compared state is the server's `state_hash` regions (IWRAM, EWRAM, VRAM, PAL, OAM), so a
lost or corrupted piece of restored memory shows up as a divergence. The guest cycle counter
is a host diagnostic that a loaded state does not restore (it restarts at 0 and its 600-frame
delta differs by a few cycles), so it is only compared between the two straight runs A and C.
Every process runs strict-static and must report zero dispatch misses at exit.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

SAVE_AT = 1200
TAIL = [(600, 0x03FF)]       # no input: the headless step path unwinds at VBlank, and a START at the
                             # title crosses a DMA that needs an interior resume (see docs/M4-RELEASE-READINESS.md)


class Host:
    def __init__(self, host, bios, rom, config, cwd, port):
        env = dict(os.environ, GBARECOMP_STRICT_STATIC="1")
        self.p = subprocess.Popen([host, "--bios", bios, "--rom", rom, "--config", config,
                                   "--tcp", str(port)], env=env, cwd=cwd,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.sock = None
        for _ in range(100):
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=120)
                break
            except OSError:
                time.sleep(0.1)
        if self.sock is None:
            self.p.kill()
            raise RuntimeError("could not connect to the TCP server")
        self.f = self.sock.makefile("rw")

    def cmd(self, **k):
        self.f.write(json.dumps(k) + "\n")
        self.f.flush()
        r = json.loads(self.f.readline())
        if not r.get("ok"):
            raise RuntimeError(f"{k}: {r}")
        return r

    def close(self):
        try:
            self.cmd(cmd="quit")
        except Exception:
            pass
        try:
            out, _ = self.p.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            self.p.kill()
            out, _ = self.p.communicate()
        return out


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def check(cond, msg, failures):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        failures.append(msg)


REGIONS = ("iwram", "ewram", "vram", "pal", "oam")


def regions(r):
    return tuple(r[k] for k in REGIONS)


def run_tail(h):
    for n, keys in TAIL:
        h.cmd(cmd="run_frames", n=n, keyinput=keys)
    return h.cmd(cmd="state_hash")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("config")
    ap.add_argument("rom")
    ap.add_argument("bios")
    a = ap.parse_args()
    host, config, rom, bios = (os.path.abspath(x) for x in (a.host, a.config, a.rom, a.bios))
    tmp = tempfile.mkdtemp(prefix="mzm-savestate-")
    failures = []

    def sandbox(name):
        d = os.path.join(tmp, name)
        os.makedirs(d)
        shutil.copyfile(rom, os.path.join(d, "game.gba"))
        return d, os.path.join(d, "game.gba")

    state = os.path.join(tmp, "mzm.state")
    print("== A: boot, save at frame", SAVE_AT)
    d, r = sandbox("a")
    h = Host(host, bios, r, config, d, free_port())
    h.cmd(cmd="run_frames", n=SAVE_AT, keyinput=0x03FF)
    at_save = h.cmd(cmd="state_hash")
    frame_save = h.cmd(cmd="frame")["frame"]
    h.cmd(cmd="savestate_save", path=state)
    after_save = h.cmd(cmd="state_hash")
    final_a = run_tail(h)
    out_a = h.close()
    check(os.path.getsize(state) > 100_000, f"state file written ({os.path.getsize(state)} bytes)", failures)
    check(regions(after_save) == regions(at_save) and after_save["cycles"] == at_save["cycles"], "saving does not perturb the machine", failures)

    print("== B: new process, load the state")
    d, r = sandbox("b")
    h = Host(host, bios, r, config, d, free_port())
    h.cmd(cmd="savestate_load", path=state)
    restored = h.cmd(cmd="state_hash")
    frame_b = h.cmd(cmd="frame")["frame"]
    final_b = run_tail(h)
    out_b = h.close()
    check(regions(restored) == regions(at_save), "restored IWRAM/EWRAM/VRAM/PAL/OAM == the machine at the save point", failures)
    check(frame_b == frame_save, f"restored frame counter == {frame_save}", failures)
    check(regions(final_b) == regions(final_a), "600 frames after load end in A's memory state", failures)

    print("== C: control, straight from boot")
    d, r = sandbox("c")
    h = Host(host, bios, r, config, d, free_port())
    h.cmd(cmd="run_frames", n=SAVE_AT, keyinput=0x03FF)
    final_c = run_tail(h)
    out_c = h.close()
    check(final_c["hash"] == final_a["hash"], "straight run C ends in A's hash, cycles included (A is deterministic)", failures)
    check(regions(final_a) != regions(at_save), "the tail changes the machine state (the comparison is not vacuous)", failures)
    for label, out in (("A", out_a), ("B", out_b), ("C", out_c)):
        check("dispatch_misses=0 interpreted_insns=0" in out, f"{label}: strict-static, zero misses at exit", failures)
    print("== D: headless `--load-state` CLI path (the harness checkpoint mechanism)")
    def headless(name, extra, png):
        d, r = sandbox(name)
        env = dict(os.environ, GBARECOMP_STRICT_STATIC="1")
        p = subprocess.run([host, "--bios", bios, "--rom", r, "--config", config, "--dump-png",
                            os.path.join(tmp, png)] + extra, cwd=d, env=env,
                           capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr
    rl, ol = headless("d1", ["--frames", "600", "--load-state", state], "loaded.png")
    rs, os_ = headless("d2", ["--frames", str(SAVE_AT + 600)], "straight.png")
    check(rl == 0 and rs == 0, "both headless runs exit 0", failures)
    check("savestate_loaded" in ol and "dispatch_misses=0 interpreted_insns=0" in ol + os_,
          "the state was loaded and both runs are strict-static clean", failures)
    pl, ps = os.path.join(tmp, "loaded.png"), os.path.join(tmp, "straight.png")
    check(os.path.exists(pl) and os.path.exists(ps) and open(pl, "rb").read() == open(ps, "rb").read(),
          "frame 1800 after `--load-state` == the straight run's frame 1800 (byte-identical PNG)", failures)
    shutil.rmtree(tmp, ignore_errors=True)
    print("host savestate PASS" if not failures else f"host savestate FAIL ({len(failures)})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
