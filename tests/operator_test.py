"""Exercise the actual host CLI with public RFC credentials; never real keygen."""
import concurrent.futures
import hashlib
import fcntl
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("TCS_TEST_BUILD_DIR", ROOT / "build"))
ACK = "--acknowledge-experimental"
SEED = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
KEY = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")


class OperatorTests(unittest.TestCase):
    def invoke(self, *args, tool="operator_fixture", expected=0, env=None):
        environment = {k: v for k, v in os.environ.items() if not k.startswith("TCS_TEST_")}
        environment.update(env or {})
        result = subprocess.run([str(BUILD / tool), *map(str, args)], env=environment,
                                capture_output=True, timeout=10)
        if expected == 0:
            self.assertEqual(result.returncode, 0, result.stderr.decode())
        elif expected is not None:
            self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(SEED.hex().encode(), result.stdout + result.stderr)
        self.assertNotIn(SEED, result.stdout + result.stderr)
        return result

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="tcs-operator-", dir="/tmp")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()  # /tmp is a symlink on macOS; tool refuses it.
        self.identity, self.session = self.root / "identity", self.root / "session"
        self.invoke("create", self.identity, ACK)
        self.invoke("context", self.identity, self.session, ACK)

    def review(self, operation="grant", subject="1", sequence="1", generation="0"):
        args = (operation, subject, sequence, generation)
        result = self.invoke("review", self.identity, self.session, *args)
        approval = result.stdout.decode().split("approval=")[1].strip()
        self.assertRegex(approval, r"^[0-9a-f]{64}$")
        return args, approval

    def test_identity_modes_permissions_and_no_overwrite(self):
        for directory in (self.identity, self.session):
            self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o700)
        key = self.identity / "identity.key"
        public = self.identity / "identity.pub"
        context = self.session / "launch.context"
        for path, size in ((key, 80), (public, 80), (context, 112)):
            self.assertEqual(path.stat().st_size, size)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(key.read_bytes()[48:], SEED)
        self.assertEqual(public.read_bytes()[48:], KEY)
        self.assertEqual(context.read_bytes()[80:], KEY)
        self.assertEqual(context.read_bytes()[48:80], b"B" * 32)
        before = {p: p.read_bytes() for p in (key, public, context)}
        self.invoke("create", self.identity, ACK, expected=1)
        self.invoke("context", self.identity, self.session, ACK, expected=1)
        for path, data in before.items():
            self.assertEqual(path.read_bytes(), data)
        self.invoke("create", self.root / "no-ack", tool="tcs-operator", expected=1)
        self.assertFalse((self.root / "no-ack").exists())
        self.invoke("show", self.identity, tool="tcs-operator", expected=1)
        self.invoke("context", self.identity, self.root / "no-context", ACK,
                    tool="tcs-operator", expected=1)
        self.assertFalse((self.root / "no-context").exists())
        # Re-labelling a known public key never promotes it to operator mode.
        data = bytearray(public.read_bytes()); data[15] = 0; public.write_bytes(data)
        self.invoke("show", self.identity, tool="tcs-operator", expected=1)

    def test_review_digest_and_real_signature_interoperability(self):
        operations = (("grant", 1), ("revoke", 3), ("quarantine", 4), ("restore", 5))
        # Values are independently read from the documented policy enum below.
        for sequence, (operation, op) in enumerate(operations, 1):
            args, approval = self.review(operation, "1", str(sequence), str(sequence - 1))
            self.invoke("sign", self.identity, self.session, *args, approval)
            packet = (self.session / f"request-{sequence}.bin").read_bytes()
            self.assertEqual(len(packet), 192)
            self.assertEqual(packet[:16], b"TCS-ADMIN-\x01" + bytes(5))
            self.assertEqual(packet[16:48], b"T" * 32)
            self.assertEqual(packet[48:80], b"B" * 32)
            self.assertEqual(struct.unpack("<6Q", packet[80:128]),
                             (sequence, op, 1, 42 if op == 1 else 0, 1 if op == 1 else 0, sequence - 1))
            preimage = b"TCS-REVIEW\x01" + bytes(4) + b"\x01" + KEY + packet[:128]
            self.assertEqual(approval, hashlib.blake2b(preimage, digest_size=32).hexdigest())
            check = subprocess.run([str(BUILD / "operator_verify")], capture_output=True, timeout=10,
                                   input=(self.session / "launch.context").read_bytes() + packet)
            self.assertEqual(check.returncode, 0, check.stderr)
            self.assertEqual(check.stdout.decode().strip(), f"{op} 1 {sequence} {sequence - 1}")
            damaged = bytearray(packet); damaged[-1] ^= 1
            check = subprocess.run([str(BUILD / "operator_verify")], capture_output=True, timeout=10,
                                   input=(self.session / "launch.context").read_bytes() + damaged)
            self.assertNotEqual(check.returncode, 0)

    def test_approval_binds_command_and_context(self):
        args, approval = self.review()
        for changed in (("revoke", "1", "1", "0"), ("grant", "2", "1", "0"),
                        ("grant", "1", "2", "0"), ("grant", "1", "1", "1")):
            self.invoke("sign", self.identity, self.session, *changed, approval, expected=1)
        for bad in ("", "0" * 64, approval.upper(), approval[:-1], approval + "0"):
            self.invoke("sign", self.identity, self.session, *args, bad, expected=1)
        other = self.root / "other-session"
        self.invoke("context", self.identity, other, ACK, env={"TCS_TEST_SECOND_BOOT": "1"})
        self.invoke("sign", self.identity, other, *args, approval, expected=1)
        self.assertEqual(list(self.session.glob("request-*")), [])
        self.assertEqual(list(other.glob("request-*")), [])

    def test_private_seed_and_public_half_must_match(self):
        args, approval = self.review()
        path = self.identity / "identity.key"; original = path.read_bytes()
        for offset in (0, 15, 16, 48, 79):
            data = bytearray(original); data[offset] ^= 1; path.write_bytes(data)
            self.invoke("sign", self.identity, self.session, *args, approval, expected=1)
        path.write_bytes(original)
        self.assertEqual(list(self.session.glob("request-*")), [])
        self.invoke("sign", self.identity, self.session, *args, approval)

    def test_canonical_commands_and_integer_boundaries(self):
        for bad in ("-1", "+1", "01", "", "1 ", " 1", "1x", "0x1", "18446744073709551616", "9" * 21):
            for position in range(1, 4):
                args = ["grant", "1", "1", "0"]; args[position] = bad
                self.invoke("review", self.identity, self.session, *args, expected=1)
        for args in (("check", "1", "1", "0"), ("GRANT", "1", "1", "0"),
                     ("grant", "4", "1", "0"), ("grant", "1", "0", "0")):
            self.invoke("review", self.identity, self.session, *args, expected=1)
        args, approval = self.review("quarantine", "0", str(2**64 - 1), str(2**64 - 1))
        self.invoke("sign", self.identity, self.session, *args, approval)
        self.assertTrue((self.session / f"request-{2**64 - 1}.bin").is_file())

    def test_untrusted_context_headers_lengths_and_bindings(self):
        path = self.session / "launch.context"; original = path.read_bytes()
        variants = [original[:-1], original + b"x", bytes(len(original))]
        for offset in (0, 10, 15, 16, 80):
            data = bytearray(original); data[offset] ^= 1; variants.append(data)
        variants += [original[:48] + bytes(32) + original[80:]]
        for data in variants:
            path.write_bytes(data)
            self.invoke("review", self.identity, self.session, "grant", "1", "1", "0", expected=1)
        path.write_bytes(original)
        self.review()
        # Public-only show/context/review never need to open the private seed.
        (self.identity / "identity.key").unlink()
        self.invoke("show", self.identity)
        args, approval = self.review()
        self.invoke("sign", self.identity, self.session, *args, approval, expected=1)

    def test_file_modes_links_fifo_and_path_components(self):
        public = self.identity / "identity.pub"
        self.identity.chmod(0o755); self.invoke("show", self.identity, expected=1); self.identity.chmod(0o700)
        for mode in (0o644, 0o640, 0o400, 0o660):
            public.chmod(mode); self.invoke("show", self.identity, expected=1)
        public.chmod(0o600)
        link = self.root / "alias"; link.symlink_to(self.identity, target_is_directory=True)
        self.invoke("show", link, expected=1)
        self.invoke("create", link / "new", ACK, expected=1)
        self.assertFalse((self.identity / "new").exists())
        for path in (str(self.identity) + "/", str(self.identity) + "/../identity", str(self.root) + "//identity"):
            self.invoke("show", path, expected=1)
        self.invoke("show", "relative-directory", expected=1)
        for marker in ("directory", "worktree-file"):
            checkout = self.root / marker; checkout.mkdir(mode=0o700)
            if marker == "directory":
                (checkout / ".git").mkdir()
            else:
                (checkout / ".git").write_text("gitdir: elsewhere\n")
            self.invoke("create", checkout / "identity", ACK, expected=1)
            self.assertFalse((checkout / "identity").exists())
        link.unlink(); os.link(public, link)
        self.invoke("show", self.identity, expected=1); link.unlink()
        original = self.identity / "saved-public"; public.rename(original)
        public.symlink_to(original); self.invoke("show", self.identity, expected=1); public.unlink()
        os.mkfifo(public, 0o600); self.invoke("show", self.identity, expected=1); public.unlink()
        original.rename(public); self.invoke("show", self.identity)

    def test_extended_acl_is_rejected(self):
        public = self.identity / "identity.pub"
        if sys.platform == "darwin":
            subprocess.run(["/bin/chmod", "+a", "everyone allow read", str(public)], check=True, capture_output=True)
        elif sys.platform.startswith("linux"):
            entries = [(1, 6, 0xffffffff), (2, 4, os.getuid() + 1), (4, 0, 0xffffffff),
                       (16, 0, 0xffffffff), (32, 0, 0xffffffff)]
            acl = struct.pack("<I", 2) + b"".join(struct.pack("<HHI", *e) for e in entries)
            os.setxattr(public, "system.posix_acl_access", acl)
        else:
            self.fail("Unsupported host platform")
        self.assertEqual(stat.S_IMODE(public.stat().st_mode), 0o600)
        self.invoke("show", self.identity, expected=1)

    def test_entropy_failure_and_exclusive_sequence_output(self):
        self.invoke("create", self.root / "failed-id", ACK, expected=1, env={"TCS_TEST_ENTROPY_ERROR": "1"})
        self.invoke("context", self.identity, self.root / "failed-session", ACK,
                    expected=1, env={"TCS_TEST_ENTROPY_ERROR": "1"})
        self.assertFalse((self.root / "failed-id").exists())
        self.assertFalse((self.root / "failed-session").exists())
        args, approval = self.review()
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda _: self.invoke("sign", self.identity, self.session, *args,
                                                        approval, expected=None), range(4)))
        self.assertEqual(sum(r.returncode == 0 for r in results), 1)
        path = self.session / "request-1.bin"; packet = path.read_bytes()
        self.invoke("sign", self.identity, self.session, *args, approval, expected=1)
        self.assertEqual(path.read_bytes(), packet)
        path.write_bytes(b"partial")
        self.invoke("sign", self.identity, self.session, *args, approval, expected=1)
        self.assertEqual(path.read_bytes(), b"partial")
        path.unlink(); path.symlink_to(self.identity / "identity.key")
        before = (self.identity / "identity.key").read_bytes()
        self.invoke("sign", self.identity, self.session, *args, approval, expected=1)
        self.assertEqual((self.identity / "identity.key").read_bytes(), before)

    def test_short_write_and_uncertain_output_fail_closed(self):
        args, approval = self.review()
        self.invoke("sign", self.identity, self.session, *args, approval, env={"TCS_TEST_SHORT_WRITE": "1"})
        self.assertEqual((self.session / "request-1.bin").stat().st_size, 192)
        for sequence, fault, expected_size in (("2", "TCS_TEST_WRITE_FAIL", 7), ("3", "TCS_TEST_FSYNC_FAIL", 192)):
            args, approval = self.review(sequence=sequence)
            self.invoke("sign", self.identity, self.session, *args, approval, expected=1, env={fault: "1"})
            path = self.session / f"request-{sequence}.bin"
            self.assertEqual(path.stat().st_size, expected_size)
            before = path.read_bytes()
            self.invoke("sign", self.identity, self.session, *args, approval, expected=1)
            self.assertEqual(path.read_bytes(), before)

    def test_one_shot_launch_and_descriptor_boundary(self):
        image = self.root / "image.img"; image.write_bytes(b"IMAGE")
        probe = (BUILD / "launch_probe").resolve()
        command = [str(BUILD / "operator_fixture"), "launch", str(self.identity), str(self.session), str(image), str(probe), ACK]
        with image.open("rb") as source:
            extra = fcntl.fcntl(source.fileno(), fcntl.F_DUPFD, 500)
            try:
                result = subprocess.run(command, capture_output=True, timeout=10, pass_fds=(extra,))
            finally:
                os.close(extra)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"no extra inherited descriptors", result.stdout)
        used = self.session / "launch.used"
        self.assertEqual(used.read_bytes(), (self.session / "launch.context").read_bytes())
        self.assertEqual(stat.S_IMODE(used.stat().st_mode), 0o600)
        snapshot = self.session / "launch.img"
        self.assertEqual(snapshot.read_bytes(), b"IMAGE")
        self.assertEqual(stat.S_IMODE(snapshot.stat().st_mode), 0o600)
        image.write_bytes(b"CHANGED")
        self.assertEqual(snapshot.read_bytes(), b"IMAGE")
        image.write_bytes(b"IMAGE")
        self.invoke("launch", self.identity, self.session, image, probe, ACK, expected=1)
        # Even a failed exec consumes a fresh session; no resume/reset switch.
        other = self.root / "failed-exec"; self.invoke("context", self.identity, other, ACK)
        self.invoke("launch", self.identity, other, image, self.root / "missing-qemu", ACK, expected=1)
        self.assertTrue((other / "launch.used").exists())
        self.invoke("launch", self.identity, other, image, probe, ACK, expected=1)
        collision = self.root / "image-collision"
        self.invoke("context", self.identity, collision, ACK)
        (collision / "launch.img").write_bytes(b"PRESERVE")
        self.invoke("launch", self.identity, collision, image, probe, ACK, expected=1)
        self.assertEqual((collision / "launch.img").read_bytes(), b"PRESERVE")
        self.assertTrue((collision / "launch.used").exists())

    def test_concurrent_launch_and_failed_reservation(self):
        image = self.root / "image.img"; image.write_bytes(b"IMAGE")
        probe = (BUILD / "launch_probe").resolve()
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda _: self.invoke("launch", self.identity, self.session, image,
                                                        probe, ACK, expected=None), range(4)))
        self.assertEqual(sum(r.returncode == 0 for r in results), 1)
        for flag, length in (("TCS_TEST_WRITE_FAIL", 7), ("TCS_TEST_FSYNC_FAIL", 112)):
            path = self.root / flag; self.invoke("context", self.identity, path, ACK)
            result = self.invoke("launch", self.identity, path, image, probe, ACK, expected=1, env={flag: "1"})
            self.assertNotIn(b"PASS exact QEMU", result.stdout)
            self.assertEqual((path / "launch.used").stat().st_size, length)
            self.invoke("launch", self.identity, path, image, probe, ACK, expected=1)
        for bad in ("relative", str(image) + ",bad=on", str(image) + "\n"):
            self.invoke("launch", self.identity, self.session, bad, probe, ACK, expected=1)


if __name__ == "__main__":
    unittest.main()
