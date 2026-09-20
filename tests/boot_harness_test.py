"""Negative boot authority checks and exact guest transcript expectations."""
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
from boot_context_test import expected, check_transcript


class BootTests(unittest.TestCase):
    def test_all_graph_attributes_and_structure(self):
        original = ET.parse(ROOT / "system/boot-test.system").getroot()
        with contextlib.redirect_stdout(io.StringIO()):
            validate(ROOT / "system/boot-test.system", "boot-test")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "system.xml"
            for index, element in enumerate(original.iter()):
                for attribute in element.attrib:
                    changed = copy.deepcopy(original)
                    list(changed.iter())[index].set(attribute, "unexpected")
                    ET.ElementTree(changed).write(path)
                    with self.subTest(index=index, attribute=attribute), self.assertRaises(ValueError):
                        validate(path, "boot-test")
            for index in range(len(list(original.iter()))):
                changed = copy.deepcopy(original)
                ET.SubElement(list(changed.iter())[index], "unexpected")
                ET.ElementTree(changed).write(path)
                with self.assertRaises(ValueError):
                    validate(path, "boot-test")
            result = subprocess.run([sys.executable, "-O", str(ROOT / "tools/check_system.py"),
                                     str(path), "--profile", "boot-test"], capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_normal_profiles_reject_boot_test_authority(self):
        for profile in ("seed", "terminal", "isolation"):
            with self.assertRaises(ValueError):
                validate(ROOT / "system/boot-test.system", profile)

    def test_exact_failure_and_success_evidence(self):
        self.assertEqual(expected(None, boot=3),
                         b"TCS BOOT TEST ONLY\r\nBOOT status=3\r\nTCS BOOT TEST DONE\r\n")
        context = bytes(112)
        self.assertIn(b"COMMAND duplicate=6\r\nCOMMAND replay=5\r\n", expected(context))
        self.assertNotIn(b"COMMAND admission", expected(context, transport=5))
        self.assertNotIn(b"COMMAND duplicate", expected(context, admission=4))

    def test_transcript_corruption_and_extra_output_rejected(self):
        wanted = expected(bytes(112))
        check_transcript(wanted, wanted)
        for end in range(len(wanted)):
            with self.assertRaises(RuntimeError):
                check_transcript(wanted[:end], wanted)
        for i in range(len(wanted)):
            changed = bytearray(wanted); changed[i] ^= 1
            with self.assertRaises(RuntimeError):
                check_transcript(bytes(changed), wanted)
        for changed in (b"debug\n" + wanted, wanted + wanted, wanted + b"x"):
            with self.assertRaises(RuntimeError):
                check_transcript(changed, wanted)


if __name__ == "__main__":
    unittest.main()
