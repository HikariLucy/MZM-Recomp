#!/usr/bin/env python3
"""
Expand the reviewed resume units (configs/mzm-resume-units.toml) into
`[[extra_func]] resume = true` entries for gba_recompile.

Why (M4-RESUME-1/2): runtime_should_yield() unwinds at the next instruction
prologue after a VBlank start, and the outer loop re-dispatches R15 there. A
function that can outlast a frame therefore needs a static resume alias at every
instruction boundary. static_resume_all does that for the whole program
(233,709 rows vs 28,670), including code nobody has audited. A *reviewed unit*
is a function, or a small set of functions, for which the safety contract below
has been checked; only its decoded instruction PCs are published.

Source of truth: the units file (name / range / mode / reason). The PCs are
DERIVED, never listed by hand. Instruction boundaries come from the recompiler's
own decoder: a first generation pass WITHOUT resume overlays is emitted and every
decoded instruction of the functions rooted in a unit carries a
`/* PC  rawaddr T|A  mnemonic */` comment in its generated body. Literal pools,
data and padding have no such comment, so they are never published. (Thumb `bl`
is two instructions, `bl.hi`/`bl.lo`; the pair passes its state through R14, so
both halves are legitimate resume points.)

Safety contract (checked here, mechanically, by audit_function()):
  a resume PC may be entered with nothing but guest-visible state, i.e.
  g_cpu registers, CPSR, guest memory and the runtime's guest-visible globals.
  The generated code guarantees it when every C++ local is confined to the block
  of the instruction that declares it. Locals are named `_<kind>_<PC>` after the
  declaring instruction, so a block that mentions a local (or label) named after
  a DIFFERENT instruction, or any `static` local, breaks the contract and the
  unit is rejected.

Usage:
  expand-resume-units.py --units U --corpus DIR --out OVERLAY.toml   # derive
  expand-resume-units.py --units U --corpus DIR --check              # audit only
"""
import argparse
import glob
import os
import re
import sys
import tomllib

FUNC_RE = re.compile(r"^void (gf_\w+)\(void\) \{\n(.*?)^}\n", re.S | re.M)
INSN_RE = re.compile(r"^    /\* ([0-9A-F]{8})  [0-9a-f]{8} ([TA]) ", re.M)
LOCAL_RE = re.compile(r"\b_[a-z]+_([0-9A-F]{8})\b")


def load_functions(corpus):
    """{name: (mode 'T'|'A', [pcs in body order], body)} for every generated function."""
    out = {}
    for f in sorted(glob.glob(os.path.join(corpus, "recompiled_*.cpp"))):
        with open(f) as fh:
            text = fh.read()
        for m in FUNC_RE.finditer(text):
            pcs = [(int(x, 16), mode) for x, mode in INSN_RE.findall(m.group(2))]
            if pcs:
                out[m.group(1)] = (pcs[0][1], [p for p, _ in pcs], m.group(2))
    return out


def audit_function(name, body):
    """Return the list of contract violations of one generated body."""
    problems = []
    marks = [(m.start(), int(m.group(1), 16)) for m in INSN_RE.finditer(body)]
    for i, (pos, pc) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(body)
        block = body[pos:end]
        # The resume prologue (before the first comment) is not an instruction block.
        for m in LOCAL_RE.finditer(block):
            if int(m.group(1), 16) != pc:
                problems.append(f"{name}: block 0x{pc:08X} uses local {m.group(0)} of another instruction")
        if re.search(r"\bstatic\b", block):
            problems.append(f"{name}: block 0x{pc:08X} declares a static")
    return problems


def load_units(path):
    with open(path, "rb") as f:
        doc = tomllib.load(f)
    units = doc.get("resume_unit", [])
    names = set()
    for u in units:
        for k in ("name", "start", "end", "mode", "reason"):
            if k not in u:
                raise SystemExit(f"resume_unit {u.get('name', '?')}: missing '{k}'")
        if u["name"] in names:
            raise SystemExit(f"duplicate resume_unit {u['name']}")
        names.add(u["name"])
        if u["mode"] not in ("thumb", "arm"):
            raise SystemExit(f"resume_unit {u['name']}: mode must be thumb or arm")
        if not u["start"] < u["end"]:
            raise SystemExit(f"resume_unit {u['name']}: empty range")
    return units


def expand(units, funcs):
    """Return ({unit name: info}, sorted entries [(pc, mode, unit)])."""
    letter = {"thumb": "T", "arm": "A"}
    roots = {pcs[0] for _, (m, pcs, b) in funcs.items()}
    info, entries, seen = {}, [], {}
    problems = []
    for u in units:
        inside = {n: v for n, v in funcs.items()
                  if u["start"] <= v[1][0] < u["end"]}
        rec = {"functions": sorted(inside), "insn": 0, "resume": 0, "roots": 0}
        if not inside or min(v[1][0] for v in inside.values()) != u["start"]:
            problems.append(f"{u['name']}: 0x{u['start']:08X} is not a generated function root")
        for n, (m, pcs, body) in sorted(inside.items()):
            if m != letter[u["mode"]]:
                problems.append(f"{u['name']}: {n} is not {u['mode']}")
            out = [p for p in pcs if not u["start"] <= p < u["end"]]
            if out:
                problems.append(f"{u['name']}: {n} decodes 0x{out[0]:08X} outside the unit "
                                f"(widen the unit or split the function)")
            problems += audit_function(n, body)
            rec["insn"] += len(pcs)
            for pc in pcs[1:]:
                if pc in roots:
                    continue                      # another function's own entry
                if pc in seen:
                    problems.append(f"{u['name']}: 0x{pc:08X} also derived by unit {seen[pc]}")
                    continue
                seen[pc] = u["name"]
                entries.append((pc, u["mode"], u["name"]))
                rec["resume"] += 1
            rec["roots"] += 1
        info[u["name"]] = rec
    if problems:
        raise SystemExit("resume unit audit FAILED:\n  " + "\n  ".join(problems))
    entries.sort()
    return info, entries


HEADER = """# GENERATED by scripts/expand-resume-units.py from configs/mzm-resume-units.toml
# and the decoded instructions of the first (resume-free) generation pass.
# Do not edit: change the reviewed units instead.
"""


def render(units, info, entries):
    out = [HEADER]
    for u in units:
        r = info[u["name"]]
        out.append(f"# unit {u['name']}: [0x{u['start']:08X}, 0x{u['end']:08X}) {u['mode']}, "
                   f"{r['roots']} functions, {r['insn']} instructions, {r['resume']} resume entries")
    for pc, mode, unit in entries:
        out.append(f'\n[[extra_func]]\naddr = 0x{pc:08X}\nmode = "{mode}"\nresume = true\nnote = "resume unit {unit}"')
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--units", required=True)
    ap.add_argument("--corpus", required=True, help="generated corpus of the resume-free pass")
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if not a.out and not a.check:
        ap.error("pass --out and/or --check")
    units = load_units(a.units)
    funcs = load_functions(a.corpus)
    info, entries = expand(units, funcs)
    for u in units:
        r = info[u["name"]]
        print(f"unit {u['name']}: {r['roots']} functions, {r['insn']} instructions, {r['resume']} resume entries")
    print(f"total resume entries: {len(entries)}")
    if a.out:
        with open(a.out, "w") as f:
            f.write(render(units, info, entries))
        print(f"overlay written: {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
