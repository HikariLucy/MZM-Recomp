#!/usr/bin/env python3
"""
MAINTAINER VERIFICATION TOOL -- not part of the normal build.

The build consumes the versioned canonical map configs/nes-emulator-map-us.toml
(see scripts/generate-nes-emulator-config.py). This tool re-derives the same
structural metadata from a LOCALLY built nested decomp ELF (nes_metroid/emulator,
built out-of-tree; see docs/M4-NES-METROID.md) and checks it against the
canonical map. The ELF is a structure/symbol oracle, not an authority: the ROM
remains the authority for every executed byte, and each Part's bytes are
reconstructed from the legal ROM by extract-nes-emulator.py and hash-checked.
Only addresses, modes and sizes are read/emitted -- no ROM-derived bytes.

Usage:
  derive-nes-emulator-map.py --elf nested.elf --compare configs/nes-emulator-map-us.toml
  derive-nes-emulator-map.py --elf nested.elf --emit-candidate .local/nes-map-candidate.toml

The canonical map is never overwritten unless --emit-candidate is pointed at
it explicitly. Mutable ranges are runtime evidence (not ELF-derived), so they
are emitted from the table below but ignored by --compare.
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


# Function seeds deliberately NOT emitted (none at the moment). Before the
# conditional-PC-load CFG fix, Part 5 0x0600E474 had to live here.
SKIPPED_SEEDS = {}

# Runs of an image that are PROVEN mutable data by runtime evidence
# (tests/m4/nes_emulator_frontier_test.cpp mutability audit) or by the linker
# map (writable .data). Everything else in an image -- code and literal pools,
# which generated code bakes in as constants -- is byte-gated.
MUTABLE_DATA = {
    # sub_06006000 (font setup, Part 1 asm) does `strh r6,[0x06006700]` = 0x0182:
    # a BG screenblock-12 tilemap entry that overlays Part 1's own code
    # (`sub r5,r5,#32` at 0x06006700). Straight-line init in sub_06006558 has
    # already executed that instruction before the font-setup call, and the
    # overwrite is observed at boot; excluded so yield/resume re-entry into the
    # function keeps verifying.
    "part1": [(0x06006700, 0x06006702)],
    # Part 2 (IWRAM): .data of part2.o (0x03002330..0x030023DC) and of
    # part2_handwritten.o (0x030023DC..0x03002DF0) are initialised writable
    # variables (linker map).
    "part2": [(0x03002330, 0x03002DF0)],
}
# In-image bytes observed dirty by the frontier test (outside the ranges above).
MUTABLE_OBSERVED = {
    # sUnk_03005808 (`.4byte 0` inside part2_handwritten .text): the emulator
    # state pointer written by the NES core and read by the IRQ handler
    # sub_030057A8 (`ldr r0,sUnk_03005808`). Observed dirty at 0x03005809/0B.
    "part2": [(0x03005808, 0x0300580C)],
}
for _name, _runs in MUTABLE_OBSERVED.items():
    MUTABLE_DATA.setdefault(_name, []).extend(_runs)

MUTABLE_NOTES = {
    ("part1", 0x06006700): "font setup strh overwrites a code halfword (BG screenblock 12 entry)",
    ("part2", 0x03002330): "writable .data of part2.o and part2_handwritten.o (linker map)",
    ("part2", 0x03005808): "sUnk_03005808 emulator state pointer, observed dirty at runtime",
}


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


def derive(elf, parts_dir, wanted):
    syms = readelf_symbols(elf)
    idx = section_indices(elf)
    result = []
    for name, sec, base, size, sha in PARTS:
        if name not in wanted:
            continue
        runs = part_map(syms, idx[sec], base, size)
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
        # Indirect-transfer targets: pointer words stored in the image's data
        # runs (function-pointer tables such as the 6502 opcode table, literal
        # pools of `ldr rN,=fn ; bx rN`) that land exactly on a decomp symbol
        # inside a code run of the matching mode. Local (non-FUNC) handlers
        # such as Opcode_* are reached only this way.
        path = os.path.join(parts_dir, f"{name}.bin")
        with open(path, "rb") as f:
            blob = f.read()
        assert len(blob) == size and hashlib.sha256(blob).hexdigest() == sha, name
        sym_addrs = {s["addr"] & ~1 for s in syms if s["ndx"] == idx[sec]}
        have = {a for a, _ in seeds}
        for ra, rb, k in runs:
            if k != "$d":
                continue
            for off in range((ra - base + 3) & ~3, rb - base - 3, 4):
                w = int.from_bytes(blob[off:off + 4], "little")
                t, thumb_bit = w & ~1, w & 1
                if not (base <= t < base + size) or t in have or t not in sym_addrs:
                    continue
                if name == "part6" and t >= PART6_TAIL_DATA[0]:
                    continue
                for ca, cb, ck in runs:
                    if ca <= t < cb and ck == ("$t" if thumb_bit else "$a"):
                        seeds.append((t, "thumb" if thumb_bit else "arm"))
                        have.add(t)
                        break
        seeds.sort()
        mode = {"$a": "arm", "$t": "thumb", "$d": "data"}
        result.append({
            "part": name, "section": sec, "load_address": base, "size": size,
            "sha256": sha,
            "runs": [(a, b, mode[k]) for a, b, k in runs],
            "seeds": seeds,
        })
    return result


def emit_canonical(result):
    out = ["# Canonical NES emulator map, USA rev 0 (schema 1).",
           "#",
           "# Structural metadata only: image geometry and digests, code/data",
           "# runs, function seeds, runtime-proven mutable ranges. No ROM-derived",
           "# bytes. Consumed by scripts/generate-nes-emulator-config.py.",
           "# Verified against a locally built nested decomp ELF with",
           "# scripts/derive-nes-emulator-map.py --compare (maintainer oracle).",
           "", "schema = 1", ""]
    for img in result:
        name = img["part"]
        out += [f"[[image]]", f'id = "nes_{name}"', f'part = "{name}"',
                f'section = "{img["section"]}"',
                f'load_address = 0x{img["load_address"]:08X}',
                f'size = 0x{img["size"]:X}', f'sha256 = "{img["sha256"]}"']
        out += ["", "# runs: complete tiling of the image, [start, end, mode]",
                "runs = ["]
        out += [f'  [0x{a:08X}, 0x{b:08X}, "{m}"],' for a, b, m in img["runs"]]
        out += ["]"]
        for m in ("arm", "thumb"):
            out += ["", f"seeds_{m} = ["]
            addrs = [a for a, mm in img["seeds"] if mm == m]
            for i in range(0, len(addrs), 6):
                out.append("  " + " ".join(f"0x{a:08X}," for a in addrs[i:i + 6]))
            out += ["]"]
        for a, b in sorted(MUTABLE_DATA.get(name, [])):
            out += ["", "[[image.mutable]]", f"start = 0x{a:08X}", f"end = 0x{b:08X}",
                    f'note = "{MUTABLE_NOTES[(name, a)]}"']
        out += [""]
    return "\n".join(out)


def compare(result, canonical_path):
    import tomllib
    with open(canonical_path, "rb") as f:
        canon = tomllib.load(f)
    by_part = {i["part"]: i for i in canon["image"]}
    problems = []
    for img in result:
        c = by_part.get(img["part"])
        if c is None:
            problems.append(f'{img["part"]}: missing from canonical map')
            continue
        for key in ("section", "load_address", "size", "sha256"):
            if c[key] != img[key]:
                problems.append(f'{img["part"]}: {key} canonical={c[key]!r} derived={img[key]!r}')
        cr = sorted((a, b, m) for a, b, m in c["runs"])
        dr = sorted(img["runs"])
        problems += [f'{img["part"]}: run only in canonical {r}' for r in set(cr) - set(dr)]
        problems += [f'{img["part"]}: run only in derived {r}' for r in set(dr) - set(cr)]
        cs = {(a, "arm") for a in c["seeds_arm"]} | {(a, "thumb") for a in c["seeds_thumb"]}
        ds = set(img["seeds"])
        problems += [f'{img["part"]}: seed only in canonical 0x{a:08X} {m}' for a, m in sorted(cs - ds)]
        problems += [f'{img["part"]}: seed only in derived 0x{a:08X} {m}' for a, m in sorted(ds - cs)]
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--elf", required=True, help="locally built nested decomp ELF (oracle)")
    ap.add_argument("--compare", metavar="CANONICAL",
                    help="derive from the ELF and fail if it diverges from this map")
    ap.add_argument("--emit-candidate", metavar="PATH",
                    help="write the derived candidate canonical map here")
    ap.add_argument("--parts-dir", default=".local/nes-emulator",
                    help="directory with extract-nes-emulator.py output")
    ap.add_argument("--parts", default="part1,part2,part3,part4,part5,part6")
    args = ap.parse_args()
    if not args.compare and not args.emit_candidate:
        ap.error("nothing to do: pass --compare and/or --emit-candidate")
    result = derive(args.elf, args.parts_dir, set(args.parts.split(",")))
    if args.emit_candidate:
        with open(args.emit_candidate, "w") as f:
            f.write(emit_canonical(result))
        print(f"candidate written: {args.emit_candidate}")
    if args.compare:
        problems = compare(result, args.compare)
        for p in problems:
            print("DIVERGE:", p)
        n_seeds = sum(len(i["seeds"]) for i in result)
        n_runs = sum(len(i["runs"]) for i in result)
        if problems:
            print(f"ORACLE COMPARE FAIL: {len(problems)} difference(s)")
            return 1
        print(f"ORACLE COMPARE PASS: {len(result)} images, {n_runs} runs, {n_seeds} seeds match {args.compare}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
