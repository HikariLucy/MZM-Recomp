#!/usr/bin/env python3
"""
Beta 3 package smoke (Linux): extract the archive into a clean temp folder, then drive the REAL
launcher in a private nested X server (Xephyr): type the ROM and BIOS paths, CONTINUE, PLAY,
reach the game, check the log, user-data locations and that nothing resolves into the repo.

usage: smoke-beta-package.py <archive.tar.gz> <rom> <bios> [--shots DIR]
Exit 77 when Xephyr/python-xlib/Pillow are unavailable. Needs no ROM/BIOS inside the archive.
"""
import importlib.util, os, shutil, subprocess, sys, tempfile, time, argparse, json

ap = argparse.ArgumentParser()
ap.add_argument("archive"); ap.add_argument("rom"); ap.add_argument("bios")
ap.add_argument("--shots", default=None)
args = ap.parse_args()

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("hd", os.path.join(here, "run-host-display.py"))
hd = importlib.util.module_from_spec(spec); sys.modules["hd"] = hd; spec.loader.exec_module(hd)
from PIL import Image
from Xlib import X, XK

KEYS = {"/": "slash", "_": "underscore", "-": "minus", ".": "period", " ": "space"}
failures = []
def check(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg)
    if not cond: failures.append(msg)

base = tempfile.mkdtemp(prefix="mzm-b3-smoke-")
pkg = os.path.join(base, "extracted"); os.makedirs(pkg)
games = os.path.join(base, "games"); os.makedirs(games)
shutil.copy(args.rom, os.path.join(games, "mzm.gba")); shutil.copy(args.bios, os.path.join(games, "bios.bin"))
subprocess.check_call(["tar", "-xzf", args.archive, "-C", pkg])
root = os.path.join(pkg, os.listdir(pkg)[0])
exe = os.path.join(root, "MZMRecomp")
home = os.path.join(base, "home"); os.makedirs(home)
cfg, state = os.path.join(base, "cfg"), os.path.join(base, "state")
print("work dir:", base)

xs = hd.Xserver(1280, 800)
port = hd.free_port()
env = dict(os.environ, DISPLAY=xs.name, HOME=home, XDG_CONFIG_HOME=cfg, XDG_STATE_HOME=state,
           SDL_AUDIODRIVER="dummy", GBARECOMP_AUDIO_PROBE="1")
for k in [k for k in env if k.startswith("GBARECOMP_") and k != "GBARECOMP_AUDIO_PROBE"]:
    del env[k]                           # nothing test-only may be required
proc = subprocess.Popen([exe, "--tcp-observe", str(port)], env=env, cwd=base,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
lines = []
import threading
threading.Thread(target=lambda: [lines.append(l.rstrip("\n")) for l in proc.stdout], daemon=True).start()

def shot(name):
    if not args.shots: return
    os.makedirs(args.shots, exist_ok=True)
    r = xs.d.screen().root; g = r.get_geometry()
    raw = r.get_image(0, 0, g.width, g.height, 2, 0xffffffff)
    Image.frombytes("RGB", (g.width, g.height), raw.data, "raw", "BGRX").save(os.path.join(args.shots, name))

def pixels():
    r = xs.d.screen().root; g = r.get_geometry()
    raw = r.get_image(0, 0, g.width, g.height, 2, 0xffffffff)
    return Image.frombytes("RGB", (g.width, g.height), raw.data, "raw", "BGRX")

def type_text(s):
    """Type through whatever keymap the nested server inherited (e.g. '/' is Shift+7 on es)."""
    d = xs.d
    for ch in s:
        ks = XK.string_to_keysym(KEYS.get(ch, ch))
        found = None
        for kc in range(d.display.info.min_keycode, d.display.info.max_keycode + 1):
            for idx in (0, 1, 2, 3):
                if d.keycode_to_keysym(kc, idx) == ks:
                    found = (kc, idx); break
            if found: break
        assert found, f"no key produces {ch!r}"
        kc, idx = found
        mod = None
        if idx in (1, 3): mod = d.keysym_to_keycode(XK.string_to_keysym("Shift_L"))
        xs.ensure_focus()
        if idx >= 2: 
            l3 = d.keysym_to_keycode(XK.string_to_keysym("ISO_Level3_Shift"))
            xs._fake(X.KeyPress, l3)
        if mod: xs._fake(X.KeyPress, mod)
        xs._fake(X.KeyPress, kc); time.sleep(0.02); xs._fake(X.KeyRelease, kc)
        if mod: xs._fake(X.KeyRelease, mod)
        if idx >= 2: xs._fake(X.KeyRelease, l3)
        time.sleep(0.04)

def has_color(im, box, pred):
    x0, y0, x1, y1 = box
    px = im.crop(box).getdata()
    return any(pred(p) for p in px)

def find_launcher_window():
    end = time.time() + 20
    while time.time() < end:
        for w in xs.d.screen().root.query_tree().children:
            try:
                n = w.get_wm_name()
                if n and "MZM Recompiled" in str(n):
                    return w
            except Exception: pass
        time.sleep(0.2)
    return None

print("[launcher]")
w = find_launcher_window()
check(w is not None, "launcher window titled 'MZM Recompiled | Beta 3' appeared")
if w: check("Beta 3" in str(w.get_wm_name()), f"window title is {w.get_wm_name()!r}")
xs.focus_win = w; w.set_input_focus(X.RevertToParent, X.CurrentTime)
time.sleep(1.5)
shot("01-setup.png")
xs.click(550, 358); type_text(os.path.join(games, "mzm.gba"))
xs.click(550, 502); type_text(os.path.join(games, "bios.bin"))
time.sleep(0.6); shot("02-filled.png")
im = pixels()
# status lines are green when "Valid"
green = lambda p: p[1] > 150 and p[0] < 120 and p[2] < 160
check(has_color(im, (220, 385, 400, 405), green), "ROM shows Valid")
check(has_color(im, (220, 530, 400, 550), green), "BIOS shows Valid")
xs.click(438, 645); time.sleep(1.0); shot("03-home.png")
check(os.path.isfile(os.path.join(cfg, "MZMRecompiled", "launcher.ini")),
      "launcher.ini written under XDG_CONFIG_HOME/MZMRecompiled")
# PLAY button: centre of the hero card
im = pixels()
orange = lambda p: p[0] > 200 and 80 < p[1] < 140 and p[2] < 70
pts = [(x, y) for y in range(250, 560, 4) for x in range(300, 980, 6) if orange(im.getpixel((x, y)))]
check(bool(pts), "PLAY button visible on Home")
if pts:
    px = sum(p[0] for p in pts) // len(pts); py = sum(p[1] for p in pts) // len(pts)
    xs.click(px, py)
print("[game]")
gw = xs.game_window(timeout=60)
check(gw is not None, "game window opened after PLAY")
g = hd.Game.__new__(hd.Game); g.port = port
frames = []
def frame():
    import socket
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
            s.sendall(b'{"cmd":"frame"}\n'); return json.loads(s.makefile().readline())["frame"]
    except OSError: return None
end = time.time() + 60; f0 = None; ok = False
while time.time() < end:
    f = frame()
    if f is not None:
        f0 = f if f0 is None else f0
        if f - f0 >= 300: ok = True; break
    time.sleep(0.3)
check(ok, "guest runs (300+ frames after PLAY)")
time.sleep(2); shot("04-title.png")
im = pixels(); nonblack = sum(1 for p in im.resize((128, 80)).getdata() if max(p) > 24)
check(nonblack > 1500, f"a non-black picture is on screen ({nonblack}/10240 px)")
xs.key("Escape"); time.sleep(1); shot("05-esc-menu.png")
a = frame(); time.sleep(0.6); b = frame()
check(a == b, "ESC opens the Enhancements menu and pauses the guest")
xs.key("Escape"); time.sleep(0.8)
a = frame(); time.sleep(0.6); b = frame()
check(b is not None and a is not None and b > a, "ESC closes the menu and the guest resumes")
print("[privacy]")
ss = subprocess.run(["ss", "-H", "-tunap"], capture_output=True, text=True).stdout.splitlines()
mine = [l for l in ss if f"pid={proc.pid}," in l]
def loopback(addr): return addr.startswith("127.") or addr.startswith("[::1]") or addr.startswith("*") or addr.startswith("0.0.0.0")
remote = [l for l in mine if not loopback(l.split()[5]) ]
udp = [l for l in mine if l.startswith("udp")]
check(not remote and not udp, "no non-loopback network connection and no UDP socket while playing"
      + (f" ({remote[:2]})" if remote else ""))
print("  info  sockets owned by the process:", [" ".join(l.split()[:6]) for l in mine])
print("[crash]")
os.kill(proc.pid, 11)          # SIGSEGV: the last-gasp handler must leave a marker in latest.log
proc.wait(); time.sleep(0.8)

print("[files]")
logdir = os.path.join(state, "MZMRecompiled", "logs")
latest = os.path.join(logdir, "latest.log")
check(os.path.isfile(latest), "logs/latest.log exists under XDG_STATE_HOME")
log = open(latest, errors="replace").read() if os.path.isfile(latest) else ""
for needle in ["MZMRecompiled release=\"Beta 3\"", "build=", "gbarecomp=", "os=linux-x86_64",
               "rom_validation=ok", "bios_validation=ok", "strict_static=requested",
               "host_window: renderer=", "display="]:
    check(needle in log, f"latest.log contains {needle!r}")
check("MZM_BUILD" not in log, "no raw placeholders in log")
check("MZMRecompiled CRASH: fatal signal SIGSEGV" in log, "a crash (SIGSEGV sent to the process) leaves a marker in latest.log")
check(os.path.isfile(os.path.join(logdir, "mzm-recompiled.log")), "mzm-recompiled.log (event history) exists")
leak = [l for l in log.splitlines() if root in l or "/home/hikarilucy/proyectos" in l]
check(not leak, "latest.log does not mention the repo or the package directory" + (f" ({leak[:2]})" if leak else ""))
print("  info  config files:", sorted(os.listdir(os.path.join(cfg, "MZMRecompiled"))) if os.path.isdir(os.path.join(cfg, "MZMRecompiled")) else None)
print("  info  files beside the ROM:", sorted(os.listdir(games)))
print("  info  files beside the executable:", sorted(os.listdir(root)))
print("  info  files in $HOME:", sorted(os.listdir(home)))
print("  info  log head:"); print("\n".join("        " + l for l in log.splitlines()[:14]))
xs.stop()
print("RESULT:", "FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
