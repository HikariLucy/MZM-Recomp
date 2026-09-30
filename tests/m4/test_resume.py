"""M4-RESUME-1/2: reviewed resume units (VBlank interior resumes).

A VBlank yield unwinds to the outer loop, which re-dispatches R15 at the NEXT
instruction prologue (runtime_should_yield). A function that can outlast a frame
needs a static resume alias at every decoded instruction; static_resume_all
would publish that for the whole ROM (233,709 rows) including unaudited code, so
only the reviewed units of configs/mzm-resume-units.toml get it. The PCs are
derived by scripts/expand-resume-units.py from the recompiler's own decode.

Ground truth here is the generated corpus: every decoded instruction carries a
`/* PC ... T|A ... */` comment inside the function that contains it; literal
pools, data and padding have none.
"""
import importlib.util
import re
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / "generated"
LOCAL = ROOT / ".local"
UNITS = ROOT / "configs" / "mzm-resume-units.toml"
CONFIG = ROOT / "configs" / "mzm-us.toml"
TOOL = ROOT / "scripts" / "expand-resume-units.py"
# Units the boot after the NES quit needs (each audited individually, see docs).
REQUIRED_UNITS = ["InitializeGame", "sram", "InitializeAudio"]

_spec = importlib.util.spec_from_file_location("expand_resume_units", TOOL)
tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tool)


def units():
    return tool.load_units(UNITS)


def dispatch_rows(path):
    text = Path(path).read_text()
    rows = {}
    for m in re.finditer(r"\{0x([0-9A-F]{8})u, (\d)u, (\d)u, (\w+)\}", text):
        rows[(int(m.group(1), 16), m.group(2) == "1")] = (m.group(3) == "1", m.group(4))
    return rows


def unit_functions(u, funcs):
    return {n: v for n, v in funcs.items() if u["start"] <= v[1][0] < u["end"]}


class ContractAuditTests(unittest.TestCase):
    """The safety contract itself: locals confined to their instruction's block."""

    GOOD = ("    if (g_runtime_resume_pc) { }\n"
            "    /* 08000000  08000000 T movs r0,#0 */\n    uint32_t _cyc_08000000 = 1u;\n    g_cpu.R[0] = _cyc_08000000;\n"
            "    /* 08000002  08000002 T bx r0 */\n    uint32_t _bxt_08000002 = g_cpu.R[0];\n")

    def test_confined_locals_pass(self):
        self.assertEqual(tool.audit_function("f", self.GOOD), [])

    def test_local_carried_across_instructions_is_rejected(self):
        bad = self.GOOD.replace("_bxt_08000002 = g_cpu.R[0];", "_bxt_08000002 = _cyc_08000000;")
        problems = tool.audit_function("f", bad)
        self.assertEqual(len(problems), 1)
        self.assertIn("0x08000002", problems[0])

    def test_static_local_is_rejected(self):
        bad = self.GOOD + "    static uint32_t hidden = 0;\n"
        self.assertTrue(any("static" in p for p in tool.audit_function("f", bad)))

    def test_expansion_rejects_a_function_that_leaves_the_unit(self):
        body = "    /* 08000000  08000000 T nop */\n    /* 08000002  08000002 T nop */\n"
        funcs = {"gf_a": ("T", [0x08000000, 0x08000002], body)}
        u = {"name": "u", "start": 0x08000000, "end": 0x08000002, "mode": "thumb", "reason": "x"}
        with self.assertRaises(SystemExit):
            tool.expand([u], funcs)

    def test_expansion_rejects_a_range_that_does_not_start_at_a_root(self):
        funcs = {"gf_a": ("T", [0x08000000, 0x08000002], "    /* 08000000  08000000 T nop */\n")}
        u = {"name": "u", "start": 0x08000002, "end": 0x08000004, "mode": "thumb", "reason": "x"}
        with self.assertRaises(SystemExit):
            tool.expand([u], funcs)

    def test_expansion_publishes_instructions_only_and_skips_other_roots(self):
        a = "    /* 08000000  08000000 T nop */\n    /* 08000002  08000002 T nop */\n"
        b = "    /* 08000006  08000006 T nop */\n"
        funcs = {"gf_a": ("T", [0x08000000, 0x08000002], a), "gf_b": ("T", [0x08000006], b)}
        u = {"name": "u", "start": 0x08000000, "end": 0x08000008, "mode": "thumb", "reason": "x"}
        info, entries = tool.expand([u], funcs)
        self.assertEqual([hex(e[0]) for e in entries], ["0x8000002"])   # 0x08000004 (data) and roots are not
        self.assertEqual(info["u"]["insn"], 3)


class UnitsFileTests(unittest.TestCase):
    def test_required_units_are_declared_with_a_reason(self):
        by_name = {u["name"]: u for u in units()}
        for name in REQUIRED_UNITS:
            self.assertIn(name, by_name, "reviewed unit missing from configs/mzm-resume-units.toml")
            self.assertTrue(by_name[name]["reason"].strip())

    def test_units_do_not_overlap(self):
        us = sorted(units(), key=lambda u: u["start"])
        for a, b in zip(us, us[1:]):
            self.assertLessEqual(a["end"], b["start"], f"{a['name']} overlaps {b['name']}")

    def test_no_pcs_are_listed_by_hand_in_the_main_config(self):
        self.assertNotIn("resume = true", CONFIG.read_text())

    def test_static_resume_all_is_not_enabled(self):
        self.assertNotIn("static_resume_all = true", CONFIG.read_text())


@unittest.skipUnless((GEN / "dispatch_table.cpp").exists(), "generated corpus not present")
class GeneratedResumeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.funcs = tool.load_functions(str(GEN))
        cls.rows = dispatch_rows(GEN / "dispatch_table.cpp")

    def test_every_required_unit_is_a_generated_function_set(self):
        for u in units():
            self.assertTrue(unit_functions(u, self.funcs), u["name"])

    def test_every_instruction_of_a_unit_is_a_resume_of_its_containing_function(self):
        roots = {v[1][0] for v in self.funcs.values()}
        missing, wrong = [], []
        for u in units():
            thumb = u["mode"] == "thumb"
            for name, (mode, pcs, body) in unit_functions(u, self.funcs).items():
                self.assertEqual(self.rows.get((pcs[0], thumb)), (False, name), "root " + name)
                for pc in pcs[1:]:
                    if pc in roots:
                        continue
                    got = self.rows.get((pc, thumb))
                    if got is None:
                        missing.append((u["name"], hex(pc)))
                    elif got != (True, name):
                        wrong.append((hex(pc), got, name))
        self.assertEqual(wrong, [])
        self.assertEqual(missing, [], "instruction PCs without a static resume")

    def test_nothing_but_instruction_boundaries_is_published_in_a_unit(self):
        for u in units():
            thumb = u["mode"] == "thumb"
            pcs = {p for _, (m, ps, b) in unit_functions(u, self.funcs).items() for p in ps}
            extra = sorted(pc for (pc, t), (resume, _) in self.rows.items()
                           if resume and t == thumb and u["start"] <= pc < u["end"] and pc not in pcs)
            self.assertEqual([hex(e) for e in extra], [], f"{u['name']}: data/literal PCs published")
            self.assertEqual([hex(pc) for (pc, t) in self.rows
                              if u["start"] <= pc < u["end"] and t != thumb], [], "other-mode entry in the unit")

    def test_resumes_are_published_only_inside_reviewed_units(self):
        us = units()
        stray = sorted(pc for (pc, t), (resume, _) in self.rows.items()
                       if resume and not any(u["start"] <= pc < u["end"] for u in us))
        self.assertEqual([hex(s) for s in stray], [])

    def test_the_contract_holds_for_every_unit_function(self):
        for u in units():
            for name, (m, pcs, body) in unit_functions(u, self.funcs).items():
                self.assertEqual(tool.audit_function(name, body), [])

    def test_expansion_is_deterministic_and_matches_the_generation_overlay(self):
        with tempfile.TemporaryDirectory() as tmp:
            outs = []
            for i in range(2):
                out = Path(tmp) / f"o{i}.toml"
                subprocess.check_call([sys.executable, str(TOOL), "--units", str(UNITS),
                                       "--corpus", str(GEN), "--out", str(out)], stdout=subprocess.DEVNULL)
                outs.append(out.read_bytes())
            self.assertEqual(outs[0], outs[1])
        overlay = LOCAL / "mzm-us-resume.toml"
        if overlay.exists():
            self.assertEqual(overlay.read_bytes(), outs[0], "overlay used for generation is stale")

    def test_normal_corpus_loses_no_entry_and_only_gains_resume_aliases(self):
        base = LOCAL / "resume-pass1-dispatch_table.cpp"
        if not base.exists():
            self.skipTest("resume-free baseline not present (run scripts/generate-m1.sh)")
        before = dispatch_rows(base)
        for key, val in before.items():
            self.assertEqual(self.rows.get(key), val, f"entry 0x{key[0]:08X} changed or lost")
        added = {k: v for k, v in self.rows.items() if k not in before}
        self.assertTrue(all(resume for resume, _ in added.values()), "a non-resume entry was added")
        self.assertEqual(len(self.rows) - len(before), len(added))


if __name__ == "__main__":
    unittest.main()
