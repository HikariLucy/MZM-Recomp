#!/usr/bin/env python3
"""Compare M4 report metrics by case name; regression returns exit code 1."""
import argparse
import json

METRICS = ("dispatch_misses", "interpreted_insns", "unmapped", "io_unhandled")


def compare(old, new):
    if old.get("case") != new.get("case"):
        return "NOT COMPARABLE", ["case differs"]
    if any(key not in old or key not in new for key in ("exit_code", *METRICS)):
        return "NOT COMPARABLE", ["required metric missing"]
    if old.get("stable_final_pc") != new.get("stable_final_pc"):
        return "NOT COMPARABLE", ["final_pc stability policy differs"]
    changes = []
    if old.get("status") == "PASS" and new.get("status") != "PASS":
        changes.append(("REGRESSION", "case status"))
    elif old.get("status") != "PASS" and new.get("status") == "PASS":
        changes.append(("IMPROVED", "case status"))
    if old["exit_code"] == 0 and new["exit_code"] != 0:
        changes.append(("REGRESSION", "exit status"))
    elif old["exit_code"] != 0 and new["exit_code"] == 0:
        changes.append(("IMPROVED", "exit status"))
    elif old["exit_code"] != new["exit_code"]:
        return "NOT COMPARABLE", ["nonzero exit status changed"]
    for key in METRICS:
        if new[key] > old[key]:
            changes.append(("REGRESSION", key))
        elif new[key] < old[key]:
            changes.append(("IMPROVED", key))
    if old.get("stable_final_pc") and old.get("final_pc") != new.get("final_pc"):
        changes.append(("REGRESSION", "stable final_pc"))
    if any(kind == "REGRESSION" for kind, _ in changes):
        return "REGRESSION", [item for _, item in changes]
    if changes:
        return "IMPROVED", [item for _, item in changes]
    return "UNCHANGED", []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    args = parser.parse_args()
    with open(args.baseline) as file:
        before = json.load(file)
    with open(args.candidate) as file:
        after = json.load(file)
    if before.get("schema_version") != after.get("schema_version"):
        print("NOT COMPARABLE: schema version differs")
        return 2
    old_cases = {row["case"]: row for row in before["cases"]}
    new_cases = {row["case"]: row for row in after["cases"]}
    regression = False
    for name in sorted(old_cases.keys() | new_cases.keys()):
        if name not in old_cases or name not in new_cases:
            status, details = "NOT COMPARABLE", ["case missing from one report"]
        else:
            status, details = compare(old_cases[name], new_cases[name])
        regression |= status == "REGRESSION"
        print(f"{status} {name}" + (": " + ", ".join(details) if details else ""))
    return 1 if regression else 0


if __name__ == "__main__":
    raise SystemExit(main())
