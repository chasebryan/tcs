"""Interactive authority graph, build separation, and transcript rejection."""
import contextlib
import copy
import io
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_system import validate
from interactive_boot_test import consume, receipt


class InteractiveTests(unittest.TestCase):
    def test_exact_graph_and_mutations(self):
        source = ROOT / "system/interactive.system"
        original = ET.parse(source).getroot()
        with contextlib.redirect_stdout(io.StringIO()):
            validate(source, "interactive")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "system.xml"
            for index, element in enumerate(original.iter()):
                for attribute in element.attrib:
                    root = copy.deepcopy(original)
                    list(root.iter())[index].set(attribute, "unexpected")
                    ET.ElementTree(root).write(path)
                    with self.subTest(index=index, attribute=attribute), self.assertRaises(ValueError):
                        validate(path, "interactive")
                root = copy.deepcopy(original)
                ET.SubElement(list(root.iter())[index], "unexpected")
                ET.ElementTree(root).write(path)
                with self.assertRaises(ValueError):
                    validate(path, "interactive")
            result = subprocess.run([sys.executable, "-O", str(ROOT / "tools/check_system.py"),
                str(path), "--profile", "interactive"], capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
        for profile in ("seed", "terminal", "isolation", "boot-test", "admin-test"):
            with self.assertRaises(ValueError):
                validate(source, profile)
        with self.assertRaises(ValueError):
            validate(ROOT / "system/admin-test.system", "interactive")

    def test_explicit_modes_and_build_separation(self):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        for mode, name in ((None, None), (0, "experimental-operator"),
                           (1, "PUBLIC-FIXTURE-ONLY"), (2, None), (-1, None)):
            result = subprocess.run(compiler + ["-E", "-P", "-x", "c", "-I" + str(ROOT / "include")] +
                ([] if mode is None else ["-DTCS_LAUNCH_MODE=" + str(mode)]) + ["-"],
                input='#include "tcs/launch_profile.h"\nTCS_LAUNCH_NAME\n',
                capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode == 0, name is not None, result.stderr)
            if name:
                self.assertTrue(result.stdout.strip().endswith('"' + name + '"'))
        with tempfile.TemporaryDirectory() as directory:
            for target, mode, location in (("interactive-image", 0, "operator"),
                ("interactive-fixture-image", 1, "interactive-fixture")):
                result = subprocess.run(["make", "-n", target, "BUILD_DIR=" + directory,
                    "MICROKIT_SDK=/tcs-sdk", "ZIG=/tcs-zig"], cwd=ROOT, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                links = [line for line in result.stdout.splitlines() if "-lmicrokit" in line]
                self.assertEqual(len(links), 10)
                for line in links:
                    self.assertIn("/release/lib", line)
                    self.assertNotIn("/debug/lib", line)
                self.assertIn("-DTCS_LAUNCH_MODE=" + str(mode), result.stdout)
                self.assertNotIn("-DTCS_LAUNCH_MODE=" + str(1-mode), result.stdout)
                self.assertIn(directory + "/" + location + "/interactive.img", result.stdout)
                self.assertNotIn("admin_client.elf", result.stdout)
                self.assertNotIn("admin_boot.elf", result.stdout)

    def test_partial_and_corrupt_receipts(self):
        expected = receipt(1, 1, 1) + b"tcs> "
        for i in range(len(expected)):
            pending = bytearray(expected[:i])
            self.assertIsNone(consume(pending, (expected,)))
            pending.extend(expected[i:] + b"next")
            self.assertEqual(consume(pending, (expected,)), expected)
            self.assertEqual(pending, b"next")
            changed = bytearray(expected); changed[i] ^= 1
            with self.assertRaises(RuntimeError):
                consume(changed, (expected,))
        for prefix in (b"FAIL", b"debug\n", b"\n"):
            with self.assertRaises(RuntimeError):
                consume(bytearray(prefix + expected), (expected,))


if __name__ == "__main__":
    unittest.main()
