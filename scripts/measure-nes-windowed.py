#!/usr/bin/env python3
"""
Measure the windowed NES rates (EMU = guest frames/s, PRESENT = presented frames/s).

The NES is entered through the qualified harness state (as run-nes-pause.py), then the
shipped MZMRecomp runs it in a window inside a private nested X server (Xephyr, software
GL). It never touches the user's desktop session.

  EMU      guest frame counter delta over a fixed interval (observe port)
  PRESENT  slope of wall time vs --frames N over two sessions (startup cancels)

usage: measure-nes-windowed.py <nes_behavior_test> <MZMRecomp> <config.toml> <rom> <bios>
Informational (prints numbers; not a pass/fail gate). Exit 77 when the environment is missing.
"""
import argparse, importlib.util, os, shutil, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("np", os.path.join(HERE, "run-nes-pause.py"))
np = importlib.util.module_from_spec(spec)
spec.loader.exec_module(np)
hd = np.hd

VARIANTS = {
    "default (nearest, CRT off)": "[Display]\nwindow_scale = 3\nfiltering = nearest\ncrt_enabled = 0\nshow_fps = 0\n",
    "CRT Lite + bilinear": "[Display]\nwindow_scale = 3\nfiltering = linear\ncrt_enabled = 1\ncrt_preset = lite\n"
                           "scanline_strength = 60\nshow_fps = 0\n",
}


def session(args, exe, work, state, ini_text, frames=None, observe=True, secs=10.0):
    with open(os.path.join(work, "config.ini"), "w") as f:
        f.write(ini_text)
    xs = hd.Xserver()
    game = None
    try:
        extra = ["--load-state", state] + (["--frames", str(frames)] if frames else [])
        t0 = time.time()
        game = hd.Game(exe, args.config, args.rom, args.bios, xs.name, work, observe=observe, extra_args=extra)
        win = xs.game_window()
        if frames:
            game.finish(300)
            return time.time() - t0
        time.sleep(6)
        a = game.frame(); ta = time.time()
        time.sleep(secs)
        b = game.frame(); tb = time.time()
        return (b - a) / (tb - ta)
    finally:
        if game and game.alive():
            game.stop()
        xs.stop()


def main():
    ap = argparse.ArgumentParser()
    for a in ("harness", "host", "config", "rom", "bios"):
        ap.add_argument(a)
    args = ap.parse_args()
    work = tempfile.mkdtemp(prefix="mzm-nesrate-")
    state = os.path.join(work, "nes.state")
    exe = os.path.join(work, "MZMRecomp")
    shutil.copy2(args.host, exe)
    rc, out = np.make_state(args.harness, args.rom, args.bios, state)
    if rc != 0 or not os.path.exists(state):
        print(out[-1500:]); return 1
    print("environment: private Xephyr (software GL / llvmpipe), SDL dummy audio, vsync off")
    for name, ini in VARIANTS.items():
        emu = [session(args, exe, work, state, ini) for _ in range(2)]
        t_a = session(args, exe, work, state, ini, frames=600, observe=False)
        t_b = session(args, exe, work, state, ini, frames=1800, observe=False)
        present = 1200.0 / (t_b - t_a)
        print(f"{name}: EMU {emu[0]:.1f} / {emu[1]:.1f} fps  PRESENT {present:.1f} fps "
              f"(600f {t_a:.1f}s, 1800f {t_b:.1f}s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
