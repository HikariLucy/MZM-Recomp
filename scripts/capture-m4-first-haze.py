#!/usr/bin/env python3
"""Play MZM normally; save the first real haze hit at a clean frame boundary.

The MZM RAM hook only reports the hit. GBARecomp's existing --tcp-observe
savestate command performs the capture on the guest thread after the generated
call stack has unwound. Present-in-place is disabled for this capture session.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
LOCAL_CHECKPOINTS = ROOT / ".local/m4-checkpoints"
HIT = re.compile(
    r"^mzm_ram_dispatch kind=haze variant=(Haze_\w+) "
    r"runtime_pc=(0x[0-9a-fA-F]{8}) source_pc=(0x[0-9a-fA-F]{8}) "
    r"match=1 native=1 hits=1$"
)


def request_savestate(port: int, path: Path) -> dict:
    deadline = time.monotonic() + 10
    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=2)
            break
        except OSError:
            if time.monotonic() >= deadline:
                raise RuntimeError("observer TCP did not become available")
            time.sleep(0.1)
    with sock:
        sock.settimeout(30)
        sock.sendall(json.dumps({"cmd": "savestate_save", "path": str(path)}).encode() + b"\n")
        data = bytearray()
        while b"\n" not in data:
            part = sock.recv(65536)
            if not part:
                raise RuntimeError("observer TCP closed before replying")
            data.extend(part)
        return json.loads(bytes(data).split(b"\n", 1)[0])


def local_path(value: str) -> Path:
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    path = path.resolve()
    if not path.is_relative_to(LOCAL_CHECKPOINTS.resolve()):
        raise ValueError("checkpoint must be under .local/m4-checkpoints/")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, default=ROOT / "build-m4-haze/MZMRecomp")
    parser.add_argument("--rom", type=Path, default=os.getenv("MZM_ROM"))
    parser.add_argument("--bios", type=Path, default=os.getenv("MZM_BIOS"))
    parser.add_argument("--config", type=Path, default=ROOT / "configs/mzm-us.toml")
    parser.add_argument("--state", default=".local/m4-checkpoints/haze-bg3.state")
    parser.add_argument("--expect-variant", default="Haze_Bg3")
    parser.add_argument("--save-path", default=".local/m4-checkpoints/haze-session.sav")
    parser.add_argument("--initial-save", type=Path,
                        help="copy an existing personal SRAM save into the isolated local session")
    parser.add_argument("--port", type=int, default=19844)
    args = parser.parse_args()

    try:
        state = local_path(args.state)
        save_path = local_path(args.save_path)
    except ValueError as exc:
        parser.error(str(exc))
    if state.exists():
        parser.error("checkpoint already exists; choose another --state name")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be 1..65535")
    if not re.fullmatch(r"Haze_[A-Za-z0-9]+", args.expect_variant):
        parser.error("invalid --expect-variant")
    for name, path in (("binary", args.bin), ("ROM", args.rom),
                       ("BIOS", args.bios), ("config", args.config)):
        if path is None or not path.is_file():
            parser.error(f"{name} missing")
    LOCAL_CHECKPOINTS.mkdir(parents=True, exist_ok=True)
    if args.initial_save:
        if not args.initial_save.is_file() or save_path.exists():
            parser.error("--initial-save must exist and destination must be unused")
        shutil.copyfile(args.initial_save, save_path)

    env = os.environ.copy()
    env["GBARECOMP_STRICT_STATIC"] = "1"
    env["GBARECOMP_PRESENT_IN_PLACE"] = "0"
    env["MZM_TRACE_RAM_DISPATCH"] = "1"
    env["MZM_M4_CAPTURE_FIRST_HAZE"] = str(state)
    env.pop("MZM_DISABLE_HAZE_RAM_DISPATCH", None)
    command = [str(args.bin), "--rom", str(args.rom), "--bios", str(args.bios),
               "--config", str(args.config), "--window", "--tcp-observe",
               str(args.port), "--save-path", str(save_path)]
    print("Play to the first haze scene; capture waits for a clean frame boundary.", flush=True)
    process = subprocess.Popen(command, cwd=ROOT, env=env, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               bufsize=1)
    result = {"hit": None, "captured": False, "error": None}
    capture_thread = None

    def capture(hit):
        try:
            reply = request_savestate(args.port, state)
            if not reply.get("ok") or not state.is_file() or state.stat().st_size == 0:
                raise RuntimeError(f"savestate request failed: {reply}")
            result["captured"] = True
            print(f"CAPTURED variant={hit.group(1)} runtime_pc={hit.group(2)} "
                  f"source_pc={hit.group(3)} checkpoint={state}", flush=True)
            print("You can close the game window when ready.", flush=True)
        except (OSError, ValueError, RuntimeError) as exc:
            result["error"] = str(exc)
            print(f"CAPTURE FAILED: {exc}", file=sys.stderr, flush=True)

    try:
        for line in process.stdout:
            print(line, end="", flush=True)
            hit = HIT.fullmatch(line.rstrip("\r\n"))
            if hit and hit.group(1) != args.expect_variant:
                print(f"Observed {hit.group(1)}; waiting for {args.expect_variant}.", flush=True)
            if hit and hit.group(1) == args.expect_variant and result["hit"] is None:
                result["hit"] = hit
                capture_thread = threading.Thread(target=capture, args=(hit,), daemon=True)
                capture_thread.start()
        process.wait()
        if capture_thread:
            capture_thread.join(timeout=35)
    except KeyboardInterrupt:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    if result["error"]:
        return 2
    if not result["hit"]:
        print("No haze dispatch was observed; no checkpoint was written.", file=sys.stderr)
        return 2
    if result["hit"] and not result["captured"]:
        print("Haze was reached but no checkpoint was captured.", file=sys.stderr)
        return 2
    return process.returncode or 0


if __name__ == "__main__":
    sys.exit(main())
