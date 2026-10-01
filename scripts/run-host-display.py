#!/usr/bin/env python3
"""
Qualify the in-game Enhancements menu and the host display settings through the shipped
MZMRecomp binary, in a real SDL window.

The window lives in a private nested X server (Xephyr), so nothing is typed into or captured
from the desktop. Input is injected with XTest and pixels are read back from that server.

  A  the game runs; ESC opens the menu and does NOT quit
  B  the guest is paused while the menu is open (frame counter frozen) and resumes on close;
     ESC closes it even with the pointer resting over a menu row
  C  Aspect / Integer scaling / Stretch change the real destination rectangle
  D  Linear filtering and CRT Lite change the picture; turning them off restores the exact
     default pixels (the nearest/CRT-off path is not degraded)
  E  choices persist in config.ini [Display] and are re-applied after a restart; invalid
     values fall back safely; "Restore display defaults" resets everything
  F  strict-static still reports 0 dispatch misses / 0 interpreted instructions / 0 unmapped /
     0 unhandled I/O after all of the above

Presentation only: nothing here changes guest state, and the only guest-facing observation is
the frame counter (read over the observe TCP port) used to prove the pause.

usage: run-host-display.py <MZMRecomp> <config.toml> <rom> <bios> [--shots DIR]
Exit status 77 (ctest SKIP) when Xephyr, python-xlib or Pillow are unavailable.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

SKIP = 77


def skip(why):
    print(f"SKIP: {why}")
    sys.exit(SKIP)


try:
    from PIL import Image, ImageChops
    from Xlib import X, XK, display, Xatom
    from Xlib.ext import xtest
    from Xlib.protocol import event as xevent
except Exception as e:  # pragma: no cover - environment dependent
    skip(f"python-xlib / Pillow not available ({e})")
if not shutil.which("Xephyr"):
    skip("Xephyr not installed")
if not os.environ.get("DISPLAY"):
    skip("no host DISPLAY for the nested X server")

FAILURES = []


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        FAILURES.append(msg)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def free_display():
    for n in range(90, 140):
        if not os.path.exists(f"/tmp/.X11-unix/X{n}") and not os.path.exists(f"/tmp/.X{n}-lock"):
            return f":{n}"
    skip("no free X display number")


class Xserver:
    def __init__(self, w=1280, h=800):
        self.name = free_display()
        self.proc = subprocess.Popen(
            ["Xephyr", self.name, "-screen", f"{w}x{h}", "-ac", "-no-host-grab", "-nolisten", "tcp"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.d = None
        for _ in range(100):
            try:
                self.d = display.Display(self.name)
                break
            except Exception:
                time.sleep(0.1)
        if self.d is None:
            self.proc.kill()
            skip("Xephyr did not start")

    def _fake(self, kind, code):
        xtest.fake_input(self.d, kind, code)
        self.d.sync()

    def key(self, name, shift=False, hold=0.06):
        kc = self.d.keysym_to_keycode(XK.string_to_keysym(name))
        sh = self.d.keysym_to_keycode(XK.string_to_keysym("Shift_L"))
        if shift:
            self._fake(X.KeyPress, sh)
        self._fake(X.KeyPress, kc)
        time.sleep(hold)
        self._fake(X.KeyRelease, kc)
        if shift:
            self._fake(X.KeyRelease, sh)
        time.sleep(0.12)

    def move(self, x, y):
        xtest.fake_input(self.d, X.MotionNotify, False, X.CurrentTime, self.d.screen().root, x, y)
        self.d.sync()
        time.sleep(0.15)

    def game_window(self, timeout=30):
        end = time.time() + timeout
        while time.time() < end:
            for w in self.d.screen().root.query_tree().children:
                try:
                    name = w.get_wm_name()
                    g = w.get_geometry()
                except Exception:
                    continue
                if name and "Metroid" in str(name) and g.width > 50:
                    return w
            time.sleep(0.2)
        raise RuntimeError("game window did not appear")

    def resize(self, win, w, h):
        win.configure(width=w, height=h)
        self.d.sync()
        time.sleep(0.6)

    def grab(self, win):
        g = win.get_geometry()
        raw = win.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff)
        return Image.frombytes("RGB", (g.width, g.height), raw.data, "raw", "BGRX")

    def close_window(self, win):
        wm_protocols = self.d.intern_atom("WM_PROTOCOLS")
        delete = self.d.intern_atom("WM_DELETE_WINDOW")
        ev = xevent.ClientMessage(window=win, client_type=wm_protocols,
                                  data=(32, [delete, X.CurrentTime, 0, 0, 0]))
        win.send_event(ev, event_mask=0)
        self.d.sync()

    def stop(self):
        try:
            self.d.close()
        except Exception:
            pass
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except Exception:
            self.proc.kill()


class Game:
    """MZMRecomp in a window, with stdout/stderr captured and the observe port open."""

    def __init__(self, binary, config, rom, bios, display_name, workdir, observe=True):
        self.port = free_port()
        env = dict(os.environ, DISPLAY=display_name, GBARECOMP_STRICT_STATIC="1",
                   SDL_AUDIODRIVER="dummy")
        self.lines = []
        self.proc = subprocess.Popen(
            [binary, "--no-launcher", "--bios", bios, "--rom", rom, "--config", config,
             "--window"] + (["--tcp-observe", str(self.port)] if observe else []),
            env=env, cwd=workdir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.proc.stdout:
            self.lines.append(line.rstrip("\n"))

    def log(self):
        return "\n".join(self.lines)

    def frame(self):
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=5) as s:
                    s.sendall(b'{"cmd":"frame"}\n')
                    return json.loads(s.makefile().readline())["frame"]
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("observe port unavailable")

    def last_layout(self):
        for line in reversed(self.lines):
            if "host_window: presentation" in line and " layout=" in line:
                tail = line.split(" layout=")[1].split()
                x, y = tail[0].split(",")
                w, h = tail[1].split("x")
                return int(x), int(y), int(w), int(h)
        return None

    def wait_layout(self, want, timeout=6):
        end = time.time() + timeout
        while time.time() < end:
            if self.last_layout() == want:
                return True
            time.sleep(0.1)
        return False

    def stop(self):
        """Hard stop for observe-mode sessions (the observer thread blocks a graceful exit)."""
        self.proc.kill()
        self.finish(10)

    def alive(self):
        return self.proc.poll() is None

    def finish(self, timeout=30):
        try:
            self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.reader.join(5)
        return self.log()


# --- pixel helpers ---------------------------------------------------------------------

def luma(im):
    return im.convert("L")


def nonblack(im, thresh=10):
    return sum(1 for v in luma(im).getdata() if v > thresh)


def block_uniform_fraction(im, scale):
    """Fraction of pixels that equal their scale x scale block's representative pixel."""
    w, h = im.size
    small = im.resize((w // scale, h // scale), Image.NEAREST)
    up = small.resize((w, h), Image.NEAREST)
    diff = ImageChops.difference(im, up).convert("L")
    bad = sum(1 for v in diff.getdata() if v > 0)
    return 1.0 - bad / float(w * h)


def row_ratio_profile(off, on, period, min_luma=90):
    """Mean on/off brightness ratio per (row % period), over pixels that are bright when off."""
    lo, lc = luma(off).load(), luma(on).load()
    w, h = off.size
    sums, counts = [0.0] * period, [0] * period
    brighter = 0
    for y in range(h):
        for x in range(0, w, 3):
            a = lo[x, y]
            if a < min_luma:
                continue
            b = lc[x, y]
            if b > a + 1:
                brighter += 1
            sums[y % period] += b / a
            counts[y % period] += 1
    prof = [sums[i] / counts[i] if counts[i] else 1.0 for i in range(period)]
    return prof, brighter


# --- menu driver -----------------------------------------------------------------------
# Section / row order of the MZM Enhancements menu (items are listed in the order the
# runtime builds them; sections appear in order of first use).
SECTIONS = ["Display", "Graphics", "Audio", "System", "Assist Tools", "Performance"]
DISPLAY = {"Fullscreen": 0, "Window scale": 1, "Aspect ratio": 2, "Restore display defaults": 3}
GRAPHICS = {"Integer scaling": 0, "Linear filter": 1, "CRT Lite": 2, "Scanline strength": 3,
            "Color correction": 4}
PERFORMANCE = {"Show FPS": 0, "VSync": 1, "Performance overlay": 2}


class Menu:
    def __init__(self, xs):
        self.xs = xs
        self.section = 0   # the shared menu remembers the last section across open/close

    def open(self):
        self.xs.key("Escape")
        time.sleep(0.4)

    def close(self):
        self.xs.key("Escape")
        time.sleep(0.4)

    def activate(self, section, row, presses=1, key="Return"):
        """Open the menu, enter `section`, move to `row`, press `key` `presses` times, close."""
        self.open()
        idx = SECTIONS.index(section)
        for _ in range((idx - self.section) % len(SECTIONS)):
            self.xs.key("Down")
        self.section = idx
        self.xs.key("Return")
        for _ in range(row):
            self.xs.key("Down")
        for _ in range(presses):
            self.xs.key(key)
        self.close()


def display_ini_text():
    return ("[Display]\nwindow_mode = windowed\nwindow_scale = 3\nscale_mode = integer\naspect = native\n"
            "filtering = nearest\nvsync = 0\nshow_fps = 1\ncrt_enabled = 1\ncrt_preset = lite\n"
            "scanline_strength = 60\ncolor_profile = gba-like\n")


def read_ini(path):
    try:
        return open(path).read()
    except OSError:
        return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("config")
    ap.add_argument("rom")
    ap.add_argument("bios")
    ap.add_argument("--shots", help="directory for local inspection screenshots (not committed)")
    args = ap.parse_args()
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)

    work = tempfile.mkdtemp(prefix="mzm-display-")
    exe = os.path.join(work, "MZMRecomp")
    shutil.copy2(args.binary, exe)           # config.ini lives next to the executable
    ini = os.path.join(work, "config.ini")
    xs = Xserver()
    game = None
    try:
        print("== A/B: boot, ESC menu, pause policy")
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work)
        win = xs.game_window()
        xs.move(2, 2)            # pointer off the window first
        time.sleep(8)
        f0 = game.frame()
        time.sleep(1.5)
        f1 = game.frame()
        check(f1 - f0 >= 60, f"guest advances at ~60 Hz with the menu closed ({f1 - f0} frames/1.5s)")
        base = xs.grab(win)

        menu = Menu(xs)
        menu.open()
        check(game.alive(), "ESC did not quit the application")
        time.sleep(0.5)
        m0 = game.frame()
        time.sleep(1.5)
        m1 = game.frame()
        shown = xs.grab(win)
        check(nonblack(shown) > 100000, "menu is drawn over the window")
        if args.shots:
            shown.save(os.path.join(args.shots, "menu-open.png"))
        check(m1 - m0 <= 1, f"guest is paused while the menu is open ({m1 - m0} frames/1.5s)")
        menu.close()
        r0 = game.frame()
        time.sleep(1.5)
        r1 = game.frame()
        check(r1 - r0 >= 60, f"guest resumes when the menu closes ({r1 - r0} frames/1.5s)")

        # ESC must also close the menu with the pointer resting on a menu row.
        xs.move(360, 240)
        menu.open()
        p0 = game.frame()
        menu.close()
        time.sleep(1.0)
        check(game.frame() - p0 >= 30, "ESC closes the menu with the mouse over a row")
        xs.move(2, 2)

        # Open/close many times: no quit, no hang, and no catch-up burst on resume (the pacer
        # is realigned, so the guest never fast-forwards to repay the time spent in the menu).
        s0, t0 = game.frame(), time.time()
        held_total = 0.0
        for _ in range(25):
            menu.open()
            h0, th = game.frame(), time.time()
            time.sleep(0.15)
            check_h = game.frame() - h0
            held_total += time.time() - th
            if check_h != 0:
                check(False, f"guest advanced {check_h} frames while the menu was open")
                break
            menu.close()
        s1, t1 = game.frame(), time.time()
        check(game.alive(), "25 rapid ESC open/close cycles did not quit or hang")
        open_time = held_total + 25 * 0.4
        ceiling = (t1 - t0 - open_time * 0.5) * 59.73 * 1.2
        check(s1 - s0 <= ceiling,
              f"no catch-up burst after rapid open/close ({s1 - s0} frames, ceiling {ceiling:.0f})")
        time.sleep(1.5)
        z0 = game.frame()
        time.sleep(1.5)
        check(55 <= game.frame() - z0 <= 100, "guest rate is back to ~60 Hz after the stress")

        print("== D: filtering and CRT Lite (guest frozen with the pause hotkey for stable frames)")
        xs.key("p", shift=True)                 # host pause: identical frames
        time.sleep(0.6)
        scale = win.get_geometry().width // 240
        off = xs.grab(win)
        check(block_uniform_fraction(off, scale) > 0.995, "default filtering is nearest (blocky 3x3 pixels)")
        menu.activate("Graphics", GRAPHICS["Linear filter"])
        time.sleep(0.4)
        lin = xs.grab(win)
        check(block_uniform_fraction(lin, scale) < 0.97, "Linear filtering smooths the image at runtime")
        menu.activate("Graphics", GRAPHICS["Linear filter"])
        time.sleep(0.4)
        again = xs.grab(win)
        check(ImageChops.difference(off, again).getbbox() is None,
              "turning Linear off restores the exact default pixels")

        menu.activate("Graphics", GRAPHICS["CRT Lite"])
        time.sleep(0.4)
        crt = xs.grab(win)
        prof, brighter = row_ratio_profile(off, crt, scale)
        print(f"        per-row brightness ratio (row % {scale}): " + ", ".join(f"{v:.3f}" for v in prof))
        check(brighter == 0, "CRT Lite never brightens a pixel")
        check(min(prof) < 0.93 and max(prof) > 0.97, "CRT Lite darkens alternating rows (visible scanlines)")
        check(min(prof) > 0.6, "CRT Lite stays conservative (no row darker than 60%)")
        if args.shots:
            off.save(os.path.join(args.shots, "nearest.png"))
            lin.save(os.path.join(args.shots, "linear.png"))
            crt.save(os.path.join(args.shots, "crt-lite.png"))
        menu.activate("Graphics", GRAPHICS["CRT Lite"])
        time.sleep(0.4)
        check(ImageChops.difference(off, xs.grab(win)).getbbox() is None,
              "CRT Off restores the exact default pixels")
        menu.activate("Graphics", GRAPHICS["Color correction"])
        time.sleep(0.4)
        colour = xs.grab(win)
        check(ImageChops.difference(off, colour).getbbox() is not None,
              "Color correction (GBA-like) changes the presented colours")
        if args.shots:
            colour.save(os.path.join(args.shots, "color-gba-like.png"))
        menu.activate("Graphics", GRAPHICS["Color correction"])
        time.sleep(0.4)
        check(ImageChops.difference(off, xs.grab(win)).getbbox() is None,
              "Color correction Off restores the exact default pixels")
        menu.activate("Performance", PERFORMANCE["Performance overlay"])
        time.sleep(0.5)
        overlay = xs.grab(win)
        top_left = (0, 0, 330, 120)
        check(ImageChops.difference(off.crop(top_left), overlay.crop(top_left)).getbbox() is not None,
              "F10 / menu performance overlay draws in the corner")
        check(ImageChops.difference(off.crop((400, 200, 720, 480)), overlay.crop((400, 200, 720, 480))).getbbox() is None,
              "the overlay leaves the rest of the picture untouched")
        if args.shots:
            overlay.save(os.path.join(args.shots, "perf-overlay.png"))
        xs.key("F10")
        time.sleep(0.4)
        check(ImageChops.difference(off, xs.grab(win)).getbbox() is None, "F10 hides the overlay again")
        # persisted value (CRT toggled on then off): file reflects the final state
        check("crt_enabled = 0" in read_ini(ini), "config.ini [Display] records crt_enabled = 0")
        xs.key("p", shift=True)                 # resume
        time.sleep(0.3)

        print("== FPS readout: EMU (guest) and PRESENT (host) are reported separately")
        menu.activate("Performance", PERFORMANCE["Show FPS"])
        time.sleep(2.0)
        title = str(win.get_wm_name())
        print("        title:", title)
        import re
        m = re.search(r"EMU ([0-9.]+) \| PRESENT ([0-9.]+) fps", title)
        check(bool(m), "title bar shows EMU and PRESENT rates")
        if m:
            emu, pres = float(m.group(1)), float(m.group(2))
            check(55.0 <= emu <= 64.0, f"EMU rate ~59.7 ({emu})")
            check(55.0 <= pres <= 64.0, f"PRESENT rate equals the guest rate when 1:1 paced ({pres})")
        menu.open()
        time.sleep(2.0)
        m = re.search(r"EMU ([0-9.]+) \| PRESENT ([0-9.]+) fps", str(win.get_wm_name()))
        if m:
            emu, pres = float(m.group(1)), float(m.group(2))
            check(emu < 2.0 and pres > 30.0, f"while the menu holds the guest EMU={emu} but PRESENT={pres}")
        menu.close()
        menu.activate("Performance", PERFORMANCE["Show FPS"])

        print("== C: aspect, integer scaling, stretch (real destination rectangle)")
        xs.resize(win, 1000, 700)
        check(game.wait_layout((0, 17, 1000, 666)), "Native 3:2 letterboxes a 1000x700 window")
        menu.activate("Graphics", GRAPHICS["Integer scaling"])
        check(game.wait_layout((20, 30, 960, 640)), "Integer scaling snaps 1000x700 to 4x (960x640) centred")
        xs.resize(win, 1100, 800)
        check(game.wait_layout((70, 80, 960, 640)), "Integer scaling follows the window (4x: 1100/240, 800/160)")
        xs.resize(win, 1300, 1000)
        check(game.wait_layout((50, 100, 1200, 800)), "Integer scaling picks the largest whole multiple (5x)")
        menu.activate("Graphics", GRAPHICS["Integer scaling"])      # back to Fit
        xs.resize(win, 1000, 700)
        check(game.wait_layout((0, 17, 1000, 666)), "Fit is restored when Integer scaling is switched off")
        menu.activate("Display", DISPLAY["Aspect ratio"])
        check(game.wait_layout((0, 0, 1000, 700)), "Stretch fills the whole window")
        menu.activate("Display", DISPLAY["Aspect ratio"])
        check(game.wait_layout((0, 17, 1000, 666)), "Native aspect is restored")

        print("== E: persistence, invalid values, restore defaults")
        menu.activate("Graphics", GRAPHICS["Integer scaling"])
        menu.activate("Graphics", GRAPHICS["CRT Lite"])
        menu.activate("Graphics", GRAPHICS["Scanline strength"], presses=2, key="Right")
        menu.activate("Performance", PERFORMANCE["Show FPS"])
        text = read_ini(ini)
        check("scale_mode = integer" in text and "crt_enabled = 1" in text and "show_fps = 1" in text,
              "choices are written to config.ini [Display]")
        check("scanline_strength = 50" in text, "scanline strength steps by 5% and persists (40 -> 50)")
        game.stop()

        game = Game(exe, args.config, args.rom, args.bios, xs.name, work)
        win = xs.game_window()
        time.sleep(6)
        log = game.log()
        check("display settings loaded from config.ini" in log, "settings are re-applied after a restart")
        xs.resize(win, 1000, 700)
        check(game.wait_layout((20, 30, 960, 640)), "Integer scaling survives the restart")
        game.stop()

        with open(ini, "w") as f:
            f.write("[KeyMap]\nPause = Shift+P\n\n[Display]\nwindow_mode = sideways\nwindow_scale = 99\n"
                    "filtering = linear\ncrt_enabled = banana\nscanline_strength = 70\n")
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work)
        win = xs.game_window()
        time.sleep(6)
        check(game.alive(), "invalid [Display] values do not prevent startup")
        check("ignored 3 invalid [Display] line(s)" in game.log(), "invalid values are reported and ignored")
        check(win.get_geometry().width == 720, "invalid window values fall back to the default size")
        menu = Menu(xs)
        menu.activate("Display", DISPLAY["Restore display defaults"])
        text = read_ini(ini)
        check("filtering = nearest" in text and "scanline_strength = 40" in text
              and "scale_mode = fit" in text and "crt_enabled = 0" in text,
              "Restore display defaults resets and persists every option")
        check("[KeyMap]" in text and "Pause = Shift+P" in text,
              "saving [Display] preserves the other config.ini sections")

        game.stop()

        print("== F: strict-static through a windowed session that uses the menu and the options")
        with open(ini, "w") as f:
            f.write(display_ini_text())
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work, observe=False)
        win = xs.game_window()
        xs.move(2, 2)
        time.sleep(8)
        menu = Menu(xs)
        menu.activate("Graphics", GRAPHICS["Linear filter"])
        menu.activate("Performance", PERFORMANCE["Performance overlay"])
        xs.key("F10")                      # developer overlay hotkey toggles it back off
        time.sleep(3)
        check(game.alive(), "session with CRT/integer/overlay options stays up")
        game.proc.terminate()              # SIGTERM: runtime shuts down and prints its strict summary
        log = game.finish(60)
        check(game.proc.returncode == 0, f"clean exit status ({game.proc.returncode})")
        check("self_heal_coverage=FULLY_STATIC" in log, "strict-static: FULLY_STATIC")
        for key in ("dispatch_misses=0", "interpreted_insns=0", "unmapped=0", "io_unhandled=0"):
            check(key in log, f"strict-static: {key}")
    except Exception:
        if game:
            print("---- game log ----\n" + game.log())
        raise
    finally:
        if game and game.alive():
            game.proc.kill()
        xs.stop()
        shutil.rmtree(work, ignore_errors=True)

    if FAILURES:
        print(f"\n{len(FAILURES)} check(s) failed")
        return 1
    print("\nhost display qualification: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
