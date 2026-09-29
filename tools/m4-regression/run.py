#!/usr/bin/env python3
"""Run declarative MZM strict-static cases against a local native executable."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import tomllib

ROOT = Path(__file__).resolve().parents[2]
FIELDS = ("cpu_backend", "strict_static", "dispatch_misses", "interpreted_insns", "unmapped", "io_unhandled", "final_pc", "ppu_frames", "steps")
COUNTERS = {"dispatch_misses", "interpreted_insns", "unmapped", "io_unhandled", "ppu_frames", "steps"}
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]*$")


def read_metrics(log):
    values = {}
    for key in FIELDS:
        hits = re.findall(r"(?<![\w])" + key + r"=([^\s]+)", log)
        if hits:
            value = hits[-1]
            values[key] = int(value) if key in COUNTERS and value.isdecimal() else value
    return values


def run_case(case, args, output):
    name = case["name"]
    if not NAME.fullmatch(name):
        raise ValueError("invalid case name")
    if case.get("rom_target") != "usa":
        raise ValueError(f"{name}: unsupported ROM target")
    limits = [key for key in ("frames", "steps") if key in case]
    if len(limits) != 1 or not isinstance(case[limits[0]], int) or case[limits[0]] <= 0:
        raise ValueError(f"{name}: declare one positive frames or steps limit")
    config = Path(case.get("config", args.config))
    if not config.is_absolute():
        config = ROOT / config
    if not config.is_file():
        raise ValueError(f"{name}: config unavailable")
    if case.get("bios_required", True) is not True:
        raise ValueError(f"{name}: this MZM target requires a BIOS")
    checkpoint = args.checkpoint or case.get("checkpoint") or case.get("load_state")
    state = None
    if checkpoint:
        if not NAME.fullmatch(checkpoint):
            raise ValueError(f"{name}: invalid checkpoint name")
        state = Path(args.checkpoint_dir) / (checkpoint + ".state")
    log_path = output / "logs" / (name + ".log")
    artifact_dir = output / "artifacts" / name
    artifact_dir.mkdir(parents=True, exist_ok=True)
    row = {"case": name, "status": "FAIL", "exit_code": None, "duration": 0.0, "stable_final_pc": "final_pc" in case.get("expect", {})}
    if state and not state.is_file():
        log_path.write_text("checkpoint unavailable; no game process started\n")
        row["error"] = "checkpoint unavailable"
        return row
    command = [str(args.bin), "--rom", str(args.rom), "--bios", str(args.bios), "--config", str(config), "--" + limits[0], str(case[limits[0]]), "--save-path", str(artifact_dir / "runtime.sav")]
    if state:
        command.extend(("--load-state", str(state)))
    for item in case.get("artifacts", []):
        if item != "final.png":
            raise ValueError(f"{name}: unsupported artifact {item}")
        command.extend(("--dump-png", str(artifact_dir / item)))
    env = os.environ.copy()
    env["GBARECOMP_STRICT_STATIC"] = "1"
    start = time.monotonic()
    try:
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env, timeout=args.timeout)
        log = proc.stdout
        row["exit_code"] = proc.returncode
    except subprocess.TimeoutExpired as exc:
        log = (exc.stdout or b"").decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        log += "\nrunner timeout\n"
        row["error"] = "timeout"
    row["duration"] = round(time.monotonic() - start, 3)
    # Runtime diagnostic lines may echo local input paths; only the local log retains them.
    log_path.write_text(log)
    row.update(read_metrics(log))
    expected = case.get("expect", {})
    failures = []
    if row["exit_code"] != 0:
        failures.append("exit_code")
    for key, value in expected.items():
        if key not in FIELDS:
            raise ValueError(f"{name}: unknown expectation {key}")
        if row.get(key) != value:
            failures.append(key)
    for item in case.get("artifacts", []):
        if not (artifact_dir / item).is_file():
            failures.append(item)
    row["status"] = "PASS" if not failures else "FAIL"
    if failures:
        row["failed_checks"] = failures
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, default=ROOT / "tests/m4/cases.toml")
    parser.add_argument("--bin", type=Path, default=ROOT / "build-m1/MZMRecomp")
    parser.add_argument("--rom", type=Path, default=os.environ.get("MZM_ROM"))
    parser.add_argument("--bios", type=Path, default=os.environ.get("MZM_BIOS"))
    parser.add_argument("--config", type=Path, default=ROOT / "configs/mzm-us.toml")
    parser.add_argument("--checkpoint", help="local named .state checkpoint for selected cases")
    parser.add_argument("--checkpoint-dir", type=Path, default=ROOT / ".local/m4-checkpoints")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    for label, path in (("ROM", args.rom), ("BIOS", args.bios), ("binary", args.bin), ("config", args.config)):
        if path is None or not path.is_file():
            parser.error(f"{label} missing; provide its argument or environment variable")
    with args.cases.open("rb") as stream:
        cases = tomllib.load(stream)["case"]
    if not cases:
        parser.error("case collection is empty")
    output = args.output or ROOT / "dist/m4-regression" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output.mkdir(parents=True, exist_ok=False)
    (output / "logs").mkdir()
    (output / "artifacts").mkdir()
    rows = [run_case(case, args, output) for case in cases]
    summary = {"schema_version": 1, "generated_utc": datetime.now(timezone.utc).isoformat(), "cases": rows}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    lines = [f"{row['status']} {row['case']} exit={row['exit_code']} duration={row['duration']}s" + (f" failed={','.join(row.get('failed_checks', []))}" if row.get("failed_checks") else "") for row in rows]
    (output / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"report={output}")
    return 0 if all(row["status"] == "PASS" for row in rows) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyError, ValueError) as exc:
        print(f"invalid M4 regression case: {exc}", file=sys.stderr)
        sys.exit(2)
