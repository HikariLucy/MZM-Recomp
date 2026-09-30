"""M4-RESUME-1: VBlank interior resume of the InitializeGame region.

A VBlank yield unwinds to the outer loop, which re-dispatches R15 at the NEXT
instruction prologue (runtime_should_yield). After the NES quit MZM restarts and
InitializeGame's DMA3 fills (EWRAM 0x10000 words, > one frame) cross a VBlank,
so R15 = 0x080006CA (the instruction after `str r1,[r0,#8]`) is re-dispatched
and there was no static entry (strict-static miss).

Ground truth is the generated code itself: every decoded instruction carries a
`/* PC ... T ... */` comment inside the function that contains it; literal
pools have none. Expected: every interior instruction of the region is a
resume alias of ITS containing function; nothing else (data, other modes) is
published; the roots are unchanged.
"""
import glob
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / "generated"
CONFIG = ROOT / "configs" / "mzm-us.toml"
START, END = 0x080006A0, 0x080007C4          # InitializeGame [start, end) (decomp symbols)


def region_functions():
    """{function name: (root pc, [instruction pcs])} for functions rooted in the region."""
    out = {}
    for f in sorted(glob.glob(str(GEN / "recompiled_*.cpp"))):
        text = Path(f).read_text()
        for m in re.finditer(r"^void (gf_\w+)\(void\) \{\n(.*?)^}\n", text, re.S | re.M):
            pcs = [int(x, 16) for x in re.findall(r"/\* ([0-9A-F]{8})  [0-9a-f]{8} T ", m.group(2))]
            if pcs and START <= pcs[0] < END:
                out[m.group(1)] = (pcs[0], pcs)
    return out


def thumb_entries():
    text = (GEN / "dispatch_table.cpp").read_text()
    out = {}
    for m in re.finditer(r"\{0x([0-9A-F]{8})u, (\d)u, (\d)u, (\w+)\}", text):
        pc, thumb, resume, fn = int(m.group(1), 16), m.group(2) == "1", m.group(3) == "1", m.group(4)
        if START <= pc < END:
            out[(pc, thumb)] = (resume, fn)
    return out


@unittest.skipUnless((GEN / "dispatch_table.cpp").exists(), "generated corpus not present")
class InitializeGameResumeTests(unittest.TestCase):
    def test_region_shape(self):
        funcs = region_functions()
        self.assertIn("gf_InitializeGame", funcs)
        self.assertEqual(funcs["gf_InitializeGame"][0], START)
        pcs = {p for _, ps in funcs.values() for p in ps}
        self.assertIn(0x080006CA, pcs)          # after the EWRAM DMA3 start (first frontier)
        self.assertIn(0x080006DC, pcs)          # after the IWRAM DMA3 start

    def test_every_interior_instruction_is_a_resume_of_its_containing_function(self):
        entries = thumb_entries()
        missing, wrong = [], []
        for name, (root, pcs) in region_functions().items():
            self.assertEqual(entries.get((root, True)), (False, name), "root " + name)
            for pc in pcs[1:]:
                got = entries.get((pc, True))
                if got is None:
                    missing.append(pc)
                elif got != (True, name):
                    wrong.append((hex(pc), got, name))
        self.assertEqual(wrong, [])
        self.assertEqual([hex(m) for m in missing], [], "instruction PCs without a static resume")

    def test_nothing_but_instruction_boundaries_is_published(self):
        pcs = {p for _, ps in region_functions().values() for p in ps}
        extra = sorted(pc for (pc, thumb), (resume, _) in thumb_entries().items() if resume and pc not in pcs)
        self.assertEqual([hex(e) for e in extra], [], "literal/data PCs published as resumes")
        for data_pc in (0x08000734, 0x08000740, 0x08000748):     # literal pool words
            self.assertNotIn((data_pc, True), thumb_entries())

    def test_no_arm_entries_in_the_thumb_region(self):
        self.assertEqual([hex(pc) for (pc, thumb) in thumb_entries() if not thumb], [])

    def test_config_lists_exactly_the_interior_instructions(self):
        text = CONFIG.read_text()
        declared = {int(m, 16) for m in re.findall(
            r"\[\[extra_func\]\]\naddr = 0x(080006[0-9A-F]{2}|080007[0-9A-F]{2})\nmode = \"thumb\"\nresume = true", text)}
        want = {p for _, (r, ps) in region_functions().items() for p in ps[1:]}
        self.assertEqual(sorted(hex(x) for x in declared), sorted(hex(x) for x in want))


if __name__ == "__main__":
    unittest.main()
