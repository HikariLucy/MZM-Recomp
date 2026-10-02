#!/usr/bin/env python3
"""
Pause / resume INSIDE the real NES, through the shipped windowed MZMRecomp host.

  1. the qualified NES harness plays the real route (title, START, gameplay input) and, at a
     frame boundary inside NES gameplay, serialises the machine with the framework's own
     save-state container (MZM_NES_SAVE_STATE; guest memory is only read, never written);
  2. the SHIPPED host starts from that state (--load-state) in a real SDL window inside a
     private nested X server (Xephyr), strict-static, and the same ESC / pause-hotkey route a
     player uses holds and releases the guest:

       running    the guest advances (the windowed NES is below 60 Hz: compared with itself)
       held       frames frozen, the machine hash is identical on two reads, presentation goes on
       25 cycles  no catch-up burst afterwards, the host stays up
       resumed    its own pre-pause rate again, audio pushes resume, no artificial underruns reported
       exit       strict counters all zero (second session, same route)

Audio is checked with the dummy SDL driver: the probe counts what the bridge delivers, not what
a speaker plays.

usage: run-nes-pause.py <nes_behavior_test> <MZMRecomp> <config.toml> <rom> <bios> [--shots DIR]
Exit status 77 (ctest SKIP) when Xephyr, python-xlib or Pillow are unavailable.
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("hd", os.path.join(HERE, "run-host-display.py"))
hd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hd)          # exits with 77 itself when the environment cannot run it
check = hd.check

STATE_FRAME = 2000
INPUT = "700:START:8,1000:START:8,1700:RIGHT:100"


def make_state(harness, rom, bios, path):
    env = dict(os.environ, MZM_NES_FRAMES=str(STATE_FRAME + 100), MZM_NES_CHECKPOINT="1000",
               MZM_NES_HASH_FRAMES=str(STATE_FRAME), MZM_NES_INPUT=INPUT,
               MZM_NES_SAVE_STATE=f"{STATE_FRAME}:{path}")
    p = subprocess.run([harness, rom, bios], env=env, capture_output=True, text=True, timeout=600)
    out = p.stdout + p.stderr
    return p.returncode, out


def observe(game, cmd):
    for _ in range(50):
        try:
            with socket.create_connection(("127.0.0.1", game.port), timeout=5) as s:
                s.sendall((json.dumps({"cmd": cmd}) + "\n").encode())
                return json.loads(s.makefile().readline())
        except OSError:
            time.sleep(0.1)
    return None


def probe(game):
    """(pushes, bridge_underrun, net_underrun) of the latest audio probe line, or None."""
    for line in reversed(game.lines):
        m = re.search(r"audio-probe\] pushes=(\d+) .*bridge_underrun=(\d+).*net_underrun=(\d+)", line)
        if m:
            return tuple(int(x) for x in m.groups())
    return None


def main():
    ap = argparse.ArgumentParser()
    for a in ("harness", "host", "config", "rom", "bios"):
        ap.add_argument(a)
    ap.add_argument("--shots")
    args = ap.parse_args()
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)

    work = tempfile.mkdtemp(prefix="mzm-nespause-")
    state = os.path.join(work, "nes-gameplay.state")
    exe = os.path.join(work, "MZMRecomp")
    shutil.copy2(args.host, exe)

    print("== 1: harness reaches NES gameplay and serialises the machine")
    rc, out = make_state(args.harness, args.rom, args.bios, state)
    check(rc == 0 and "NES3 DONE" in out and "strict=clean" in out, "harness run is strict-clean")
    check(f"NES3 save_state frame={STATE_FRAME}" in out and " ok=1" in out, "state saved inside the NES")
    if not os.path.exists(state):
        print(out[-2000:])
        return 1

    xs = hd.Xserver()
    game = None
    try:
        print("== 2: shipped host resumes inside the NES")
        game = hd.Game(exe, args.config, args.rom, args.bios, xs.name, work,
                       extra_args=["--load-state", state],
                       extra_env={"GBARECOMP_FORCE_REFRESH_HZ": "144"})
        win = xs.game_window()
        xs.park()                      # the host's real pointer must not hover the menu (see run-host-display.py)
        game.wait_ready()
        hd.check_wait(lambda: ("savestate_loaded" in game.log() and f"frame={STATE_FRAME}" in game.log(),
                               game.log()[-300:]),
                      f"host loaded the NES state at guest frame {STATE_FRAME}", timeout=15.0)
        menu = hd.Menu(xs, game, win)
        r0 = game.frame()
        time.sleep(1.5)
        r1 = game.frame()
        pre = r1 - r0                  # frames per 1.5 s; the NES is below real time in this host
        check(pre >= 20, f"NES advances before the pause ({pre} frames/1.5 s; the NES' throughput "
                         "below 60 Hz is a known, separate limitation)")
        check(r0 > STATE_FRAME, f"guest frame counter continues from the state ({r0})")
        base = xs.grab(win)
        check(hd.nonblack(base) > 2000, "the NES picture is on screen")
        pb0 = probe(game)
        check(pb0 is not None, f"audio probe is reporting ({pb0})")

        print("== 3: hold through the ESC menu")
        menu.open()
        time.sleep(0.5)
        h0 = game.frame()
        hash0 = observe(game, "state_hash")
        time.sleep(1.5)
        h1 = game.frame()
        hash1 = observe(game, "state_hash")
        shown = xs.grab(win)
        check(h1 - h0 <= 1, f"frames frozen while the menu is open ({h1 - h0} frames/1.5 s)")
        if hash0 and hash0.get("ok") and hash1 and hash1.get("ok"):
            same = all(hash0.get(k) == hash1.get(k) for k in ("iwram", "ewram", "vram", "pal", "oam"))
            check(same, "machine memory hash identical across the hold (IWRAM/EWRAM/VRAM/PAL/OAM)")
        else:
            check(False, f"state_hash unavailable on the observe port ({hash0})")
        check(hd.nonblack(shown) > 100000, "presentation continues: the menu is drawn over the NES")
        if args.shots:
            shown.save(os.path.join(args.shots, "nes-menu-open.png"))
        menu.close()
        time.sleep(0.3)
        c0 = game.frame()
        time.sleep(1.5)
        rr = game.frame() - c0
        check(rr >= 0.7 * pre, f"NES resumes at its pre-pause rate ({rr} vs {pre} frames/1.5 s)")

        print("== 4: 25 open/close cycles")
        t0, s0 = time.time(), game.frame()
        held_total = 0.0
        moved = 0
        for _ in range(25):
            menu.open()
            a, th = game.frame(), time.time()
            time.sleep(0.15)
            moved += game.frame() - a
            held_total += time.time() - th
            menu.close()
        s1, t1 = game.frame(), time.time()
        check(game.alive(), "host alive after 25 cycles")
        check(moved <= 2, f"guest never advanced while held ({moved} frames over 25 holds)")
        ceiling = (t1 - t0) * (pre / 1.5) * 1.2
        check(s1 - s0 <= ceiling, f"no catch-up burst ({s1 - s0} frames, ceiling {ceiling:.0f})")
        time.sleep(1.5)
        z0 = game.frame()
        time.sleep(1.5)
        zr = game.frame() - z0
        check(0.7 * pre <= zr <= 1.3 * pre, f"rate back to the pre-pause rate ({zr} vs {pre} frames/1.5 s)")

        print("== 5: pause hotkey (Shift+P) hold")
        xs.key("p", shift=True)
        time.sleep(0.6)
        p0 = game.frame()
        time.sleep(1.5)
        check(game.frame() - p0 <= 1, "frames frozen with the pause hotkey")
        xs.key("p", shift=True)
        time.sleep(0.4)
        q0 = game.frame()
        time.sleep(1.5)
        check(game.frame() - q0 >= 0.7 * pre, "NES resumes after the hotkey pause")

        print("== 5b: display options changed from the ESC menu inside the NES")
        base_img = xs.grab(win)
        for label, section, row in (("Linear filter", "Graphics", hd.GRAPHICS["Linear filter"]),
                                    ("CRT Lite", "Graphics", hd.GRAPHICS["CRT"]),
                                    ("CRT Soft", "Graphics", hd.GRAPHICS["CRT"]),
                                    ("Presentation Refresh = Monitor (forced 144 Hz, synthetic)", "Performance",
                                     hd.PERFORMANCE["Presentation Refresh"]),
                                    ("Integer scaling", "Graphics", hd.GRAPHICS["Integer scaling"])):
            a, b = menu.activate(section, row, around=lambda: observe(game, "state_hash"))
            check(a is not None and a == b, f"{label}: guest machine untouched while paused in the menu")
            time.sleep(1.0)
            f0 = game.frame()
            time.sleep(1.5)
            rate = game.frame() - f0
            check(rate >= 0.8 * pre, f"{label}: NES resumes cleanly ({rate} vs {pre} frames/1.5 s)")
            check(game.alive(), f"{label}: host alive")
        styled = xs.grab(win)
        check(hd.ImageChops.difference(base_img, styled).getbbox() is not None,
              "filter + CRT presets + presentation + integer scaling change the presented picture")
        menu.activate("Display", hd.DISPLAY["Restore display defaults"])
        time.sleep(1.0)
        check(hd.nonblack(xs.grab(win)) > 2000, "NES picture still on screen after restoring defaults")

        print("== 6: audio")
        time.sleep(3.0)                      # let any refill grace (400 ms) expire
        pb1 = probe(game)
        time.sleep(3.0)
        pb2 = probe(game)
        check(pb1 and pb2 and pb2[0] > pb1[0], f"audio pushes keep flowing after the cycles ({pb1} -> {pb2})")
        check(pb2 and pb2[2] == 0, f"no artificial underruns counted (net_underrun={pb2 and pb2[2]}; "
                                    f"raw bridge count {pb2 and pb2[1]} includes the intentional holds)")
        check(pb1 and pb2 and pb2[2] == pb1[2], "the corrected counter is flat once playing again")

        game.stop()                  # the observe port blocks a graceful exit

        print("== 7: strict-static at exit (second session, same route, no observe port)")
        game = hd.Game(exe, args.config, args.rom, args.bios, xs.name, work, observe=False,
                       extra_args=["--load-state", state])
        win = xs.game_window()
        hd.check(game.wait_log("host_window: presentation", timeout=60.0), "windowed session presented its first frame")
        menu = hd.Menu(xs, None, win)  # no observe port in this session: the picture is the menu evidence
        for _ in range(10):
            menu.open()
            time.sleep(0.3)
            menu.close()
            time.sleep(0.3)
        xs.key("p", shift=True)
        time.sleep(0.8)
        xs.key("p", shift=True)
        time.sleep(3)
        check(game.alive(), "session with 10 menu cycles and a hotkey pause stays up")
        game.proc.terminate()        # SIGTERM: the runtime shuts down and prints its strict summary
        log = game.finish(60)
        check(game.proc.returncode == 0, f"clean exit status ({game.proc.returncode})")
        check("self_heal_coverage=FULLY_STATIC" in log, "strict-static: FULLY_STATIC")
        for key in ("dispatch_misses=0", "interpreted_insns=0", "unmapped=0", "io_unhandled=0"):
            check(key in log, f"strict-static: {key}")
    except Exception:
        if game:
            print("---- game log ----\n" + game.log()[-3000:])
        raise
    finally:
        if game and game.alive():
            game.proc.kill()
        xs.stop()
        shutil.rmtree(work, ignore_errors=True)

    if hd.FAILURES:
        print(f"\n{len(hd.FAILURES)} check(s) failed")
        return 1
    print("\nNES pause/resume qualification: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
