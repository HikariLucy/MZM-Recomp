#!/usr/bin/env python3
"""
Qualify the launcher's Support / Report Problem page through the shipped MZMRecomp binary, in a
private nested X server (Xephyr). Nothing real is opened: MZM_SUPPORT_TEST_DIR makes the module record
URL/folder/file opens in opener.txt and mirror every clipboard write (read back through SDL) in
clipboard.txt. No browser, file manager or network is touched.

  A  Home -> Support navigation; the crash banner for a seeded previous crash marker
  B  Copy Bug Report / Diagnostic Info / Build Info / Log Path: content, privacy, path redaction
  C  Open Log Folder / latest.log / Feedback Guide / Report Issue on GitHub (recorded, not launched);
     the issue URL carries no diagnostics
  D  problem-area selection feeds the report; Back returns to Home
  E  logs: previous.log + last-crash.log kept, previous_session= recorded; a second run sees an
     unexpected end; no TCP/UDP sockets at all

usage: run-support-module.py <MZMRecomp> <rom> <bios>     exit 77 (ctest SKIP) without Xephyr/xlib/Pillow
"""
import importlib.util, os, shutil, subprocess, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))
try:
    spec = importlib.util.spec_from_file_location("hd", os.path.join(here, "run-host-display.py"))
    hd = importlib.util.module_from_spec(spec); sys.modules["hd"] = hd; spec.loader.exec_module(hd)
    from PIL import Image
except SystemExit:
    sys.exit(77)
except Exception as e:                      # missing xlib / Pillow
    print("SKIP:", e); sys.exit(77)

binary, rom, bios = sys.argv[1:4]
failures = []
def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond: failures.append(msg)

base = tempfile.mkdtemp(prefix="mzm-support-")
home = os.path.join(base, "alice-home")            # a recognisable fake home: must never leak
tdir = os.path.join(base, "t"); os.makedirs(tdir)
logs = os.path.join(home, ".local/state/MZMRecompiled/logs"); os.makedirs(logs)
cfg = os.path.join(home, ".config/MZMRecompiled"); os.makedirs(cfg)
with open(os.path.join(cfg, "launcher.ini"), "w") as f: f.write(f"rom={rom}\nbios={bios}\n")
with open(os.path.join(logs, "latest.log"), "w") as f:
    f.write("MZMRecompiled release=old\n[mzm] t start\nMZMRecompiled CRASH: fatal signal SIGSEGV (seeded)\n")

xs = hd.Xserver(1280, 800)
subprocess.run(["setxkbmap", "-display", xs.name, "-layout", "us", "-variant", ""], check=False)
proc = None
def cleanup():
    if proc and proc.poll() is None: proc.kill(); proc.wait()
    try: xs.stop()
    except Exception: pass
    shutil.rmtree(base, ignore_errors=True)
import atexit; atexit.register(cleanup)

def launch():
    env = {k: v for k, v in os.environ.items() if not k.startswith(("GBARECOMP_", "XDG_"))}
    env.update(DISPLAY=xs.name, HOME=home, SDL_AUDIODRIVER="dummy", MZM_SUPPORT_TEST_DIR=tdir)
    p = subprocess.Popen([binary], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    import threading
    threading.Thread(target=lambda: [None for _ in p.stdout], daemon=True).start()
    return p

def find_window():
    end = time.time() + 20
    while time.time() < end:
        for w in xs.d.screen().root.query_tree().children:
            try:
                if "MZM Recompiled" in str(w.get_wm_name()): return w
            except Exception: pass
        time.sleep(0.2)

def pixels():
    r = xs.d.screen().root; g = r.get_geometry()
    raw = r.get_image(0, 0, g.width, g.height, 2, 0xffffffff)
    return Image.frombytes("RGB", (g.width, g.height), raw.data, "raw", "BGRX")

def click(x, y):
    for _ in range(30):
        if xs.click(x, y): time.sleep(0.4); return True
        time.sleep(0.2)
    return False

orange = lambda p: p[0] > 200 and 100 < p[1] < 150 and p[2] < 60
def orange_at(x, y):
    im = pixels(); return any(orange(im.getpixel((x + dx, y + dy))) for dx in range(-60, 60, 6) for dy in range(-12, 12, 4))

def read(name):
    try: return open(os.path.join(tdir, name), errors="replace").read()
    except OSError: return ""

NAV_SUPPORT, REPORT, BUG, DIAG = (815, 638), (357, 460), (630, 460), (903, 460)
LOGDIR, LATEST, LOGPATH = (357, 510), (630, 510), (903, 510)
FEED, BUILDI, COMBO, BACK = (357, 558), (630, 558), (370, 412), (291, 636)
BUILDINFO = (630, 558)

print("[run 1: seeded crash marker]")
proc = launch()
w = find_window()
check(w is not None, "launcher window appeared")
xs.focus_win = w; w.set_input_focus(hd.X.RevertToParent, hd.X.CurrentTime)
time.sleep(1.5)
check(orange_at(640, 525), "Home page shown (PLAY visible)")
check(click(*NAV_SUPPORT), "clicked Support on Home")
time.sleep(1.0)
check(orange_at(*REPORT), "Support page shown (Report Issue on GitHub button)")

print("[B copy actions]")
click(*BUG); bug = read("clipboard.txt")
check("## MZM Recompiled bug report" in bug and "### Steps to reproduce" in bug and "Do not upload ROM or BIOS" in bug,
      "Copy Bug Report puts the template on the clipboard")
click(*DIAG); diag = read("clipboard.txt")
check("MZM Recompiled Diagnostic Report" in diag and "MZM SHA:" in diag and "GBARecomp SHA:" in diag, "Copy Diagnostic Info: build identity")
check("A previous crash was detected" in diag, "diagnostics report the previous crash marker")
check("ROM validation: configured, valid" in diag and "BIOS validation: configured, valid" in diag, "ROM/BIOS reported as validation results only")
for label, text in (("bug report", bug), ("diagnostics", diag)):
    check("alice-home" not in text and rom not in text and bios not in text and "Metroid" not in text,
          f"{label}: no home directory, ROM or BIOS path")
check("Log path: ~/.local/state/MZMRecompiled/logs/latest.log" in diag, "log path is redacted to ~")
check("Config path:" in diag and "config.ini" in diag and "alice-home" not in diag, "config path reported without the home directory")
check("OS:" in diag and "GPU:" in diag and "Display refresh:" in diag and "Gamepad:" in diag, "hardware fields present (values may be Unknown)")
click(*BUILDI); bi = read("clipboard.txt")
check(bi.startswith("MZM Recompiled") and "MZM SHA:" in bi and "recomp-ui SHA:" in bi, "Copy Build Info")
click(*LOGPATH); lp = read("clipboard.txt").strip()
check(lp == "~/.local/state/MZMRecompiled/logs/latest.log", f"Copy Log Path is redacted ({lp!r})")

print("[C open actions]")
open(os.path.join(tdir, "opener.txt"), "w").close()
click(*LOGDIR); click(*LATEST); click(*FEED)
op = read("opener.txt")
check(f"folder\t{logs}" in op, "Open Log Folder resolves to the real log directory")
check(f"file\t{os.path.join(logs, 'latest.log')}" in op, "Open latest.log resolves to latest.log")
check("FEEDBACK.md" not in op, "Open Feedback Guide: no file beside this dev binary -> nothing opened, no crash")
click(*REPORT); op = read("opener.txt")
url = [l.split("\t", 1)[1] for l in op.splitlines() if l.startswith("url\t")]
check(url == ["https://github.com/HikariLucy/MZM-Recomp/issues/new?template=bug_report.yml&title=%5BBeta%5D%20&labels=bug%2Cbeta"],
      f"Report Issue opens only the new-issue URL ({url})")
check("## MZM Recompiled bug report" in read("clipboard.txt"), "Report Issue leaves the bug report on the clipboard")

print("[D problem area, back]")
click(*COMBO); time.sleep(0.5)
click(353, 454); time.sleep(0.5)          # "Audio" in the popup (it opens scrolled to the current item)
click(*BUG)
check("### Problem area\nAudio" in read("clipboard.txt"), "selected problem area (Audio) feeds the report")
click(*BACK); time.sleep(0.8)
check(orange_at(640, 525), "Back to Home works (PLAY visible)")
check(proc.poll() is None, "launcher still running")
socks = [l for l in subprocess.run(["ss", "-H", "-tunap"], capture_output=True, text=True).stdout.splitlines() if f"pid={proc.pid}," in l]
check(not socks, "the launcher holds no TCP/UDP socket")
proc.kill(); proc.wait()
time.sleep(0.3)

print("[E logs]")
prev = os.path.join(logs, "previous.log"); crash = os.path.join(logs, "last-crash.log")
check(os.path.isfile(prev) and "CRASH" in open(prev).read(), "previous.log keeps the old run")
check(os.path.isfile(crash) and "seeded" in open(crash).read(), "last-crash.log keeps the crashed run")
lt = open(os.path.join(logs, "latest.log"), errors="replace").read()
check("previous_session=crash-marker" in lt, "latest.log records previous_session=crash-marker")
check("alice-home" not in lt, "latest.log has no home directory")

print("[run 2: previous run was killed]")
proc = launch(); w = find_window(); xs.focus_win = w; w.set_input_focus(hd.X.RevertToParent, hd.X.CurrentTime)
time.sleep(1.5); click(*NAV_SUPPORT); time.sleep(1.0); click(*DIAG)
check("may have ended unexpectedly" in read("clipboard.txt"), "a killed previous run is reported as an unexpected end")
check(os.path.isfile(crash) and "previous_session=crash-marker" in open(crash).read(),
      "last-crash.log now holds the most recent bad run (the one killed in run 1)")
proc.kill(); proc.wait()
print("RESULT:", "FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
