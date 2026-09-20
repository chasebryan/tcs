"""Compile/preprocess the profile guards independently of the downloaded SDK."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DEBUG = ["TCS_DEBUG_PROFILE", "CONFIG_DEBUG_BUILD", "CONFIG_PRINTING"]
RELEASE = ["TCS_RELEASE_PROFILE", "CONFIG_VERIFICATION_BUILD"]


class BuildProfileTests(unittest.TestCase):
    def preprocess(self, flags, extra=()):
        return subprocess.run(
            shlex.split(os.environ.get("HOST_CC", "cc")) +
            ["-E", "-P", "-x", "c", "-I" + str(ROOT / "include")] +
            ["-D" + flag + "=1" for flag in flags] + list(extra) + ["-"],
            input='#include "tcs/build_profile.h"\nTCS_PROFILE_NAME\n',
            text=True, capture_output=True, timeout=10)

    def test_correct_profiles(self):
        for flags, name in ((DEBUG, "debug-kernel"), (RELEASE, "release-kernel"),
                            (["TCS_HOST_TEST", "TCS_TEST_MICROKIT_H"], "host-test")):
            with self.subTest(name=name):
                result = self.preprocess(flags)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), '"' + name + '"')

    def test_mismatched_or_missing_profiles_fail(self):
        cases = [[], DEBUG[1:], RELEASE[1:], ["TCS_DEBUG_PROFILE"],
                 ["TCS_RELEASE_PROFILE"], ["TCS_HOST_TEST"],
                 DEBUG + ["TCS_RELEASE_PROFILE"], RELEASE + ["TCS_DEBUG_PROFILE"],
                 DEBUG + ["TCS_HOST_TEST", "TCS_TEST_MICROKIT_H"]]
        cases += [RELEASE + [flag] for flag in ("CONFIG_DEBUG_BUILD", "CONFIG_PRINTING")]
        cases += [[flag for flag in DEBUG if flag != missing]
                  for missing in ("CONFIG_DEBUG_BUILD", "CONFIG_PRINTING")]
        for flags in cases:
            with self.subTest(flags=flags):
                result = self.preprocess(flags)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("error", result.stderr.lower())

    def test_freestanding_cannot_use_host_fixture(self):
        result = self.preprocess(["TCS_HOST_TEST", "TCS_TEST_MICROKIT_H"], ["-ffreestanding"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("hosted IPC fixture", result.stderr)

    def test_build_plan_keeps_all_release_links_separate(self):
        # Static pattern rules matter on older Make: a broad debug %.elf rule
        # can otherwise also match release/client.elf and link the wrong library.
        with tempfile.TemporaryDirectory(prefix="tcs-plan-") as directory:
            for target, profile in (("terminal-image", "debug"),
                                    ("terminal-release-image", "release")):
                with self.subTest(profile=profile):
                    result = subprocess.run(
                        ["make", "-n", target, "BUILD_DIR=" + directory,
                         "MICROKIT_SDK=/tcs-sdk", "ZIG=/tcs-zig"],
                        cwd=ROOT, text=True, capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    links = [line for line in result.stdout.splitlines() if "-lmicrokit" in line]
                    self.assertEqual(len(links), 6)
                    for line in links:
                        self.assertIn("/" + profile + "/lib", line)
                        self.assertIn("-DTCS_" + profile.upper() + "_PROFILE=1", line)
                        other = "debug" if profile == "release" else "release"
                        self.assertNotIn("/" + other + "/lib", line)
                    self.assertIn("--config " + profile, result.stdout)

    def test_ambiguous_config_override_fails(self):
        result = subprocess.run(["make", "-n", "terminal-image", "CONFIG=release"],
                                cwd=ROOT, text=True, capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("separate release profile", result.stderr)


if __name__ == "__main__":
    unittest.main()
