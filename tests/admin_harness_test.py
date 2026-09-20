"""Private admin graph mutations and strict scenario evidence parsing."""
import contextlib
import copy
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_system import validate
from admin_boot_test import expected_boot, consume_expected


class AdminHarnessTests(unittest.TestCase):
    def test_exact_graph_and_mutations(self):
        original = ET.parse(ROOT / "system/admin-test.system").getroot()
        with contextlib.redirect_stdout(io.StringIO()):
            validate(ROOT / "system/admin-test.system", "admin-test")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "system.xml"
            for index, element in enumerate(original.iter()):
                for attribute in element.attrib:
                    root = copy.deepcopy(original); list(root.iter())[index].set(attribute, "unexpected")
                    ET.ElementTree(root).write(path)
                    with self.subTest(index=index, attribute=attribute), self.assertRaises(ValueError):
                        validate(path, "admin-test")
                root = copy.deepcopy(original); ET.SubElement(list(root.iter())[index], "unexpected")
                ET.ElementTree(root).write(path)
                with self.assertRaises(ValueError):
                    validate(path, "admin-test")
            result = subprocess.run([sys.executable, "-O", str(ROOT / "tools/check_system.py"),
                                     str(path), "--profile", "admin-test"], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
        for profile in ("seed", "terminal", "isolation", "boot-test"):
            with self.assertRaises(ValueError):
                validate(ROOT / "system/admin-test.system", profile)

    def test_partial_and_corrupt_transcripts(self):
        expected = expected_boot()
        for i in range(len(expected)):
            pending = bytearray(expected[:i]); self.assertFalse(consume_expected(pending, expected))
            pending.extend(expected[i:] + b"next"); self.assertTrue(consume_expected(pending, expected))
            self.assertEqual(pending, b"next")
            changed = bytearray(expected); changed[i] ^= 1
            with self.assertRaises(RuntimeError):
                consume_expected(changed, expected)
        for prefix in (b"FAIL", b"TCS ADMIN IPC FAIL", b"debug\n", b"\n"):
            with self.assertRaises(RuntimeError):
                consume_expected(bytearray(prefix + expected), expected)


if __name__ == "__main__":
    unittest.main()
