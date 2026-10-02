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

Synchronisation (HOST-DISPLAY-STABILITY-1). Nothing here relies on a blind sleep to "let the
host catch up": every action is followed by a wait on an observable effect (frame counter, the
renderer's own layout log line, the persisted config.ini, window geometry, a stable screenshot)
with an explicit timeout whose failure message carries the last observed state. The nested X
server has no window manager and, with -no-host-grab, passes the *real* pointer of the desktop
session through; the shared menu selects whatever row the pointer hovers (recomp-ui
draw_sections/draw_items), so a stray pointer silently re-targets the keyboard navigation. The
pointer is therefore parked and grabbed inside the nested server (confined to a 1x1 sink window)
except in the two steps that need it, and keyboard focus is set explicitly on the game window.

  G  (ENHANCEMENTS-2) Alt+Enter toggles Windowed <-> Borderless, persists it, ignores key
     auto-repeat and works with the menu open; the menu is operable from a (virtual) game
     controller; the guest never receives controller input the menu owns, nor a button that
     was still held when the menu closed; CRT Soft/Lite/Strong/Custom and the Presentation
     Refresh option behave and persist; with Monitor Refresh the guest stays at ~59.7 Hz while
     PRESENT rises and the machine hash does not change across repeated presents.
     The monitor refresh in G is FORCED (GBARECOMP_FORCE_REFRESH_HZ=144) because a nested X
     server reports no real rate: that part is synthetic / nested-display evidence, not a
     144 Hz hardware pass. The controller is an SDL virtual joystick driven through
     GBARECOMP_TEST_VIRTUAL_PAD (no system-wide input device is created).

Presentation only: nothing here changes guest state, and the only guest-facing observation is
the frame counter (read over the observe TCP port) used to prove the pause.

usage: run-host-display.py <MZMRecomp> <config.toml> <rom> <bios> [--shots DIR]
Exit status 77 (ctest SKIP) when Xephyr, python-xlib or Pillow are unavailable.
"""
import argparse
import json
import os
import re
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


def wait_until(probe, what, timeout=10.0, interval=0.1):
    """Poll `probe() -> (ok, state)` until ok; (ok, last_state). Never raises on probe errors."""
    end = time.time() + timeout
    state = None
    while True:
        try:
            ok, state = probe()
        except Exception as e:  # a transient X error is a state, not a crash
            ok, state = False, f"probe error: {e}"
        if ok:
            return True, state
        if time.time() >= end:
            return False, state
        time.sleep(interval)


def check_wait(probe, msg, timeout=10.0, interval=0.1):
    """check() on a condition that settles asynchronously; failure reports the last state seen."""
    ok, state = wait_until(probe, msg, timeout, interval)
    check(ok, msg if ok else f"{msg} [timeout {timeout:g}s; last state: {state}]")
    return ok


def check_layout(game, want, msg, timeout=6.0):
    return check_wait(lambda: (game.last_layout() == want, f"layout {game.last_layout()} want {want}"),
                      msg, timeout)


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
        self.size = (w, h)
        self.focus_win = None
        self.focus_repairs = 0
        self.parked = False
        root = self.d.screen().root
        # 1x1 override-redirect sink in the bottom-right corner: where the pointer is parked.
        self.sink = root.create_window(w - 2, h - 2, 1, 1, 0, self.d.screen().root_depth,
                                       X.InputOutput, X.CopyFromParent, override_redirect=1)
        self.sink.map()
        self.d.sync()

    def park(self):
        """Grab the pointer inside the nested server and confine it to the sink window, so neither
        the host's real pointer nor an XTest motion can hover the game's menu."""
        if self.parked:
            return
        status = self.sink.grab_pointer(False, 0, X.GrabModeAsync, X.GrabModeAsync,
                                        self.sink, X.NONE, X.CurrentTime)
        self.d.sync()
        if status != X.GrabSuccess:
            raise RuntimeError(f"could not park the pointer (grab status {status})")
        self.parked = True

    def unpark(self):
        if self.parked:
            self.d.ungrab_pointer(X.CurrentTime)
            self.d.sync()
            self.parked = False

    def pointer(self):
        q = self.d.screen().root.query_pointer()
        return q.root_x, q.root_y

    def focus_ok(self):
        if self.focus_win is None:
            return True
        fo = self.d.get_input_focus().focus
        return getattr(fo, "id", fo) == self.focus_win.id

    def ensure_focus(self):
        """Keyboard focus belongs to the game window (there is no window manager to give it).
        Counted when it had to be repaired: that is a finding, not something to hide."""
        if self.focus_win is None or self.focus_ok():
            return
        self.focus_repairs += 1
        print(f"        note: keyboard focus was not on the game window; restored ({self.focus_repairs})")
        self.focus_win.set_input_focus(X.RevertToParent, X.CurrentTime)
        self.d.sync()

    def _fake(self, kind, code):
        xtest.fake_input(self.d, kind, code)
        self.d.sync()

    def key(self, name, shift=False, hold=0.06):
        self.ensure_focus()
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

    def combo(self, mods, name, hold=0.08):
        """Press `mods` (keysym names) and `name`, hold, release in reverse. A long `hold` lets the
        X server generate key auto-repeat, which the host must not turn into extra toggles."""
        self.ensure_focus()
        mkc = [self.d.keysym_to_keycode(XK.string_to_keysym(m)) for m in mods]
        kc = self.d.keysym_to_keycode(XK.string_to_keysym(name))
        for m in mkc:
            self._fake(X.KeyPress, m)
        self._fake(X.KeyPress, kc)
        time.sleep(hold)
        self._fake(X.KeyRelease, kc)
        for m in reversed(mkc):
            self._fake(X.KeyRelease, m)
        time.sleep(0.15)

    def move(self, x, y):
        xtest.fake_input(self.d, X.MotionNotify, False, X.CurrentTime, self.d.screen().root, x, y)
        self.d.sync()
        time.sleep(0.15)

    def click(self, x, y):
        """Warp to (x, y) and click with no gap in between. Returns False (without clicking) when the
        pointer is not where it was put: the host's real pointer shares this server (see header)."""
        xtest.fake_input(self.d, X.MotionNotify, False, X.CurrentTime, self.d.screen().root, x, y)
        self.d.sync()
        time.sleep(0.1)          # the menu needs a rendered frame under the pointer (hover) before the press
        if self.pointer() != (x, y):
            return False
        self._fake(X.ButtonPress, 1)
        time.sleep(0.05)
        self._fake(X.ButtonRelease, 1)
        time.sleep(0.1)
        return True

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
                    if w.get_attributes().map_state != X.IsViewable:
                        continue
                    self.focus_win = w
                    w.set_input_focus(X.RevertToParent, X.CurrentTime)
                    self.d.sync()
                    return w
            time.sleep(0.2)
        raise RuntimeError(f"game window did not appear within {timeout}s "
                           f"(top-level windows: {[str(c.get_wm_name()) for c in self.d.screen().root.query_tree().children]})")

    def geometry(self, win):
        g = win.get_geometry()
        return g.width, g.height

    def resize(self, win, w, h):
        win.configure(width=w, height=h)
        self.d.sync()
        ok, st = wait_until(lambda: (self.geometry(win) == (w, h), self.geometry(win)),
                            f"window resize to {w}x{h}", timeout=5.0)
        check(ok, f"window resized to {w}x{h}" if ok else f"window resize to {w}x{h} [timeout 5s; geometry {st}]")

    def grab(self, win, timeout=5.0):
        """Screenshot of the game window; waits for the window to be viewable and inside the screen
        (GetImage raises BadMatch otherwise) instead of failing on a transient geometry."""
        last = {}

        def probe():
            g = win.get_geometry()
            a = win.get_attributes()
            last.update(w=g.width, h=g.height, x=g.x, y=g.y, map_state=a.map_state)
            if a.map_state != X.IsViewable:
                return False, last
            # GetImage needs the rectangle inside the screen: a window larger than the nested
            # screen (the resize tests) is captured clipped to the visible part.
            sw, sh = self.size
            ox, oy = max(0, -g.x), max(0, -g.y)
            w = min(g.width - ox, sw - max(g.x, 0))
            h = min(g.height - oy, sh - max(g.y, 0))
            last.update(clip=(ox, oy, w, h))
            raw = win.get_image(ox, oy, w, h, X.ZPixmap, 0xffffffff)
            last["img"] = Image.frombytes("RGB", (w, h), raw.data, "raw", "BGRX")
            return True, last
        ok, st = wait_until(probe, "screenshot", timeout)
        if not ok:
            raise RuntimeError(f"screenshot not available after {timeout}s: {st}")
        return st["img"]

    def settled(self, win, pred, what, timeout=6.0):
        """Screenshot that satisfies `pred(img)` and is identical to the next one (>= one fully
        rendered frame after the change). Returns the image; records a failed check otherwise."""
        state = {}

        def probe():
            a = self.grab(win)
            time.sleep(0.12)
            b = self.grab(win)
            stable = a.size == b.size and ImageChops.difference(a, b).getbbox() is None
            state["stable"] = stable
            state["pred"] = bool(pred(b))
            state["img"] = b
            return stable and state["pred"], dict(stable=stable, pred=state["pred"], size=b.size)
        ok, st = wait_until(probe, what, timeout, interval=0.05)
        if not ok:
            print(f"        timeout waiting for: {what}; last state: {st}")
        return state.get("img")

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

    def __init__(self, binary, config, rom, bios, display_name, workdir, observe=True,
                 extra_args=(), extra_env=None):
        self.port = free_port()
        env = dict(os.environ, DISPLAY=display_name, GBARECOMP_STRICT_STATIC="1",
                   SDL_AUDIODRIVER="dummy", GBARECOMP_AUDIO_PROBE="1",
                   **(extra_env or {}))
        self.lines = []
        self.stamps = []          # arrival time of each line (relative to launch), for diagnostics
        self.t_launch = time.time()
        self.proc = subprocess.Popen(
            [binary, "--no-launcher", "--bios", bios, "--rom", rom, "--config", config,
             "--window"] + list(extra_args) +
            (["--tcp-observe", str(self.port)] if observe else []),
            env=env, cwd=workdir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.proc.stdout:
            self.stamps.append(time.time() - self.t_launch)
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

    def probe_history(self, last=12):
        """The last audio-probe lines with their arrival times, for failure diagnostics."""
        rows = [(round(t, 1), l.split("audio-probe] ")[1][:150]) for t, l in zip(self.stamps, self.lines)
                if "audio-probe]" in l]
        return rows[-last:]

    def audio_probe(self):
        """(pushes, raw bridge underruns, net underruns) of the latest probe line, or None."""
        for line in reversed(self.lines):
            m = re.search(r"audio-probe\] pushes=(\d+) .*bridge_underrun=(\d+).*net_underrun=(\d+)", line)
            if m:
                return tuple(int(x) for x in m.groups())
        return None

    def last_layout(self):
        for line in reversed(self.lines):
            if "host_window: presentation" in line and " layout=" in line:
                tail = line.split(" layout=")[1].split()
                x, y = tail[0].split(",")
                w, h = tail[1].split("x")
                return int(x), int(y), int(w), int(h)
        return None

    def wait_layout(self, want, timeout=6):
        ok, _ = wait_until(lambda: (self.last_layout() == want, self.last_layout()),
                           f"layout {want}", timeout)
        return ok

    def wait_ready(self, timeout=60.0, frames=30):
        """The guest runs: the observe port answers and the frame counter advances `frames` frames."""
        state = {}

        def probe():
            try:
                f = self.frame()
            except RuntimeError as e:
                return False, str(e)
            state.setdefault("f0", f)
            return f - state["f0"] >= frames, {"frame": f, "since": f - state["f0"]}
        ok, st = wait_until(probe, "guest running", timeout, interval=0.2)
        check(ok, "host started and the guest is running" if ok else
              f"host started and the guest is running [timeout {timeout:g}s; last state: {st}]")
        return ok

    def wait_log(self, needle, timeout=15.0):
        ok, _ = wait_until(lambda: (needle in self.log(), None), needle, timeout)
        return ok

    def frozen(self, window=0.3):
        """True when the guest frame counter did not move over `window` seconds."""
        a = self.frame()
        time.sleep(window)
        return self.frame() == a

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
GRAPHICS = {"Integer scaling": 0, "Linear filter": 1, "CRT": 2, "Scanline strength": 3,
            "Color correction": 4}
PERFORMANCE = {"Show FPS": 0, "VSync": 1, "Performance overlay": 2, "Presentation Refresh": 3}


class Menu:
    """Drives the ESC menu from the keyboard. Every open/close waits for an observable effect.

    The shared menu keeps `section_index` across open/close (and resets the row), and it also moves
    the selection to whatever the pointer hovers, which is why the pointer is parked (see Xserver)
    while this class navigates. `host_paused` tells the acks the guest is frozen by the host pause
    hotkey, so the frame counter cannot be the evidence and the picture is."""

    def __init__(self, xs, game=None, win=None):
        self.xs = xs
        self.game = game
        self.win = win
        self.section = 0   # the shared menu remembers the last section across open/close
        self.host_paused = False

    def _ack(self, opening, before, fast):
        if fast or self.win is None:
            time.sleep(0.4)
            return
        what = "menu open" if opening else "menu closed"
        if self.game is not None and not self.host_paused:
            if opening:
                ok, st = wait_until(lambda: (self.game.frozen(0.25), "guest still advancing"),
                                    what, timeout=5.0, interval=0.05)
            else:
                def running():
                    a = self.game.frame()
                    time.sleep(0.25)
                    b = self.game.frame()
                    return b - a >= 5, {"frames in 0.25s": b - a}
                ok, st = wait_until(running, what, timeout=6.0, interval=0.05)
        else:
            ok, st = wait_until(
                lambda: (ImageChops.difference(self.xs.grab(self.win), before).getbbox() is not None,
                         "picture unchanged"), what, timeout=5.0, interval=0.1)
        if not ok:
            raise RuntimeError(f"ESC did not {'open' if opening else 'close'} the menu "
                               f"[last state: {st}; pointer {self.xs.pointer()}; "
                               f"focus_ok={self.xs.focus_ok()}; pointer_parked={self.xs.parked}]")
        time.sleep(0.1)

    def _need_picture(self, fast):
        # the picture is the evidence only when the frame counter cannot be (host pause / no observe port)
        return self.win is not None and not fast and (self.host_paused or self.game is None)

    def open(self, fast=False):
        before = self.xs.grab(self.win) if self._need_picture(fast) else None
        self.xs.key("Escape")
        self._ack(True, before, fast)

    def close(self, fast=False):
        before = self.xs.grab(self.win) if self._need_picture(fast) else None
        self.xs.key("Escape")
        self._ack(False, before, fast)

    def activate(self, section, row, presses=1, key="Return", around=None):
        """Open the menu, enter `section`, move to `row`, press `key` `presses` times, close.

        `around`, if given, is called once right after the menu opened and once right before it
        closes (the guest is paused for both); the pair of results is returned."""
        self.open()
        before = around() if around else None
        idx = SECTIONS.index(section)
        for _ in range((idx - self.section) % len(SECTIONS)):
            self.xs.key("Down")
        self.section = idx
        self.xs.key("Return")
        for _ in range(row):
            self.xs.key("Down")
        for _ in range(presses):
            self.xs.key(key)
        after = around() if around else None
        self.close()
        return (before, after) if around else None


def display_ini_text():
    return ("[Display]\nwindow_mode = windowed\nwindow_scale = 3\nscale_mode = integer\naspect = native\n"
            "filtering = nearest\nvsync = 0\nshow_fps = 1\ncrt_enabled = 1\ncrt_preset = lite\n"
            "scanline_strength = 60\ncolor_profile = gba-like\n")


def read_ini(path):
    try:
        return open(path).read()
    except OSError:
        return ""


def scale_presets_resume_borderless(args):
    """Window-scale presets through the menu, Borderless (own larger screen), the Resume button."""
    print("== H: window scale presets, Resume button, Borderless (1600x1000 nested screen)")
    work = tempfile.mkdtemp(prefix="mzm-display-h-")
    exe = os.path.join(work, "MZMRecomp")
    shutil.copy2(args.binary, exe)
    ini = os.path.join(work, "config.ini")
    xs = Xserver(1600, 1000)
    game = None
    try:
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work)
        win = xs.game_window()
        xs.park()
        game.wait_ready()
        menu = Menu(xs, game, win)

        def size():
            g = win.get_geometry()
            return g.width, g.height

        def size_is(want):
            return lambda: (size() == want, f"window {size()} want {want}; layout {game.last_layout()}")

        check_wait(size_is((720, 480)), "default window is 3x", timeout=5.0)
        menu.activate("Display", DISPLAY["Window scale"], presses=2, key="Left")
        check_wait(size_is((240, 160)), "menu scale 1x -> 240x160", timeout=6.0)
        check_layout(game, (0, 0, 240, 160), "1x destination is native")
        menu.activate("Display", DISPLAY["Window scale"], presses=5, key="Right")
        check_wait(size_is((1440, 960)), "menu scale 6x -> 1440x960", timeout=6.0)
        check_layout(game, (0, 0, 1440, 960), "6x destination fills the window")
        menu.activate("Display", DISPLAY["Window scale"], presses=3, key="Left")
        check_wait(size_is((720, 480)), "menu scale back to 3x", timeout=6.0)

        # Borderless fullscreen. Without a window manager the X window geometry is stale, so the
        # renderer's own destination rectangle (its layout log line) is the evidence, and the
        # selected mode persisted in config.ini is the secondary one.
        menu.activate("Display", DISPLAY["Fullscreen"], presses=1, key="Right")
        check_wait(lambda: (game.last_layout() == (50, 0, 1500, 1000),
                            f"layout {game.last_layout()}; window_mode: "
                            f"{[l for l in read_ini(ini).splitlines() if 'window_mode' in l]}"),
                   "Borderless fullscreen letterboxes 3:2 into the 1600x1000 screen", timeout=10.0)
        check(game.alive(), "host alive in Borderless")
        menu.activate("Display", DISPLAY["Fullscreen"], presses=1, key="Left")
        check_wait(lambda: (game.last_layout() is not None and game.last_layout()[2:] == (720, 480),
                            f"layout {game.last_layout()}"),
                   "back to a 720x480 window", timeout=10.0)

        # Resume button: ESC opens, a mouse click on the footer Resume button closes. This hovers the
        # menu on purpose, so it is the last step (hover moves the keyboard selection).
        g = win.get_geometry()
        menu.open()
        a = game.frame()
        time.sleep(1.0)
        check(game.frame() - a <= 1, "guest held while the menu is open")
        xs.unpark()
        clicked = 0
        for attempt in range(5):
            if xs.click(g.x + 627, g.y + 410):
                clicked += 1
            ok, _ = wait_until(lambda: (not game.frozen(0.25), "guest still frozen after the click"),
                               "Resume click", timeout=2.0, interval=0.05)
            if ok:
                break
        if attempt:
            print(f"        note: the Resume click needed {attempt + 1} attempt(s) "
                  f"(pointer displaced by foreign input {attempt + 1 - clicked} time(s))")
        check_wait(lambda: (not game.frozen(0.25), "guest still frozen after the click"),
                   "Resume button closes the menu", timeout=1.0, interval=0.05)
        b = game.frame()
        time.sleep(1.5)
        check(game.frame() - b >= 60, "the guest runs again after Resume")
        xs.park()
        check(game.alive(), "host stays up through scale/Borderless/Resume")
    except Exception:
        if game:
            print("---- game log ----\n" + game.log()[-3000:])
        raise
    finally:
        if game and game.alive():
            game.proc.kill()
        xs.stop()
        shutil.rmtree(work, ignore_errors=True)


class VirtualPad:
    """Drives the SDL virtual controller the host attaches under GBARECOMP_TEST_VIRTUAL_PAD."""

    def __init__(self, path):
        self.path = path
        open(path, "w").close()

    def _w(self, line):
        with open(self.path, "a") as f:
            f.write(line + "\n")

    def down(self, name):
        self._w(f"b {name} 1")

    def up(self, name):
        self._w(f"b {name} 0")

    def tap(self, name, hold=0.12, gap=0.2):
        self.down(name)
        time.sleep(hold)
        self.up(name)
        time.sleep(gap)

    def stick(self, axis, value):
        self._w(f"a {axis} {value}")

    def stick_tap(self, axis, value, hold=0.12, gap=0.25):
        self.stick(axis, value)
        time.sleep(hold)
        self.stick(axis, 0)
        time.sleep(gap)


def read_record(path):
    """(frame, keyinput) rows of the guest KEYINPUT trace (what the guest actually read)."""
    rows = []
    try:
        for line in open(path):
            if line.startswith("#") or "," not in line:
                continue
            f, k = line.strip().split(",")
            rows.append((int(f), int(k, 16)))
    except OSError:
        pass
    return rows


def observe(game, cmd):
    try:
        with socket.create_connection(("127.0.0.1", game.port), timeout=5) as sk:
            sk.sendall((json.dumps({"cmd": cmd}) + "\n").encode())
            return json.loads(sk.makefile().readline())
    except Exception:
        return None


def enhancements_2(args):
    """ENHANCEMENTS-2 host controls: Alt+Enter, controller navigation and input ownership, CRT
    presets, Presentation Refresh (forced 144 Hz: synthetic evidence)."""
    print("== G: Alt+Enter, controller navigation, input ownership, Presentation Refresh "
          "(monitor refresh forced to 144 Hz: synthetic / nested-display evidence)")
    work = tempfile.mkdtemp(prefix="mzm-display-g-")
    exe = os.path.join(work, "MZMRecomp")
    shutil.copy2(args.binary, exe)
    ini = os.path.join(work, "config.ini")
    cmdfile = os.path.join(work, "pad.cmd")
    record = os.path.join(work, "input.rec")
    env = dict(GBARECOMP_TEST_VIRTUAL_PAD=cmdfile, GBARECOMP_FORCE_REFRESH_HZ="144",
               GBARECOMP_INPUT_RECORD=record)
    xs = Xserver(1600, 1000)
    game = None
    rate_re = r"EMU ([0-9.]+) \| PRESENT ([0-9.]+) fps"

    def mode_changes(g):
        return sum(1 for l in g.lines if "host_window: fullscreen-borderless display" in l
                   or "host_window: windowed display" in l)

    try:
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work, extra_env=env)
        win = xs.game_window()
        xs.park()
        game.wait_ready()
        pad = VirtualPad(cmdfile)
        check(game.wait_log("virtual test pad attached"), "test controller attached to the host")
        menu = Menu(xs, game, win)

        # ---- Alt+Enter -------------------------------------------------------------------
        print("   -- Alt+Enter")
        check_wait(lambda: (win.get_geometry().width == 720, win.get_geometry().width), "starts as a 3x window", timeout=5.0)
        n0 = mode_changes(game)
        xs.combo(["Alt_L"], "Return")
        check_wait(lambda: (game.last_layout() == (50, 0, 1500, 1000), f"layout {game.last_layout()}"),
                   "Alt+Enter enters Borderless fullscreen (3:2 letterboxed in 1600x1000)", timeout=10.0)
        check_wait(lambda: ("window_mode = borderless" in read_ini(ini), read_ini(ini)[-250:]),
                   "Alt+Enter persists window_mode = borderless", timeout=5.0)
        xs.combo(["Alt_L"], "Return")
        check_wait(lambda: (game.last_layout() is not None and game.last_layout()[2:] == (720, 480),
                            f"layout {game.last_layout()}"),
                   "Alt+Enter returns to the 720x480 window", timeout=10.0)
        check_wait(lambda: ("window_mode = windowed" in read_ini(ini), read_ini(ini)[-250:]),
                   "Alt+Enter persists window_mode = windowed", timeout=5.0)
        check(mode_changes(game) - n0 == 2, f"two Alt+Enter presses made exactly two mode changes ({mode_changes(game) - n0})")
        n1 = mode_changes(game)
        xs.combo(["Alt_L"], "Return", hold=1.4)      # held: the X server auto-repeats the key
        check_wait(lambda: (game.last_layout() == (50, 0, 1500, 1000), f"layout {game.last_layout()}"),
                   "a held Alt+Enter toggles to Borderless", timeout=10.0)
        time.sleep(0.8)
        check(mode_changes(game) - n1 == 1,
              f"key auto-repeat did not re-trigger the toggle (one mode change, got {mode_changes(game) - n1})")
        # with the menu open: still toggles, never doubles as "accept", the menu stays open
        menu.open()
        n2 = mode_changes(game)
        xs.combo(["Alt_L"], "Return")
        check_wait(lambda: (game.last_layout() is not None and game.last_layout()[2:] == (720, 480),
                            f"layout {game.last_layout()}"),
                   "Alt+Enter works with the menu open", timeout=10.0)
        check(mode_changes(game) - n2 == 1, "exactly one mode change with the menu open")
        check(game.frozen(0.4), "the menu is still open (guest held) after Alt+Enter")
        menu.close()
        check(game.alive(), "host alive after the Alt+Enter sequence")

        # ---- controller: menu navigation -------------------------------------------------
        print("   -- controller navigation")
        pad.tap("guide")
        check_wait(lambda: (game.frozen(0.25), "guest still advancing"), "Guide opens the menu (guest held)", timeout=5.0)
        pad.tap("down")                              # section list: Display -> Graphics
        pad.tap("a")                                 # enter Graphics (row 0: Integer scaling)
        pad.stick_tap("ly", 30000)                   # left stick down: row 1 (Linear filter)
        pad.tap("a")                                 # toggle Linear
        check_wait(lambda: ("filtering = linear" in read_ini(ini), read_ini(ini)[-250:]),
                   "D-pad + stick + A reached and toggled 'Linear filter' (one row per tap)", timeout=5.0)
        # a stick HELD for 300 ms (about 18 polls) must move exactly one row, not scroll
        pad.stick("ly", 30000)
        time.sleep(0.3)
        pad.stick("ly", 0)
        time.sleep(0.25)
        pad.tap("a")                                 # row 2: CRT  (Off -> Lite)
        check_wait(lambda: ("crt_enabled = 1" in read_ini(ini) and "crt_preset = lite" in read_ini(ini),
                            read_ini(ini)[-250:]),
                   "a held stick moved exactly one row (debounced): the next A hit 'CRT' (Off -> Lite)", timeout=5.0)
        pad.tap("a")                                 # Lite -> Soft
        check_wait(lambda: ("crt_preset = soft" in read_ini(ini), read_ini(ini)[-250:]),
                   "A cycles the CRT preset (Soft)", timeout=5.0)
        pad.tap("right")                             # Soft -> Strong
        check_wait(lambda: ("crt_preset = strong" in read_ini(ini), read_ini(ini)[-250:]),
                   "D-pad right adjusts the choice (Strong)", timeout=5.0)
        pad.tap("left")                              # Strong -> Soft again
        check_wait(lambda: ("crt_preset = soft" in read_ini(ini), read_ini(ini)[-250:]),
                   "D-pad left adjusts the choice back (Soft)", timeout=5.0)
        pad.tap("b")                                 # back out of the section
        check(game.frozen(0.3), "B backs out of a section without closing the menu")
        pad.tap("b")                                 # close
        check_wait(lambda: (not game.frozen(0.25), "guest still held"), "B closes the menu", timeout=5.0)
        pad.tap("guide")
        check_wait(lambda: (game.frozen(0.25), "guest running"), "Guide opens the menu again", timeout=5.0)
        pad.tap("start")
        check_wait(lambda: (not game.frozen(0.25), "guest still held"), "Start resumes (closes the menu)", timeout=5.0)

        # ---- input ownership -------------------------------------------------------------
        print("   -- guest input ownership")
        RELEASED = 0x03FF
        f_mark = game.frame()
        pad.tap("a", hold=0.3)                       # menu closed: the guest must see A
        ok, rows = wait_until(lambda: (any(f >= f_mark and not (k & 1) for f, k in read_record(record)),
                                       read_record(record)[-4:]), "A reaches the guest", timeout=5.0)
        check(ok, "menu closed: controller A reaches the guest")
        pad.tap("guide")
        check_wait(lambda: (game.frozen(0.25), "guest running"), "menu open for the ownership test", timeout=5.0)
        n_rows = len(read_record(record))
        for name in ("a", "b", "up", "down", "left", "right", "x", "y", "lb", "rb", "back"):
            pad.tap(name, hold=0.1, gap=0.1)
        pad.stick_tap("lx", 30000)
        pad.stick_tap("ly", -30000)
        time.sleep(0.4)
        # While open the recorded KEYINPUT may only be "all released" (it can add at most the one
        # transition row to RELEASED when the menu opened over a held button).
        new_rows = read_record(record)[n_rows:]
        check(all(k == RELEASED for _, k in new_rows),
              f"menu open: the guest saw no controller input ({len(new_rows)} new rows: {new_rows[:4]})")
        # Close with B while keeping B held: the guest must not see B until it is released.
        pad.tap("guide")                             # close via Guide so section state is predictable
        check_wait(lambda: (not game.frozen(0.25), "guest still held"), "menu closed for the latch test", timeout=5.0)
        pad.tap("guide")
        check_wait(lambda: (game.frozen(0.25), "guest running"), "menu re-opened for the latch test", timeout=5.0)
        pad.down("b")                                # B: top level of the menu closes it
        check_wait(lambda: (not game.frozen(0.25), "guest still held"), "B (held) closed the menu", timeout=5.0)
        f_close = game.frame()
        time.sleep(0.6)                              # B still physically down, menu closed
        held_rows = [(f, k) for f, k in read_record(record) if f >= f_close and not (k & 2)]
        check(not held_rows, f"a button held across the menu close is masked from the guest ({held_rows[:3]})")
        pad.up("b")
        time.sleep(0.3)
        f_after = game.frame()
        pad.tap("b", hold=0.3)
        ok, _ = wait_until(lambda: (any(f >= f_after and not (k & 2) for f, k in read_record(record)),
                                    read_record(record)[-4:]), "B reaches the guest again", timeout=5.0)
        check(ok, "after releasing, a new B press reaches the guest again")
        # Start resumes the menu; the Start still held after that must not pause/start the game.
        pad.tap("guide")
        check_wait(lambda: (game.frozen(0.25), "guest running"), "menu open for the Start test", timeout=5.0)
        pad.down("start")
        check_wait(lambda: (not game.frozen(0.25), "guest still held"), "Start (held) resumed the game", timeout=5.0)
        f_start = game.frame()
        time.sleep(0.6)
        start_rows = [(f, k) for f, k in read_record(record) if f >= f_start and not (k & 8)]
        check(not start_rows, f"the Start that resumed the menu does not reach the guest ({start_rows[:3]})")
        pad.up("start")
        time.sleep(0.3)
        game.stop()
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work, extra_env=env)
        win = xs.game_window()
        game.wait_ready()
        menu = Menu(xs, game, win)       # a fresh process: the shared menu starts on its first section

        # ---- Presentation Refresh (forced 144 Hz: synthetic) -----------------------------
        print("   -- Presentation Refresh (forced 144 Hz)")
        menu.activate("Performance", PERFORMANCE["Show FPS"])
        def steady(lo_p, hi_p):
            def probe():
                mm = re.search(rate_re, str(win.get_wm_name()))
                if not mm:
                    return False, f"no EMU/PRESENT in title: {win.get_wm_name()!r}"
                e, pr = float(mm.group(1)), float(mm.group(2))
                return (55.0 <= e <= 64.0 and lo_p <= pr <= hi_p), f"EMU={e} PRESENT={pr}"
            return probe
        check_wait(steady(55.0, 64.0), "Native: PRESENT follows the guest (one present per guest frame)",
                   timeout=15.0, interval=0.25)
        menu.activate("Performance", PERFORMANCE["Presentation Refresh"], key="Right")
        check_wait(lambda: ("presentation = monitor" in read_ini(ini), read_ini(ini)[-300:]),
                   "Presentation Refresh = Monitor persists", timeout=5.0)
        check_wait(steady(100.0, 170.0), "Monitor Refresh (forced 144 Hz): PRESENT ~144 while EMU stays ~59.7",
                   timeout=15.0, interval=0.25)
        print("        title:", win.get_wm_name())
        f0, t0 = game.frame(), time.time()
        time.sleep(4.0)
        f1, t1 = game.frame(), time.time()
        guest_hz = (f1 - f0) / (t1 - t0)
        check(58.5 <= guest_hz <= 60.9, f"guest frame counter still advances at ~59.7 Hz ({guest_hz:.2f})")
        # held guest (menu open): EMU 0, PRESENT keeps going at the monitor rate, state does not move
        menu.open()
        def held_rates():
            mm = re.search(rate_re, str(win.get_wm_name()))
            if not mm:
                return False, str(win.get_wm_name())
            e, pr = float(mm.group(1)), float(mm.group(2))
            return (e < 2.0 and pr > 100.0), f"EMU={e} PRESENT={pr}"
        check_wait(held_rates, "menu open (Monitor Refresh): EMU 0, PRESENT continues at the monitor rate", timeout=10.0)
        h0 = observe(game, "state_hash")
        shot_a = xs.grab(win)
        time.sleep(1.2)
        h1 = observe(game, "state_hash")
        shot_b = xs.grab(win)
        if h0 and h0.get("ok") and h1 and h1.get("ok"):
            keys = ("cycles", "iwram", "ewram", "vram", "pal", "oam")
            check(all(h0.get(k) == h1.get(k) for k in keys),
                  "machine state hash (cycles, IWRAM/EWRAM/VRAM/PAL/OAM) identical across repeated presents")
        else:
            check(False, f"state_hash unavailable on the observe port ({h0})")
        check(ImageChops.difference(shot_a, shot_b).getbbox() is None,
              "repeated presents show the identical picture (no interpolation, no blending)")
        # the presentation setting is still selectable while held and Native restores 1:1
        menu.close()
        menu.activate("Performance", PERFORMANCE["Presentation Refresh"], key="Left")
        check_wait(steady(55.0, 64.0), "back to Native: PRESENT returns to the guest rate", timeout=15.0, interval=0.25)
        menu.activate("Performance", PERFORMANCE["Presentation Refresh"], key="Right")
        check_wait(steady(100.0, 170.0), "Monitor Refresh selected again for the restart check", timeout=15.0, interval=0.25)
        game.stop()

        game = Game(exe, args.config, args.rom, args.bios, xs.name, work, extra_env=env)
        win = xs.game_window()
        game.wait_ready()
        check_wait(steady(100.0, 170.0), "Monitor Refresh survives a restart (PRESENT ~144, EMU ~59.7)",
                   timeout=20.0, interval=0.25)
        check(game.alive(), "session alive with Monitor Refresh + controller + CRT presets")
        game.stop()

        # a config from before ENHANCEMENTS-2: no presentation key, hand-tuned lite -> Custom
        with open(ini, "w") as f:
            f.write("[Display]\nwindow_scale = 3\ncrt_enabled = 1\ncrt_preset = lite\nscanline_strength = 65\n")
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work, extra_env=env, observe=False)
        win = xs.game_window()
        check(game.wait_log("host_window: presentation", timeout=60.0), "an ENHANCEMENTS-1 config loads and presents")
        time.sleep(1.0)
        menu = Menu(xs, None, win)
        menu.activate("Performance", PERFORMANCE["Show FPS"])
        check_wait(lambda: ("presentation = native" in read_ini(ini) and "scanline_strength = 65" in read_ini(ini)
                            and "crt_preset = custom" in read_ini(ini), read_ini(ini)[-300:]),
                   "old config migrates: presentation defaults to native, the tuned strength stays as Custom",
                   timeout=8.0)
        game.proc.terminate()
        log = game.finish(60)
        check(game.proc.returncode == 0, f"clean exit status ({game.proc.returncode})")
        check("self_heal_coverage=FULLY_STATIC" in log, "strict-static: FULLY_STATIC")
        for key in ("dispatch_misses=0", "interpreted_insns=0", "unmapped=0", "io_unhandled=0"):
            check(key in log, f"strict-static: {key}")
    except Exception:
        if game:
            print("---- game log ----\n" + game.log()[-4000:])
        raise
    finally:
        if game and game.alive():
            game.proc.kill()
        xs.stop()
        shutil.rmtree(work, ignore_errors=True)


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
        xs.park()                # pointer parked (grabbed) off the window for the whole session
        game.wait_ready()
        f0 = game.frame()
        time.sleep(1.5)
        f1 = game.frame()
        check(f1 - f0 >= 60, f"guest advances at ~60 Hz with the menu closed ({f1 - f0} frames/1.5s)")
        base = xs.grab(win)
        pb_play = game.audio_probe()
        check(pb_play is not None and pb_play[1] == 0 and pb_play[2] == 0,
              f"normal play: no underruns at all, raw or corrected ({pb_play})")

        menu = Menu(xs, game, win)
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

        # Open/close many times: no quit, no hang, and no catch-up burst on resume (the pacer
        # is realigned, so the guest never fast-forwards to repay the time spent in the menu).
        s0, t0 = game.frame(), time.time()
        held_total = 0.0
        for _ in range(25):
            menu.open(fast=True)
            h0, th = game.frame(), time.time()
            time.sleep(0.15)
            check_h = game.frame() - h0
            held_total += time.time() - th
            if check_h != 0:
                check(False, f"guest advanced {check_h} frames while the menu was open")
                break
            menu.close(fast=True)
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
        # The holds starve the audio ring on purpose: the raw bridge counter grows, the corrected
        # one (the overlay's) must not, including the ~400 ms refill after each resume.
        time.sleep(3.0)
        pb_a = game.audio_probe()
        time.sleep(3.0)
        pb_b = game.audio_probe()
        check(pb_a and pb_b and pb_b[0] > pb_a[0], f"audio pushes keep flowing after the holds ({pb_a} -> {pb_b})")
        # Corrected underruns count only those after the 400 ms resume grace (host_window.cpp
        # kResumeGraceMs); under scheduling jitter one refill after a resume can outlast it
        # (seen once in ~30 runs: 1767 corrected vs 1247997 raw). That tail is tolerated only in
        # this post-stress snapshot, bounded to 1% of raw and < 32768 samples (under one
        # second of audio); normal play above and the flat-counter check below stay strict.
        starve_ok = bool(pb_b and pb_b[1] > 0 and pb_b[2] <= pb_b[1] * 0.01 and pb_b[2] < 32768)
        check(starve_ok,
              f"intentional starvation is excluded (raw {pb_b and pb_b[1]}, corrected {pb_b and pb_b[2]})")
        if pb_b and pb_b[2] != 0:
            print(f"        note: {pb_b[2]} corrected underrun(s) after resume grace; probe history:")
            for t, l in game.probe_history():
                print(f"          t={t} {l}")
        check(pb_a and pb_b and pb_b[1] == pb_a[1] and pb_b[2] == pb_a[2],
              "after the refill grace both counters are flat (no underruns while playing)")

        print("== D: filtering and CRT Lite (guest frozen with the pause hotkey for stable frames)")
        xs.key("p", shift=True)                 # host pause: identical frames
        check_wait(lambda: (game.frozen(0.25), "guest still advancing"),
                   "host pause hotkey freezes the guest", timeout=5.0, interval=0.05)
        menu.host_paused = True                 # the frame counter cannot be the menu evidence now
        scale = win.get_geometry().width // 240
        off = xs.settled(win, lambda im: True, "stable paused picture")
        check(block_uniform_fraction(off, scale) > 0.995, "default filtering is nearest (blocky 3x3 pixels)")
        same_as_off = lambda im: im.size == off.size and ImageChops.difference(off, im).getbbox() is None
        menu.activate("Graphics", GRAPHICS["Linear filter"])
        lin = xs.settled(win, lambda im: block_uniform_fraction(im, scale) < 0.97, "Linear filtering applied")
        check(block_uniform_fraction(lin, scale) < 0.97, "Linear filtering smooths the image at runtime")
        menu.activate("Graphics", GRAPHICS["Linear filter"])
        again = xs.settled(win, same_as_off, "Linear off restores the default pixels")
        check(same_as_off(again), "turning Linear off restores the exact default pixels")

        menu.activate("Graphics", GRAPHICS["CRT"])          # Off -> Lite
        crt_ok = lambda im: im.size == off.size and (lambda pr: min(pr[0]) < 0.93 and max(pr[0]) > 0.97)(
            row_ratio_profile(off, im, scale))
        crt = xs.settled(win, crt_ok, "CRT Lite scanlines applied")
        prof, brighter = row_ratio_profile(off, crt, scale)
        print(f"        per-row brightness ratio (row % {scale}): " + ", ".join(f"{v:.3f}" for v in prof))
        check(brighter == 0, "CRT Lite never brightens a pixel")
        check(min(prof) < 0.93 and max(prof) > 0.97, "CRT Lite darkens alternating rows (visible scanlines)")
        check(min(prof) > 0.6, "CRT Lite stays conservative (no row darker than 60%)")
        if args.shots:
            off.save(os.path.join(args.shots, "nearest.png"))
            lin.save(os.path.join(args.shots, "linear.png"))
            crt.save(os.path.join(args.shots, "crt-lite.png"))
        # ENHANCEMENTS-2: the CRT item is a preset choice (Off, Lite, Soft, Strong, Custom). Each
        # preset must be a distinct, conservative look and persist by name.
        check_wait(lambda: ("crt_preset = lite" in read_ini(ini), read_ini(ini)[-200:]),
                   "CRT Lite persists as crt_preset = lite", timeout=5.0)
        mins = {"lite": min(prof)}
        for name in ("soft", "strong"):
            menu.activate("Graphics", GRAPHICS["CRT"])
            cur = xs.settled(win, lambda im: im.size == off.size and ImageChops.difference(off, im).getbbox() is not None,
                             f"CRT {name} applied")
            pr, br = row_ratio_profile(off, cur, scale)
            mins[name] = min(pr)
            print(f"        CRT {name}: per-row ratio " + ", ".join(f"{v:.3f}" for v in pr))
            check(br == 0, f"CRT {name} never brightens a pixel")
            check(min(pr) > 0.6, f"CRT {name} stays readable (no row darker than 60%)")
            check_wait(lambda: (f"crt_preset = {name}" in read_ini(ini), read_ini(ini)[-200:]),
                       f"CRT {name} persists as crt_preset = {name}", timeout=5.0)
            if args.shots:
                cur.save(os.path.join(args.shots, f"crt-{name}.png"))
        check(mins["soft"] > mins["lite"] > mins["strong"],
              f"presets are ordered Soft < Lite < Strong in darkness ({mins['soft']:.3f} > {mins['lite']:.3f} > {mins['strong']:.3f})")
        menu.activate("Graphics", GRAPHICS["CRT"])          # Strong -> Custom (keeps the strength)
        check_wait(lambda: ("crt_preset = custom" in read_ini(ini) and "scanline_strength = 70" in read_ini(ini),
                            read_ini(ini)[-200:]),
                   "CRT Custom keeps the strength of the preset it came from", timeout=5.0)
        menu.activate("Graphics", GRAPHICS["CRT"])          # Custom -> Off
        check(same_as_off(xs.settled(win, same_as_off, "CRT off restores the default pixels")),
              "CRT Off restores the exact default pixels")
        menu.activate("Graphics", GRAPHICS["Color correction"])
        differs = lambda im: im.size == off.size and ImageChops.difference(off, im).getbbox() is not None
        colour = xs.settled(win, differs, "colour correction applied")
        check(differs(colour), "Color correction (GBA-like) changes the presented colours")
        if args.shots:
            colour.save(os.path.join(args.shots, "color-gba-like.png"))
        menu.activate("Graphics", GRAPHICS["Color correction"])
        check(same_as_off(xs.settled(win, same_as_off, "colour correction off restores the default pixels")),
              "Color correction Off restores the exact default pixels")
        menu.activate("Performance", PERFORMANCE["Performance overlay"])
        top_left = (0, 0, 330, 120)
        overlay = xs.settled(win, lambda im: im.size == off.size and
                             ImageChops.difference(off.crop(top_left), im.crop(top_left)).getbbox() is not None,
                             "performance overlay drawn")
        check(ImageChops.difference(off.crop(top_left), overlay.crop(top_left)).getbbox() is not None,
              "F10 / menu performance overlay draws in the corner")
        check(ImageChops.difference(off.crop((400, 200, 720, 480)), overlay.crop((400, 200, 720, 480))).getbbox() is None,
              "the overlay leaves the rest of the picture untouched")
        if args.shots:
            overlay.save(os.path.join(args.shots, "perf-overlay.png"))
        xs.key("F10")
        check(same_as_off(xs.settled(win, same_as_off, "F10 hides the overlay")), "F10 hides the overlay again")
        # persisted value (CRT toggled on then off): file reflects the final state
        check_wait(lambda: ("crt_enabled = 0" in read_ini(ini), read_ini(ini)[-300:]),
                   "config.ini [Display] records crt_enabled = 0", timeout=5.0)
        xs.key("p", shift=True)                 # resume
        menu.host_paused = False
        check_wait(lambda: (not game.frozen(0.25), "guest still frozen"),
                   "host pause hotkey resumes the guest", timeout=5.0, interval=0.05)

        print("== FPS readout: EMU (guest) and PRESENT (host) are reported separately")
        menu.activate("Performance", PERFORMANCE["Show FPS"])
        import re
        rate_re = r"EMU ([0-9.]+) \| PRESENT ([0-9.]+) fps"
        # The meter averages over a window that still contains the menu hold and the audio refill that
        # preceded it, so the readout is waited on until it reflects steady play (explicit timeout).
        def steady():
            mm = re.search(rate_re, str(win.get_wm_name()))
            if not mm:
                return False, f"no EMU/PRESENT in title: {win.get_wm_name()!r}"
            e, pr = float(mm.group(1)), float(mm.group(2))
            return (55.0 <= e <= 64.0 and 55.0 <= pr <= 64.0), f"EMU={e} PRESENT={pr}"
        ok = check_wait(steady, "title bar shows EMU ~59.7 and PRESENT equal to the guest rate (1:1 paced)",
                        timeout=15.0, interval=0.25)
        print("        title:", win.get_wm_name())
        menu.open()

        def held_rates():
            mm = re.search(rate_re, str(win.get_wm_name()))
            if not mm:
                return False, str(win.get_wm_name())
            e, pr = float(mm.group(1)), float(mm.group(2))
            return (e < 2.0 and pr > 30.0), f"EMU={e} PRESENT={pr}"
        check_wait(held_rates, "while the menu holds the guest EMU drops but PRESENT keeps going", timeout=10.0)
        menu.close()
        menu.activate("Performance", PERFORMANCE["Show FPS"])

        print("== C: aspect, integer scaling, stretch (real destination rectangle)")
        xs.resize(win, 1000, 700)
        check_layout(game, (0, 17, 1000, 666), "Native 3:2 letterboxes a 1000x700 window")
        menu.activate("Graphics", GRAPHICS["Integer scaling"])
        check_layout(game, (20, 30, 960, 640), "Integer scaling snaps 1000x700 to 4x (960x640) centred")
        xs.resize(win, 1100, 800)
        check_layout(game, (70, 80, 960, 640), "Integer scaling follows the window (4x: 1100/240, 800/160)")
        xs.resize(win, 1300, 1000)
        check_layout(game, (50, 100, 1200, 800), "Integer scaling picks the largest whole multiple (5x)")
        menu.activate("Graphics", GRAPHICS["Integer scaling"])      # back to Fit
        xs.resize(win, 1000, 700)
        check_layout(game, (0, 17, 1000, 666), "Fit is restored when Integer scaling is switched off")
        menu.activate("Display", DISPLAY["Aspect ratio"])
        check_layout(game, (0, 0, 1000, 700), "Stretch fills the whole window")
        menu.activate("Display", DISPLAY["Aspect ratio"])
        check_layout(game, (0, 17, 1000, 666), "Native aspect is restored")

        print("== E: persistence, invalid values, restore defaults")
        menu.activate("Graphics", GRAPHICS["Integer scaling"])
        menu.activate("Graphics", GRAPHICS["CRT"])           # Off -> Lite
        menu.activate("Graphics", GRAPHICS["Scanline strength"], presses=2, key="Right")
        menu.activate("Performance", PERFORMANCE["Show FPS"])
        check_wait(lambda: (all(k in read_ini(ini) for k in ("scale_mode = integer", "crt_enabled = 1", "show_fps = 1")),
                            read_ini(ini)[-300:]),
                   "choices are written to config.ini [Display]", timeout=5.0)
        check_wait(lambda: ("scanline_strength = 50" in read_ini(ini), read_ini(ini)[-300:]),
                   "scanline strength steps by 5% and persists (40 -> 50)", timeout=5.0)
        check_wait(lambda: ("crt_preset = custom" in read_ini(ini), read_ini(ini)[-300:]),
                   "moving the slider switches the preset to Custom (no contradictory UI)", timeout=5.0)

        # ESC must also close the menu with the pointer resting on a menu row. This is the only
        # step that hovers the menu on purpose (hover moves the selection, so it runs last in the
        # session whose navigation model it would otherwise invalidate).
        xs.unpark()
        xs.move(360, 240)
        hover = Menu(xs, game, win)
        hover.open()
        p0 = game.frame()
        hover.close()
        check_wait(lambda: (game.frame() - p0 >= 30, f"{game.frame() - p0} frames since close"),
                   "ESC closes the menu with the mouse over a row", timeout=6.0)
        xs.park()
        game.stop()

        game = Game(exe, args.config, args.rom, args.bios, xs.name, work)
        win = xs.game_window()
        game.wait_ready()
        check_wait(lambda: ("display settings loaded from config.ini" in game.log(), game.log()[-300:]),
                   "settings are re-applied after a restart", timeout=15.0)
        xs.resize(win, 1000, 700)
        check_layout(game, (20, 30, 960, 640), "Integer scaling survives the restart")
        game.stop()

        with open(ini, "w") as f:
            f.write("[KeyMap]\nPause = Shift+P\n\n[Display]\nwindow_mode = sideways\nwindow_scale = 99\n"
                    "filtering = linear\ncrt_enabled = banana\nscanline_strength = 70\n")
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work)
        win = xs.game_window()
        game.wait_ready()
        check(game.alive(), "invalid [Display] values do not prevent startup")
        check_wait(lambda: ("ignored 3 invalid [Display] line(s)" in game.log(), game.log()[-300:]),
                   "invalid values are reported and ignored", timeout=10.0)
        check_wait(lambda: (win.get_geometry().width == 720, win.get_geometry().width),
                   "invalid window values fall back to the default size", timeout=5.0)
        menu = Menu(xs, game, win)
        menu.activate("Display", DISPLAY["Restore display defaults"])

        def restored():
            t = read_ini(ini)
            return ("filtering = nearest" in t and "scanline_strength = 40" in t
                    and "scale_mode = fit" in t and "crt_enabled = 0" in t
                    and "crt_preset = lite" in t and "presentation = native" in t), t[-300:]
        check_wait(restored, "Restore display defaults resets and persists every option", timeout=5.0)
        text = read_ini(ini)
        check("[KeyMap]" in text and "Pause = Shift+P" in text,
              "saving [Display] preserves the other config.ini sections")

        game.stop()

        print("== F: strict-static through a windowed session that uses the menu and the options")
        with open(ini, "w") as f:
            f.write(display_ini_text())
        game = Game(exe, args.config, args.rom, args.bios, xs.name, work, observe=False)
        win = xs.game_window()
        check(game.wait_log("host_window: presentation", timeout=60.0), "windowed session presented its first frame")
        menu = Menu(xs, None, win)         # no observe port here: the picture is the menu evidence
        menu.activate("Graphics", GRAPHICS["Linear filter"])
        check_wait(lambda: ("filtering = linear" in read_ini(ini), read_ini(ini)[-200:]),
                   "Linear filter reached config.ini in the strict session", timeout=5.0)
        menu.activate("Performance", PERFORMANCE["Performance overlay"])
        xs.key("F10")                      # developer overlay hotkey toggles it back off
        time.sleep(3)                      # soak: let the session run with the options on
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

    scale_presets_resume_borderless(args)
    enhancements_2(args)

    if FAILURES:
        print(f"\n{len(FAILURES)} check(s) failed")
        return 1
    print("\nhost display qualification: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
