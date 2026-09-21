"""Native crypto object reuse, sanitizer separation, and incremental plans."""
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROGRAMS = ("admin_test", "admin_ipc_test", "launch_admin_ipc_test", "boot_fixture",
            "admin_fixture", "operator_fixture", "operator_verify")
SOURCES = ("third_party/monocypher/monocypher.c", "third_party/monocypher/monocypher-ed25519.c")
SANITIZE = "-fsanitize=address,undefined"


class HostBuildTests(unittest.TestCase):
    def plan(self, directory, *targets, extra=()):
        result = subprocess.run(["make", "-n", *extra, *targets, "BUILD_DIR=" + str(directory),
            "HOST_CC=tcs-host-compiler", "MICROKIT_SDK=/tcs-sdk", "ZIG=/tcs-zig"],
            cwd=ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return [shlex.split(line) for line in result.stdout.splitlines()
                if line.startswith("tcs-host-compiler ")]

    def test_one_pair_shared_by_seven_sanitized_programs_only(self):
        with tempfile.TemporaryDirectory(prefix="tcs-host-plan-") as directory:
            targets = [str(Path(directory) / p) for p in (*PROGRAMS, "tcs-operator")]
            commands = self.plan(directory, *targets)
            objects = [c for c in commands if "-c" in c]
            self.assertEqual(len(objects), 2)
            for source in SOURCES:
                matches = [c for c in objects if source in c]
                self.assertEqual(len(matches), 1)
                command = matches[0]
                self.assertIn(source, command)
                self.assertIn(SANITIZE, command)
                self.assertIn("-Werror", command)
                self.assertNotIn("-target", command)
                self.assertFalse(any(arg.startswith("-DTCS_") for arg in command))
                self.assertEqual(command[-1], str(Path(directory) / "host-sanitized" / (Path(source).stem + ".o")))
            applications = {Path(c[-1]).name: c for c in commands if "-c" not in c}
            self.assertEqual(set(applications), {*PROGRAMS, "tcs-operator"})
            for name in PROGRAMS:
                command = applications[name]
                self.assertIn(SANITIZE, command)
                for source in SOURCES:
                    self.assertNotIn(source, command)
                    self.assertIn(str(Path(directory) / "host-sanitized" / (Path(source).stem + ".o")), command)
            for name, flag in (("operator_fixture", "-DTCS_OPERATOR_TEST_ONLY"),
                               ("admin_fixture", "-DTCS_ADMIN_SCENARIO"),
                               ("launch_admin_ipc_test", "-DTCS_LAUNCH_IPC_TEST")):
                self.assertIn(flag, applications[name])
            operator = applications["tcs-operator"]
            self.assertNotIn(SANITIZE, operator)
            self.assertFalse(any("host-sanitized" in a or a.startswith("-DTCS_") for a in operator))
            for source in SOURCES:
                self.assertIn(source, operator)

    def test_guest_targets_never_depend_on_host_objects(self):
        with tempfile.TemporaryDirectory(prefix="tcs-host-plan-") as directory:
            for target in ("image", "terminal-image", "terminal-release-image", "isolation-image",
                           "boot-test-image", "admin-test-image", "interactive-image", "interactive-fixture-image"):
                with self.subTest(target=target):
                    self.assertEqual(self.plan(directory, target), [])

    def test_parallel_creation_reuse_and_dependency_invalidation(self):
        # A fake compiler records build scheduling without executing a binary or
        # recompiling crypto during the native test suite itself.
        with tempfile.TemporaryDirectory(prefix="tcs-host-incremental-") as directory:
            root = Path(directory); build = root / "build"
            compiler = root / "compiler.py"; log = root / "calls.jsonl"
            compiler.write_text('import json, sys\nfrom pathlib import Path\n'
                'args = sys.argv[1:]\n'
                'with Path(__file__).with_name("calls.jsonl").open("a") as log:\n'
                '    log.write(json.dumps(args) + "\\n")\n'
                'Path(args[args.index("-o") + 1]).write_bytes(b"build scheduling fixture")\n')
            targets = [str(build / p) for p in (*PROGRAMS, "tcs-operator")]
            command = ["make", "-j4", *targets, "BUILD_DIR=" + str(build),
                       "HOST_CC=" + shlex.quote(sys.executable) + " " + shlex.quote(str(compiler))]
            for _ in range(2):
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr)
                calls = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual(len(calls), 10, "repeat build must not compile again")
                self.assertEqual(sum("-c" in c for c in calls), 2)
            # -W is Make's non-mutating 'assume this prerequisite changed'.
            app_only = self.plan(build, *targets, extra=("-W", "tests/operator_entropy.c"))
            self.assertEqual(len(app_only), 1)
            self.assertEqual(Path(app_only[0][-1]).name, "operator_fixture")
            crypto = self.plan(build, *targets, extra=("-W", SOURCES[0]))
            self.assertEqual(len(crypto), 9)
            self.assertEqual(sum("-c" in c for c in crypto), 1)
            header = self.plan(build, *targets, extra=("-W", "third_party/monocypher/monocypher-ed25519.h"))
            self.assertEqual(len(header), 10)
            self.assertEqual(sum("-c" in c for c in header), 2)
            recipe = self.plan(build, *targets, extra=("-W", "Makefile"))
            self.assertEqual(len(recipe), 9)
            self.assertEqual(sum("-c" in c for c in recipe), 2)


if __name__ == "__main__":
    unittest.main()
