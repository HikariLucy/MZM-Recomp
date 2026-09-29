#!/usr/bin/env python3
"""
Derives the per-Part code/data map of the NES emulator from a LOCALLY built
nested decomp ELF (nes_metroid/emulator, built out-of-tree; see
docs/M4-NES-METROID.md). Only addresses, modes and sizes are emitted -- no
ROM-derived bytes. The ELF is used purely as a symbol oracle: every Part's
bytes are separately reconstructed from the legal ROM by
extract-nes-emulator.py and checked against the guest.

Output (stdout): TOML fragment with one [[data_range]] per data run and one
[[extra_func]] per function symbol inside a code run, plus a C++ table of the
code runs used by the runtime byte gate.

Usage: derive-nes-emulator-map.py path/to/mzm_us.elf [--format toml|cpp]
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

PARTS = [
    # name, section, load address, size, SHA-256 of the extracted image
    ("part1", ".vram_text_1", 0x06006000, 0x1240,
     "eff34fbc387676a159dce57aff8089a2d6e355eaf96ecdef32155422c14da6d5"),
    ("part2", ".iwram_text", 0x03000000, 0x5A4C,
     "15df40ee533211143aae8cd3deb6062f9d0364135479b0c361e9a24ca6ff0fb2"),
    ("part3", ".vram_text_2", 0x0600B000, 0x150,
     "c9978bfe63c71e714f5b95c1324abcbddfd1a613a74a44c84f158438f421c3d9"),
    ("part4", ".vram_text_3", 0x0600C000, 0x60,
     "201cd71270e85933f208ae434bb0a446d3e5b75471da7a120dcf349ef2779f31"),
    ("part5", ".vram_text_4", 0x0600E000, 0xD88,
     "027930d0edc399cc93acce1a21e15be36e34a69b51ceb803686d2ad6c580fee0"),
    ("part6", ".ewram_text", 0x0203E000, 0x8E0,
     "d3c8c872d123dea0cc39c304a959547257351eb2687d9c1c9b85d5d422938514"),
]
# The local toolchain lays out the tail of Part 6 differently from the ROM
# (an extra ARM veneer, different libgcc _call_via_rX placement; 46 differing
# bytes, all at/after 0x0203E772 as BL displacements or in the tail). In the
# ROM (checked against the extracted Part 6): 0x0203E888..0x0203E8A4 is data
# (sSramVersion + write-function pointer table) and 0x0203E8A4..0x0203E8E0 is
# fifteen 4-byte Thumb `bx rN; nop` thunks. Symbols at/after 0x0203E888 are
# therefore taken from this table, not from the ELF.
PART6_TAIL_DATA = (0x0203E888, 0x0203E8A4)
PART6_THUNKS = (0x0203E8A4, 0x0203E8E0)


# Function seeds deliberately NOT emitted because the pinned generator cannot
# yet translate them without a hard data_range collision (see the config
# header and docs/M4-NES-METROID.md). They stay as documented uncovered
# strict-static frontiers, never as generated code over data.
SKIPPED_SEEDS = {
    0x0600E474: "ldrne pc,[pc,r1,lsl #2] jump-table idiom; beq fall-through "
                "edge enters table bytes 0x0600E4C0..0x0600E4D0",
}


# Runs of an image that are PROVEN mutable data by runtime evidence
# (tests/m4/nes_emulator_frontier_test.cpp mutability audit). Everything else
# in an image -- code and literal pools, which generated code bakes in as
# constants -- is byte-gated. Filled only from observed guest diffs.
MUTABLE_DATA = {
    # sub_06006000 (font setup, Part 1 asm) does `strh r6,[0x06006700]` = 0x0182:
    # a BG screenblock-12 tilemap entry that overlays Part 1's own code
    # (`sub r5,r5,#32` at 0x06006700). Straight-line init in sub_06006558 has
    # already executed that instruction before the font-setup call, and the
    # overwrite is observed at boot; excluded so yield/resume re-entry into the
    # function keeps verifying. Evidence: nes_emulator_frontier_test mutability audit.
    "part1": [(0x06006700, 0x06006702)],
}

# Parts with no AOT corpus are identified by their code runs only: their
# mutable variables (.data, in-text words) are not fully characterised and no
# generated code bakes their literals.
GATE_CODE_ONLY = {"part2"}


def readelf_symbols(elf):
    out = subprocess.check_output(["arm-none-eabi-readelf", "-sW", elf], text=True)
    syms = []
    for line in out.splitlines():
        f = line.split()
        if len(f) < 8 or not re.match(r"^\d+:$", f[0]):
            continue
        syms.append({"addr": int(f[1], 16), "size": int(f[2]), "type": f[3],
                     "bind": f[4], "ndx": f[6], "name": f[7] if len(f) > 7 else ""})
    return syms


def section_indices(elf):
    out = subprocess.check_output(["arm-none-eabi-readelf", "-SW", elf], text=True)
    idx = {}
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*(\d+)\]\s+(\S+)", line)
        if m:
            idx[m.group(2)] = m.group(1)
    return idx


def part_map(syms, ndx, base, size):
    maps = sorted((s["addr"], s["name"]) for s in syms
                  if s["ndx"] == ndx and s["name"] in ("$a", "$t", "$d"))
    runs = []
    for i, (a, kind) in enumerate(maps):
        end = maps[i + 1][0] if i + 1 < len(maps) else base + size
        if end > a:
            runs.append([a, end, kind])
    # Merge adjacent same-kind runs.
    merged = []
    for r in runs:
        if merged and merged[-1][2] == r[2] and merged[-1][1] == r[0]:
            merged[-1][1] = r[1]
        else:
            merged.append(r)
    return merged


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--format", choices=["toml", "cpp"], default="toml")
    ap.add_argument("--parts-dir", default=".local/nes-emulator",
                    help="directory with extract-nes-emulator.py output (cpp format)")
    ap.add_argument("--parts", default="part1,part2,part3,part4,part5,part6",
                    help="comma-separated Parts to emit")
    args = ap.parse_args()
    wanted = set(args.parts.split(","))
    syms = readelf_symbols(args.elf)
    idx = section_indices(args.elf)
    result = []
    for name, sec, base, size, sha in PARTS:
        if name not in wanted:
            continue
        runs = part_map(syms, idx[sec], base, size)
        end = base + size
        if name == "part6":
            # Clip the toolchain-specific tail; append the ROM thunk run.
            runs = [[a, min(b, PART6_TAIL_DATA[0]), k] for a, b, k in runs
                    if a < PART6_TAIL_DATA[0]]
            runs.append([PART6_TAIL_DATA[0], PART6_TAIL_DATA[1], "$d"])
            runs.append([PART6_THUNKS[0], PART6_THUNKS[1], "$t"])
        funcs = sorted({s["addr"] & ~1 for s in syms
                        if s["ndx"] == idx[sec] and s["type"] == "FUNC"
                        and (name != "part6" or (s["addr"] & ~1) < PART6_TAIL_DATA[0])})
        if name == "part6":
            funcs += list(range(PART6_THUNKS[0], PART6_THUNKS[1], 4))
        # Keep only function starts that fall in a code run; record their mode.
        seeds = []
        for a in funcs:
            for ra, rb, k in runs:
                if ra <= a < rb and k in ("$a", "$t"):
                    seeds.append((a, "thumb" if k == "$t" else "arm"))
                    break
        result.append((name, base, size, runs, seeds, sha))

    if args.format == "toml":
        for name, base, size, runs, seeds, sha in result:
            print(f"[[executable_image]]\nid = \"nes_{name}\"\n"
                  f"path = \".local/nes-emulator/{name}.bin\"\n"
                  f"load_address = 0x{base:08X}\nsize = 0x{size:X}\n"
                  f"sha256 = \"{sha}\"\nnote = \"NES emulator {name}\"\n")
        for name, base, size, runs, seeds, sha in result:
            print(f"# ---- {name}: 0x{base:08X}..0x{base+size:08X}")
            for a, b, k in runs:
                if k == "$d":
                    print(f"[[data_range]]\nstart = 0x{a:08X}\nend = 0x{b:08X}\n"
                          f'note = "NES {name} data/literals"\n')
            for a, mode in seeds:
                if a in SKIPPED_SEEDS:
                    print(f"# NOT SEEDED 0x{a:08X} {mode}: {SKIPPED_SEEDS[a]}\n")
                    continue
                print(f"[[extra_func]]\naddr = 0x{a:08X}\nmode = \"{mode}\"\n"
                      f'name = "nes_{name}_{a:08x}"\ndispatch = false\n')
    else:
        print("// Generated by scripts/derive-nes-emulator-map.py --format cpp.")
        print("// Addresses, modes and SHA-256 digests only; no ROM-derived bytes.")
        print("// gate runs = whole image minus runtime-proven mutable data; the gate")
        print("// SHA-256 covers those runs (code + literal pools baked into generated code).")
        print("#pragma once\n#include <cstddef>\n#include <cstdint>\n")
        print("namespace mzm_nes_emulator {\n")
        print("struct CodeRun { std::uint32_t start; std::uint32_t end; };\n")
        for name, base, size, runs, seeds, sha in result:
            print(f"// Code runs from the decomp mapping symbols (test classification only).")
            print(f"inline constexpr CodeRun k_{name}_code[] = {{")
            for a, b, k in runs:
                if k != "$d":
                    print(f"    {{0x{a:08X}u, 0x{b:08X}u}},")
            print("};")
            if name in GATE_CODE_ONLY:
                gate = [(a, b) for a, b, k in runs if k != "$d"]
            else:
                cuts = sorted(MUTABLE_DATA.get(name, []))
                gate, cur = [], base
                for ca, cb in cuts:
                    if ca > cur:
                        gate.append((cur, ca))
                    cur = max(cur, cb)
                if cur < base + size:
                    gate.append((cur, base + size))
            path = os.path.join(args.parts_dir, f"{name}.bin")
            with open(path, "rb") as f:
                img = f.read()
            assert len(img) == size, (name, len(img), size)
            assert hashlib.sha256(img).hexdigest() == sha, name
            h = hashlib.sha256()
            for a, b in gate:
                h.update(img[a - base:b - base])
            print(f"inline constexpr CodeRun k_{name}_gate[] = {{")
            for a, b in gate:
                print(f"    {{0x{a:08X}u, 0x{b:08X}u}},")
            print("};")
            print(f'inline constexpr const char* k_{name}_gate_sha256 =\n    "{h.hexdigest()}";\n')
        print("}  // namespace mzm_nes_emulator")

if __name__ == "__main__":
    sys.exit(main())
