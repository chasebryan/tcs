"""A pass marker or timeout is not evidence of a memory-protection fault."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from terminal_boot_test import consume_boot, consume_isolation


def evidence():
    addresses = (0x09000018, 0x04000000, 0x05000000, 0x05000000, 0x06000000, 0x07000000)
    lines = ["TCS ISOLATION BEGIN release-kernel cases=6"]
    for child, address in enumerate(addresses, 1):
        execute, write = child == 6, child in (2, 4, 5)
        ip = address if execute else 0x200004
        fsr = ((0x20 if execute else 0x24) << 26) | (1 << 25) | (write << 6) | (15 if child >= 5 else 6)
        lines.append(f"FAULT PASS child={child} label=6 words=4 ip={ip} address={address} instruction={int(execute)} fsr={fsr}")
    lines.append("TCS ISOLATION PASS cases=6 canary=intact policy=restricted")
    return ("\n".join(lines) + "\n").encode()


class IsolationHarnessTests(unittest.TestCase):
    def test_every_split_waits_for_all_evidence(self):
        data = evidence()
        for split in range(len(data)):
            pending = bytearray(data[:split])
            self.assertFalse(consume_isolation(pending))
            self.assertEqual(pending, data[:split])
            pending.extend(data[split:] + b"next")
            self.assertTrue(consume_isolation(pending))
            self.assertEqual(pending, b"next")

    def test_fault_evidence_mutations_fail(self):
        original = evidence()
        changes = [
            (b"child=2", b"child=1"), (b"child=1", b"child=0"),
            (b"label=6", b"label=5"), (b"words=4", b"words=3"),
            (b"ip=2097156", b"ip=0"), (b"ip=2097156", b"ip=2097157"),
            (b"address=150994968", b"address=150994969"),
            (b"instruction=0", b"instruction=1"),
            (b"fsr=2449473542", b"fsr=2449473537"),
            (b"fsr=2449473542", b"fsr=2449473606"),
            (b"canary=intact", b"canary=changed"),
            (b"policy=restricted", b"policy=active"),
            (b"FAULT PASS", b"TCS ISOLATION FAIL"),
        ]
        for before, after in changes:
            with self.subTest(before=before, after=after):
                self.assertIn(before, original)
                with self.assertRaises(RuntimeError):
                    consume_isolation(bytearray(original.replace(before, after, 1)))

    def test_missing_faults_and_pass_only_never_succeed(self):
        lines = evidence().splitlines(keepends=True)
        for child in range(1, 7):
            data = bytearray(b"".join(lines[:child] + lines[child + 1:]))
            self.assertFalse(consume_isolation(data))
            data.extend(b"TCS TERMINAL READY (release-kernel, read-only)\ntcs> ")
            with self.assertRaises(RuntimeError):
                consume_isolation(data)
        with self.assertRaises(RuntimeError):
            consume_isolation(bytearray(lines[-1]))
        self.assertFalse(consume_isolation(bytearray()))

    def test_test_image_rejected_by_normal_release_harness(self):
        data = bytearray(evidence() + b"TCS TERMINAL READY (release-kernel, read-only)\ntcs> ")
        with self.assertRaisesRegex(RuntimeError, "before release"):
            consume_boot(data, "release")


if __name__ == "__main__":
    unittest.main()
