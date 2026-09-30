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

    def test_scopes_tile_images_and_requires_are_declared_scopes(self):
        for img in load():
            scopes = img.get("scope")
            if not scopes:
                continue
            ids = [sc["id"] for sc in scopes]
            self.assertEqual(len(ids), len(set(ids)), img["id"])
            tiles = sorted(tuple(r) for sc in scopes for r in sc["ranges"])
            cur = img["load_address"]
            for a, b in tiles:
                self.assertEqual(a, cur, f"{img['id']} scope gap/overlap at {cur:#x}")
                self.assertGreater(b, a)
                cur = b
            self.assertEqual(cur, img["load_address"] + img["size"], img["id"])
            for sc in scopes:
                self.assertLessEqual(set(sc.get("requires", [])), set(ids) - {sc["id"]})
                self.assertIn(sc["lifecycle"], ("resident", "overwritten-after-init"))
            for m in img.get("mutable", []):
                self.assertTrue(any(a <= m["start"] and m["end"] <= b
                                    for sc in scopes for a, b in sc["ranges"]),
                                "mutable range straddles scopes")
            for mode in ("arm", "thumb"):
                for a in img[f"seeds_{mode}"]:
                    self.assertTrue(any(x <= a < y for sc in scopes for x, y in sc["ranges"]))

    def test_part1_lifecycle_boundary(self):
        part1 = next(i for i in load() if i["id"] == "nes_part1")
        sc = {s["id"]: s for s in part1["scope"]}
        self.assertEqual(sc["init"]["ranges"], [[0x06006000, 0x06006E00]])
        self.assertEqual(sc["resident"]["ranges"],
                         [[0x06006E00, 0x0600713C], [0x06007210, 0x06007240]])
        self.assertEqual(sc["menu"]["ranges"], [[0x0600713C, 0x06007210]])
        # 0x06006E00 is the first literal of the resident handler sub_06006E08,
        # which is also the first resident seed.
        self.assertIn(0x06006E08, part1["seeds_arm"])
        self.assertNotIn("resident", sc["resident"].get("requires", []))

    @unittest.skipUnless((PARTS / "part1.bin").exists(), "ROM-extracted parts not present")
    def test_scope_requires_cover_every_direct_static_transfer(self):
        """Every b/bl, pc-relative load and pc-relative address that leaves a scope
        must be listed in requires (transitively), otherwise generated code would
        reach bytes the resolver never verified."""
        for img in load():
            scopes = img.get("scope")
            if not scopes:
                continue
            blob = (PARTS / f"{img['part']}.bin").read_bytes()
            base = img["load_address"]

            def scope_of(a):
                for s in scopes:
                    if any(x <= a < y for x, y in s["ranges"]):
                        return s["id"]
                return None

            by_id = {s["id"]: s for s in scopes}

            def closure(sid):
                seen, todo = {sid}, [sid]
                while todo:
                    for r in by_id[todo.pop()].get("requires", []):
                        if r not in seen:
                            seen.add(r)
                            todo.append(r)
                return seen

            edges = set()
            for a, b, mode in img["runs"]:
                if mode != "arm":
                    continue
                for pc in range(a, b, 4):
                    w = int.from_bytes(blob[pc - base:pc - base + 4], "little")
                    tgt = None
                    if (w >> 25) & 7 == 5:                       # b / bl
                        imm = w & 0xFFFFFF
                        imm -= (1 << 24) if imm & 0x800000 else 0
                        tgt = pc + 8 + (imm << 2)
                    elif (w & 0x0C000000) == 0x04000000 and (w >> 16) & 15 == 15 \
                            and not w & 0x02000000 and w & 0x00100000:   # ldr rX,[pc,#imm]
                        off = w & 0xFFF
                        tgt = pc + 8 + (off if w & 0x00800000 else -off)
                    elif (w & 0x0E000000) == 0x02000000 and (w >> 16) & 15 == 15 \
                            and (w >> 21) & 15 in (2, 4):          # add/sub rX,pc,#imm
                        rot = ((w >> 8) & 15) * 2
                        imm = ((w & 0xFF) >> rot | (w & 0xFF) << (32 - rot)) & 0xFFFFFFFF if rot else w & 0xFF
                        tgt = pc + 8 + (imm if (w >> 21) & 15 == 4 else -imm)
                    elif (w & 0x0E000010) == 0x00000000 and (w >> 16) & 15 == 15 \
                            and (w >> 21) & 15 == 4:               # add rX,pc,rY,lsl #n (table base)
                        tgt = pc + 8
                    if tgt is None or not (base <= tgt < base + img["size"]):
                        continue
                    src, dst = scope_of(pc), scope_of(tgt)
                    if src != dst:
                        edges.add((src, dst, pc, tgt))
            self.assertTrue(edges, "expected cross-scope edges for Part 1 (init->resident, menu->init)")
            for src, dst, pc, tgt in sorted(edges):
                self.assertIn(dst, closure(src),
                              f"{img['id']}: {pc:#010x} ({src}) reaches {tgt:#010x} ({dst}) "
                              f"but {dst!r} is not in requires of {src!r}")
            self.assertIn(("menu", "init", 0x06007208, 0x06006880), edges)
            self.assertIn(("init", "resident", 0x0600691C, 0x06006E08), edges)
            self.assertFalse([e for e in edges if e[0] == "resident"],
                             "resident must have no direct transfer out of the scope")

    @unittest.skipUnless((PARTS / "part6.bin").exists(), "ROM-extracted parts not present")
    def test_part6_mutable_is_exactly_the_zero_initialised_password_buffer(self):
        # sPasswordBytes[18] (nes_metroid/emulator/src/part6.c): first object of
        # part6.o .data, zero-initialised, filled by the password/save code
        # (first store frame 3068, writer 0x0203E31C, NES-3b). It must be the
        # whole zero run [0x0203E43C,0x0203E44E) of the image and nothing else:
        # the neighbours are non-zero constants that stay byte-gated.
        img = next(i for i in load() if i["part"] == "part6")
        blob = (PARTS / "part6.bin").read_bytes()
        base = img["load_address"]
        self.assertEqual([(m["start"], m["end"]) for m in img["mutable"]], [(0x0203E43C, 0x0203E44E)])
        lo, hi = 0x0203E43C - base, 0x0203E44E - base
        self.assertEqual(blob[lo:hi], bytes(18))
        self.assertNotEqual(blob[lo - 1], 0)
        self.assertNotEqual(blob[hi], 0)
        # inside a declared data run, so no code or literal pool is excluded
        runs = [r for r in img["runs"] if r[0] <= 0x0203E43C and r[1] >= 0x0203E44E]
        self.assertEqual([r[2] for r in runs], ["data"])
        # gate = image minus exactly those bytes
        hdr = HEADER.read_text()
        self.assertIn("{0x0203E000u, 0x0203E43Cu},\n    {0x0203E44Eu, 0x0203E8E0u},", hdr)

    def test_scopes_do_not_reach_the_gbarecomp_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "cfg.toml"
            subprocess.check_call([sys.executable, str(GEN), "--config-out", str(out)],
                                  stdout=subprocess.DEVNULL)
            cfg = tomllib.loads(out.read_text())
        self.assertEqual(set(cfg), {"executable_image", "data_range", "extra_func"})
        self.assertEqual([i["id"] for i in cfg["executable_image"]],
                         [f"nes_part{n}" for n in range(1, 7)])
        for table in cfg.values():
            for row in table:
                self.assertFalse({"scope", "scopes", "lifecycle", "requires"} & set(row))
                self.assertNotIn("nes_part1_init", str(row))
                self.assertNotIn("nes_part1_resident", str(row))

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
