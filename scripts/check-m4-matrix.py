#!/usr/bin/env python3
"""Validate docs/M4-COMPATIBILITY-MATRIX.md and print the status counts.

The first Markdown table of the file is the matrix. Every data row must have
exactly the six header columns, start and end with `|`, and carry a known
status (`PASS|PARTIAL|BLOCKED|UNVERIFIED`, optionally followed by a
parenthesised qualifier such as `PASS (route-scoped)`). Duplicate
(area, feature) keys fail. If the document states "Current row counts:
**N total ...**", the stated numbers must equal the computed ones.

Exit status 0 = valid, 1 = invalid, 2 = usage error.
"""
import argparse
import re
import sys
from collections import Counter
from pathlib import Path

STATUSES = ("PASS", "PARTIAL", "BLOCKED", "UNVERIFIED")
HEADER = ["Area", "Feature / Route", "Status", "Evidence", "Automation",
          "Remaining Risk"]
STATUS_RE = re.compile(r"^(PASS|PARTIAL|BLOCKED|UNVERIFIED)(?: \(([^()]+)\))?$")
STATED_RE = re.compile(
    r"Current row counts:\s*\*\*(\d+) total\s*[—-]\s*(\d+) PASS,\s*(\d+) PARTIAL,"
    r"\s*(\d+) BLOCKED,\s*(\d+) UNVERIFIED\*\*")


def split_row(line):
    """Split on unescaped pipes outside backtick code spans."""
    cells, cur, in_code, i = [], [], False, 0
    while i < len(line):
        c = line[i]
        if c == "`":
            in_code = not in_code
        if c == "\\" and i + 1 < len(line) and line[i + 1] == "|":
            cur.append("|")
            i += 2
            continue
        if c == "|" and not in_code:
            cells.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    cells.append("".join(cur).strip())
    return cells


def parse(text):
    """Return (rows, errors, stated). rows: list of (lineno, cells)."""
    errors, rows, in_table, seen_table = [], [], False, False
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        if not line.startswith("|"):
            if in_table:
                in_table, seen_table = False, True
            continue
        if seen_table:
            continue  # only the first table is the matrix
        if not line.endswith("|") or len(line) < 2:
            errors.append(f"line {n}: row does not end with '|'")
            line = line + "|"
        cells = split_row(line)[1:-1]
        if not in_table:
            in_table = True
            if cells != HEADER:
                errors.append(f"line {n}: unexpected header {cells}")
            continue
        if all(re.fullmatch(r":?-{3,}:?", c) for c in cells):
            continue
        if len(cells) != len(HEADER):
            errors.append(f"line {n}: {len(cells)} columns, expected {len(HEADER)}")
            continue
        rows.append((n, cells))
    m = STATED_RE.search(text)
    stated = tuple(int(x) for x in m.groups()) if m else None
    return rows, errors, stated


def check(text):
    rows, errors, stated = parse(text)
    counts, keys = Counter(), {}
    for n, cells in rows:
        for idx, name in enumerate(HEADER):
            if not cells[idx]:
                errors.append(f"line {n}: empty '{name}' cell")
        m = STATUS_RE.match(cells[2])
        if not m:
            errors.append(f"line {n}: unknown status {cells[2]!r}")
            continue
        counts[m.group(1)] += 1
        key = (cells[0], cells[1])
        if key in keys:
            errors.append(f"line {n}: duplicate row {key} (first at line {keys[key]})")
        keys.setdefault(key, n)
    total = sum(counts.values())
    if stated is None:
        errors.append("no 'Current row counts: **N total ...**' statement found")
    elif stated != (total, counts["PASS"], counts["PARTIAL"],
                    counts["BLOCKED"], counts["UNVERIFIED"]):
        errors.append(
            "stated counts %s != computed (total, PASS, PARTIAL, BLOCKED, "
            "UNVERIFIED) = %s" % (stated, (total, counts["PASS"], counts["PARTIAL"],
                                           counts["BLOCKED"], counts["UNVERIFIED"])))
    if not rows:
        errors.append("matrix table has no data rows")
    return total, counts, errors, rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("path", nargs="?",
                    default=str(Path(__file__).resolve().parent.parent
                                / "docs" / "M4-COMPATIBILITY-MATRIX.md"))
    ap.add_argument("--list", metavar="STATUS", choices=STATUSES,
                    help="also list the rows with this status")
    args = ap.parse_args(argv)
    try:
        text = Path(args.path).read_text(encoding="utf-8")
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    total, counts, errors, rows = check(text)
    print(f"TOTAL {total}")
    for s in STATUSES:
        print(f"{s} {counts[s]}")
    if args.list:
        for n, c in rows:
            if c[2].startswith(args.list):
                print(f"  line {n}: {c[0]} | {c[1]}")
    for e in errors:
        print(f"INVALID: {e}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
