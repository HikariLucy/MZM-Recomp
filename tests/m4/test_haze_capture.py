import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "capture_m4_first_haze", ROOT / "scripts/capture-m4-first-haze.py")
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    def test_match_event_requires_verified_native_hit(self):
        line = ("mzm_ram_dispatch kind=haze variant=Haze_Bg3 "
                "runtime_pc=0x03001944 source_pc=0x0805d768 "
                "match=1 native=1 hits=1")
        self.assertEqual(capture.HIT.fullmatch(line).group(1), "Haze_Bg3")
        self.assertIsNone(capture.HIT.fullmatch(line.replace("native=1", "native=0")))
        self.assertIsNone(capture.HIT.fullmatch(line.replace("match=1", "match=0")))
        self.assertIsNone(capture.HIT.fullmatch(line.replace("hits=1", "hits=2")))

    def test_checkpoint_path_must_be_local(self):
        self.assertEqual(
            capture.local_path(".local/m4-checkpoints/haze-bg3.state"),
            ROOT / ".local/m4-checkpoints/haze-bg3.state")
        with self.assertRaises(ValueError):
            capture.local_path("/tmp/not-a-local-checkpoint.state")

    def test_savestate_request_uses_existing_observer_command(self):
        class FakeSocket:
            def __init__(self):
                self.sent = b""

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return None

            def settimeout(self, value):
                pass

            def sendall(self, data):
                self.sent = data

            def recv(self, size):
                return b'{"ok":true,"saved":"local"}\n'

        sock = FakeSocket()
        with patch.object(capture.socket, "create_connection", return_value=sock):
            self.assertTrue(capture.request_savestate(19844, ROOT / ".local/m4-checkpoints/test.state")["ok"])
        self.assertIn(b'"cmd": "savestate_save"', sock.sent)
        self.assertIn(b'"path":', sock.sent)


if __name__ == "__main__":
    unittest.main()
