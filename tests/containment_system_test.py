"""The lifecycle experiment must never leak parent/device authority to normal profiles."""
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


class ContainmentSystemTests(unittest.TestCase):
    def test_explicit_fixture_and_hosted_guards(self):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        for flags, accepted in (([], False), (["-DTCS_CONTAINMENT_TEST_PROFILE=1"], True),
                                (["-DTCS_CONTAINMENT_TEST_PROFILE=1", "-ffreestanding"], False),
                                (["-DTCS_CONTAINMENT_TEST_PROFILE=1", "-DTCS_DEBUG_PROFILE=1"], False)):
            with self.subTest(flags=flags):
                result = subprocess.run([*compiler, "-std=c11", "-Iinclude", "-Itests/containment",
                    "-Itests/lifecycle/support", *flags, "-E", "-x", "c", "-"],
                    input='#include "runtime.h"\n', cwd=ROOT, text=True, capture_output=True, timeout=10)
                self.assertEqual(result.returncode == 0, accepted, result.stderr)

    def test_exact_profile_and_normal_exclusion(self):
        with contextlib.redirect_stdout(io.StringIO()):
            validate(ROOT / "system/containment-test.system", "containment-test")
        for profile, name in (("seed", "tcs"), ("terminal", "terminal"), ("isolation", "isolation"),
                              ("boot-test", "boot-test"), ("admin-test", "admin-test"), ("interactive", "interactive"),
                              ("lifecycle-test", "lifecycle-test")):
            with self.subTest(profile=profile):
                with self.assertRaises(ValueError): validate(ROOT / "system/containment-test.system", profile)
                with self.assertRaises(ValueError): validate(ROOT / "system" / (name + ".system"), "containment-test")

    def test_every_element_attribute_and_extra_authority(self):
        original = ET.parse(ROOT / "system/containment-test.system").getroot()
        count = 0
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mutated.system"
            for index, element in enumerate(original.iter()):
                changes = [("extra attribute", lambda e: e.set("extra", "true")),
                           ("extra element", lambda e: ET.SubElement(e, "protection_domain")),
                           ("text", lambda e: setattr(e, "text", "unexpected"))]
                for key in element.attrib:
                    changes.extend((("missing " + key, lambda e, k=key: e.attrib.pop(k)),
                                    ("changed " + key, lambda e, k=key: e.set(k, "unexpected"))))
                for name, change in changes:
                    with self.subTest(element=index, change=name):
                        root = copy.deepcopy(original)
                        change(list(root.iter())[index])
                        ET.ElementTree(root).write(path)
                        with self.assertRaises(ValueError): validate(path, "containment-test")
                        count += 1
            result = subprocess.run([sys.executable, "-O", str(ROOT / "tools/check_system.py"),
                str(path), "--profile", "containment-test"], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unexpected containment-test authority", result.stderr)
        print(f"PASS containment authority graph: {count} element/attribute mutations rejected")

    def test_build_is_separate_and_supervisor_has_only_model_dependency(self):
        with tempfile.TemporaryDirectory(prefix="tcs-runtime-plan-") as directory:
            result = subprocess.run(["make", "-n", "containment-image", "BUILD_DIR=" + directory,
                "MICROKIT_SDK=/tcs-sdk", "ZIG=/tcs-zig"], cwd=ROOT, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            commands = [shlex.split(line) for line in result.stdout.splitlines()
                        if line.startswith('"/tcs-zig" cc ')]
            links = {Path(c[-1]).name: c for c in commands if "-c" not in c}
            self.assertEqual(set(links), {"observer.elf", "supervisor.elf", "broker.elf", "caller.elf", "worker_a.elf", "worker_b.elf", "serial.elf"})
            for name, command in links.items():
                self.assertIn("-DTCS_RELEASE_PROFILE=1", command)
                self.assertFalse(any("/debug/" in a or "host-sanitized" in a for a in command))
                model = [a for a in command if a.endswith("/lifecycle_model.o")]
                self.assertEqual(len(model), int(name == "supervisor.elf"))
                reduction = [a for a in command if a.endswith("/reduction_model.o")]
                self.assertEqual(len(reduction), int(name == "supervisor.elf"))
            fixture_compiles = [c for c in commands if "-c" in c and any(a.startswith("tests/containment/") for a in c)]
            self.assertEqual(len(fixture_compiles), 5)
            for command in fixture_compiles:
                self.assertIn("-DTCS_CONTAINMENT_TEST_PROFILE=1", command)


if __name__ == "__main__":
    unittest.main()
