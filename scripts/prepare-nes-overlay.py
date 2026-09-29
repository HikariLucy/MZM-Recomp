#!/usr/bin/env python3
"""Split the imported NES blob range at reviewed ROM instruction islands."""

import argparse
from pathlib import Path


OLD = """[[data_range]]
start = 0x087D8000
end = 0x087F7734
note = "non-executable (sections)"""

NEW = """[[data_range]]
start = 0x087D8004
end = 0x087D80D4
note = "NES data before loader"

[[data_range]]
start = 0x087D8114
end = 0x087D8124
note = "NES loader literal pool"

[[data_range]]
start = 0x087D8140
end = 0x087F7734
note = "NES data after reset stub"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("imported", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.imported.read_text()
    identity = 'sha1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8"'
    if identity not in source or source.count(OLD) != 1:
        parser.error("unexpected USA identity or imported NES data range")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source.replace(OLD, NEW, 1))


if __name__ == "__main__":
    main()
