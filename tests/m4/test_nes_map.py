import hashlib
import importlib.util
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP = ROOT / "configs" / "nes-emulator-map-us.toml"
GEN = ROOT / "scripts" / "generate-nes-emulator-config.py"
PARTS = ROOT / ".local" / "nes-emulator"
HEADER = ROOT / "src" / "mzm_nes_emulator_map.h"


def load():
    with open(MAP, "rb") as f:
        return tomllib.load(f)["image"]


class CanonicalMapTests(unittest.TestCase):
    # Bytes the decomp mapping symbols leave unclassified (neither code nor
    # declared data); preserved as-is from the original oracle derivation.
    KNOWN_UNCLASSIFIED = {"nes_part4": [(0x0600C000, 0x0600C00C)]}

    def test_runs_cover_each_image_without_overlap(self):
        for img in load():
            gaps, cur = [], img["load_address"]
            for a, b, mode in sorted(tuple(r) for r in img["runs"]):
                self.assertGreaterEqual(a, cur, f"overlap in {img['id']}")
                self.assertIn(mode, ("arm", "thumb", "data"))
                self.assertGreater(b, a)
                if a > cur:
                    gaps.append((cur, a))
                cur = b
            if cur < img["load_address"] + img["size"]:
                gaps.append((cur, img["load_address"] + img["size"]))
            self.assertLessEqual(cur, img["load_address"] + img["size"])
            self.assertEqual(gaps, self.KNOWN_UNCLASSIFIED.get(img["id"], []), img["id"])

    def test_seeds_are_unique_and_inside_matching_code_runs(self):
        for img in load():
            seen = set()
            for mode in ("arm", "thumb"):
                for a in img[f"seeds_{mode}"]:
                    self.assertNotIn(a, seen, hex(a))
                    seen.add(a)
                    self.assertTrue(any(r[0] <= a < r[1] and r[2] == mode for r in img["runs"]),
                                    f"{img['id']} seed {a:#x} {mode}")

    def test_mutable_ranges_inside_image_and_no_payload_bytes(self):
        text = MAP.read_text()
        self.assertLess(len(text), 64 * 1024)
        for img in load():
            for m in img.get("mutable", []):
                self.assertGreaterEqual(m["start"], img["load_address"])
                self.assertLessEqual(m["end"], img["load_address"] + img["size"])
            self.assertEqual(len(img["sha256"]), 64)
        self.assertNotIn("/home/", text)

    def test_expansion_is_deterministic_and_relative(self):
        with tempfile.TemporaryDirectory() as tmp:
            outs = []
            for i in range(2):
                out = Path(tmp) / f"c{i}.toml"
                subprocess.check_call([sys.executable, str(GEN), "--config-out", str(out)],
                                      stdout=subprocess.DEVNULL)
                outs.append(out.read_bytes())
            self.assertEqual(outs[0], outs[1])
            cfg = tomllib.loads(outs[0].decode())
            self.assertEqual(len(cfg["executable_image"]), 6)
            self.assertEqual(len(cfg["extra_func"]), 388)
            for img in cfg["executable_image"]:
                self.assertTrue(img["path"].startswith(".local/nes-emulator/"))
                self.assertTrue(img["overlay"])
            self.assertNotIn(str(ROOT), outs[0].decode())

    @unittest.skipUnless((PARTS / "part6.bin").exists(), "ROM-extracted parts not present")
    def test_committed_header_matches_canonical_map(self):
        r = subprocess.run([sys.executable, str(GEN), "--check-header", str(HEADER),
                            "--parts-dir", str(PARTS)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
