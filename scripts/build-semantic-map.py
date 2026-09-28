#!/usr/bin/env python3
"""Build a conservative MZM semantic symbol map from a matching decomp ELF.

The output is metadata only. It does not copy ROM bytes or extracted assets.

ISA state is derived from ARM ELF mapping symbols when available:
  $a -> ARM code
  $t -> Thumb code
  $d -> data
If no mapping symbol can be established for a symbol, ISA is UNKNOWN.
"""

from __future__ import annotations

import argparse
import csv
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass


@dataclass
class Mapping:
    address: int
    state: str


@dataclass
class Symbol:
    address: int
    size: int
    kind: str
    name: str


def run(*args: str) -> str:
    result = subprocess.run(args, check=True, text=True, capture_output=True)
    return result.stdout


def read_mapping_symbols(elf: pathlib.Path) -> list[Mapping]:
    text = run("arm-none-eabi-readelf", "-sW", str(elf))
    mappings: list[Mapping] = []

    # Example names may be $a, $t, $d or suffixed forms such as $t.1.
    rx = re.compile(
        r"^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$"
    )
    for line in text.splitlines():
        m = rx.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        name = m.group(2).strip()
        if name.startswith("$a"):
            mappings.append(Mapping(addr, "ARM"))
        elif name.startswith("$t"):
            mappings.append(Mapping(addr, "THUMB"))
        elif name.startswith("$d"):
            mappings.append(Mapping(addr, "DATA"))

    mappings.sort(key=lambda x: x.address)
    return mappings


def read_symbols(elf: pathlib.Path) -> list[Symbol]:
    text = run(
        "arm-none-eabi-nm",
        "-n",
        "-S",
        "--defined-only",
        str(elf),
    )
    out: list[Symbol] = []
    rx = re.compile(
        r"^([0-9a-fA-F]+)(?:\s+([0-9a-fA-F]+))?\s+([A-Za-z])\s+(.+)$"
    )
    for line in text.splitlines():
        m = rx.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        size = int(m.group(2), 16) if m.group(2) else 0
        kind = m.group(3)
        name = m.group(4)
        if 0x08000000 <= addr < 0x0A000000:
            out.append(Symbol(addr, size, kind, name))
    return out


def isa_at(address: int, mappings: list[Mapping]) -> str:
    state = "UNKNOWN"
    for item in mappings:
        if item.address > address:
            break
        state = item.state
    return state


def load_map_objects(map_path: pathlib.Path) -> list[tuple[int, int, str]]:
    """Return (start, end, object path) ranges parsed conservatively from GNU ld map."""
    ranges: list[tuple[int, int, str]] = []
    rx = re.compile(
        r"^\s*\.text\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+?\.o)(?:\s|$)"
    )
    for line in map_path.read_text(errors="replace").splitlines():
        m = rx.match(line)
        if not m:
            continue
        start = int(m.group(1), 16)
        size = int(m.group(2), 16)
        ranges.append((start, start + size, m.group(3).strip()))
    return ranges


def object_for(address: int, ranges: list[tuple[int, int, str]]) -> str:
    for start, end, obj in ranges:
        if start <= address < end:
            return obj
    return ""


def source_for(obj: str, mzm_root: pathlib.Path) -> str:
    if not obj:
        return ""
    p = pathlib.Path(obj)
    candidates = []
    if p.suffix == ".o":
        candidates.extend([p.with_suffix(".c"), p.with_suffix(".s")])
    for candidate in candidates:
        if (mzm_root / candidate).exists():
            return candidate.as_posix()
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True, type=pathlib.Path)
    ap.add_argument("--map", required=True, dest="map_file", type=pathlib.Path)
    ap.add_argument("--mzm-root", required=True, type=pathlib.Path)
    ap.add_argument("--output", required=True, type=pathlib.Path)
    args = ap.parse_args()

    mappings = read_mapping_symbols(args.elf)
    symbols = read_symbols(args.elf)
    objects = load_map_objects(args.map_file)

    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            ["address", "size", "isa", "symbol_type", "symbol", "object", "source"]
        )
        for sym in symbols:
            obj = object_for(sym.address, objects)
            writer.writerow(
                [
                    f"0x{sym.address:08X}",
                    f"0x{sym.size:X}",
                    isa_at(sym.address, mappings),
                    sym.kind,
                    sym.name,
                    obj,
                    source_for(obj, args.mzm_root),
                ]
            )

    print(f"symbols={len(symbols)}")
    print(f"mapping_symbols={len(mappings)}")
    print(f"output={args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
