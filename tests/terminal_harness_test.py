"""The emulator harness must distinguish release output from debug evidence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from terminal_boot_test import consume_boot, denied_output


class TerminalBootTests(unittest.TestCase):
    def test_split_banner_preserves_pending(self):
        for profile in ("debug", "release"):
            banner = f"TCS TERMINAL READY ({profile}-kernel, read-only)\ntcs> ".encode()
            for split in range(len(banner)):
                pending = bytearray(banner[:split])
                self.assertFalse(consume_boot(pending, profile))
                self.assertEqual(pending, banner[:split])
                pending.extend(banner[split:] + b"next")
                self.assertTrue(consume_boot(pending, profile))
                self.assertEqual(pending, b"next")

    def test_release_preamble_is_rejected(self):
        for prefix in (b"kernel output\n", b"TCS audit ready\n", b" ", b"\n"):
            with self.subTest(prefix=prefix):
                with self.assertRaisesRegex(RuntimeError, "before release"):
                    consume_boot(bytearray(prefix + b"TCS TERMINAL READY (release-kernel, read-only)\ntcs> "), "release")

    def test_wrong_profile_is_rejected(self):
        for requested, actual in (("debug", "release"), ("release", "debug")):
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                consume_boot(bytearray(f"TCS TERMINAL READY ({actual}-kernel, read-only)\n".encode()), requested)

    def test_debug_preamble_is_allowed_but_faults_are_not(self):
        banner = b"TCS TERMINAL READY (debug-kernel, read-only)\ntcs> "
        self.assertTrue(consume_boot(bytearray(b"boot diagnostics\n" + banner), "debug"))
        with self.assertRaisesRegex(RuntimeError, "Runtime fault"):
            consume_boot(bytearray(b"MON|ERROR fault\n" + banner), "debug")

    def test_only_debug_expects_audit_output(self):
        self.assertEqual(denied_output("release", 4), b"READ DENIED status=1\ntcs> ")
        self.assertEqual(denied_output("debug", 4), b"TCS audit decision 4\nREAD DENIED status=1\ntcs> ")


if __name__ == "__main__":
    unittest.main()
