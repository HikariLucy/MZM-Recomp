import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class RegressionTests(unittest.TestCase):
    def test_haze_case_requires_native_trace_and_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            for name in ("rom.gba", "bios.bin"):
                (base / name).write_bytes(b"test")
            binary = base / "fake-game"
            binary.write_text(
                "#!/bin/sh\n"
                "test \"$MZM_TRACE_RAM_DISPATCH\" = 1 || exit 8\n"
                "echo 'cpu_backend=static-recompiled strict_static=ENABLED dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0'\n"
                "echo 'mzm_ram_dispatch kind=haze variant=Haze_Bg3 runtime_pc=0x03001944 source_pc=0x0805d768 match=1 native=1 hits=1'\n"
                "echo 'mzm_ram_dispatch_summary hook_calls=3 haze_attempts=1 haze_matches=1'\n"
            )
            binary.chmod(0o755)
            cases = base / "cases.toml"
            cases.write_text(
                '[[case]]\nname = "haze"\nrom_target = "usa"\nframes = 1\n'
                'haze_variant = "Haze_Bg3"\n[case.expect]\n'
                'haze_runtime_pc = "0x03001944"\nhaze_source_pc = "0x0805d768"\n'
            )
            command = [sys.executable, str(ROOT / "tools/m4-regression/run.py"),
                       "--cases", str(cases), "--bin", str(binary),
                       "--rom", str(base / "rom.gba"), "--bios", str(base / "bios.bin")]
            report = base / "pass"
            result = subprocess.run(command + ["--output", str(report)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            row = json.loads((report / "summary.json").read_text())["cases"][0]
            self.assertEqual(row["haze_variant_observed"], "Haze_Bg3")
            binary.write_text(
                "#!/bin/sh\n"
                "echo 'mzm_ram_dispatch kind=haze variant=Haze_Bg3 runtime_pc=0x03001944 source_pc=0x0805d768 match=1 native=0 hits=1'\n"
                "echo 'mzm_ram_dispatch_summary hook_calls=3 haze_attempts=1 haze_matches=1'\n"
            )
            report = base / "missing-hit"
            result = subprocess.run(command + ["--output", str(report)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            row = json.loads((report / "summary.json").read_text())["cases"][0]
            self.assertIn("haze_variant", row["failed_checks"])

    def test_case_can_require_waitcnt_write_and_disable_vblank_yield(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            for name in ("rom.gba", "bios.bin"):
                (base / name).write_bytes(b"test")
            binary = base / "fake-game"
            binary.write_text(
                "#!/bin/sh\n"
                "test \"$GBARECOMP_YIELD_ON_VBLANK\" = 0 || exit 8\n"
                "printf 'idx,cycle,pc,addr,value,size\\n0,9,0x08000000,0x04000204,0x45b4,2\\n' > \"$GBARECOMP_MMIO_DUMP\"\n"
                "echo 'cpu_backend=static-recompiled strict_static=ENABLED final_pc=0x000001b4 steps=1400 cycles=67667408 ppu_frames=317'\n"
                "echo 'dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0'\n"
            )
            binary.chmod(0o755)
            cases = base / "cases.toml"
            cases.write_text(
                '[[case]]\nname = "init"\nrom_target = "usa"\nsteps = 1400\n'
                'vblank_yield = false\nwaitcnt_write = 0x45b4\n'
                '[case.expect]\nfinal_pc = "0x000001b4"\n'
            )
            report = base / "report"
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/m4-regression/run.py"),
                 "--cases", str(cases), "--bin", str(binary),
                 "--rom", str(base / "rom.gba"), "--bios", str(base / "bios.bin"),
                 "--output", str(report)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            row = json.loads((report / "summary.json").read_text())["cases"][0]
            self.assertEqual(row["cycles"], 67667408)
            self.assertEqual(row["waitcnt_write_observed"], "0x45b4")
            binary.write_text("#!/bin/sh\necho 'final_pc=0x000001b4 cycles=67667408'\n")
            missing = base / "missing-write"
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/m4-regression/run.py"),
                 "--cases", str(cases), "--bin", str(binary),
                 "--rom", str(base / "rom.gba"), "--bios", str(base / "bios.bin"),
                 "--output", str(missing)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            row = json.loads((missing / "summary.json").read_text())["cases"][0]
            self.assertIn("waitcnt_write", row["failed_checks"])

    def test_case_runner_accepts_clean_strict_static_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            for name in ("rom.gba", "bios.bin"):
                (base / name).write_bytes(b"test")
            binary = base / "fake-game"
            binary.write_text("#!/bin/sh\necho 'cpu_backend=static-recompiled strict_static=ENABLED'\necho 'final_pc=0x08000000 unmapped=0 io_unhandled=0 steps=1000 ppu_frames=0'\necho 'dispatch_misses=0 interpreted_insns=0'\n")
            binary.chmod(0o755)
            cases = base / "cases.toml"
            cases.write_text('[[case]]\nname = "boot"\nrom_target = "usa"\nbios_required = true\nsteps = 1000\n[case.expect]\ncpu_backend = "static-recompiled"\nstrict_static = "ENABLED"\ndispatch_misses = 0\ninterpreted_insns = 0\nunmapped = 0\nio_unhandled = 0\n')
            report = base / "report"
            result = subprocess.run([sys.executable, str(ROOT / "tools/m4-regression/run.py"), "--cases", str(cases), "--bin", str(binary), "--rom", str(base / "rom.gba"), "--bios", str(base / "bios.bin"), "--config", str(ROOT / "configs/mzm-us.toml"), "--output", str(report)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads((report / "summary.json").read_text())
            self.assertEqual(data["cases"][0]["status"], "PASS")
            self.assertNotIn(str(base), (report / "summary.json").read_text())

    def test_case_specific_config_is_forwarded(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            for name in ("rom.gba", "bios.bin", "custom.toml"):
                (base / name).write_bytes(b"test")
            binary = base / "fake-game"
            binary.write_text("#!/bin/sh\ntest \"$5\" = --config && test \"$6\" = \"" + str(base / "custom.toml") + "\" || exit 9\necho 'cpu_backend=static-recompiled strict_static=ENABLED dispatch_misses=0 interpreted_insns=0 unmapped=0 io_unhandled=0'\n")
            binary.chmod(0o755)
            cases = base / "cases.toml"
            cases.write_text('[[case]]\nname = "custom"\nrom_target = "usa"\nbios_required = true\nconfig = "' + str(base / 'custom.toml') + '"\nframes = 1\n[case.expect]\nstrict_static = "ENABLED"\n')
            result = subprocess.run([sys.executable, str(ROOT / "tools/m4-regression/run.py"), "--cases", str(cases), "--bin", str(binary), "--rom", str(base / "rom.gba"), "--bios", str(base / "bios.bin"), "--output", str(base / "report")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_diff_reports_failed_case_with_same_counters(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            values = {"case": "boot", "status": "PASS", "exit_code": 0, "dispatch_misses": 0, "interpreted_insns": 0, "unmapped": 0, "io_unhandled": 0}
            old = base / "old.json"
            new = base / "new.json"
            old.write_text(json.dumps({"schema_version": 1, "cases": [values]}))
            new.write_text(json.dumps({"schema_version": 1, "cases": [{**values, "status": "FAIL"}]}))
            result = subprocess.run([sys.executable, str(ROOT / "scripts/compare-m4-regression.py"), str(old), str(new)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("REGRESSION", result.stdout)

    def test_diff_reports_counter_regression(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            old = base / "old.json"
            new = base / "new.json"
            old.write_text(json.dumps({"cases": [{"case": "boot", "status": "PASS", "exit_code": 0, "dispatch_misses": 0, "interpreted_insns": 0, "unmapped": 0, "io_unhandled": 0}]}))
            new.write_text(json.dumps({"cases": [{"case": "boot", "status": "FAIL", "exit_code": 1, "dispatch_misses": 1, "interpreted_insns": 0, "unmapped": 0, "io_unhandled": 0}]}))
            result = subprocess.run([sys.executable, str(ROOT / "scripts/compare-m4-regression.py"), str(old), str(new)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("REGRESSION", result.stdout)


if __name__ == "__main__":
    unittest.main()
