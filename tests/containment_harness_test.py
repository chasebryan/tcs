"""Reject contradictory hung-broker/stop evidence, not just missing PASS text."""
from dataclasses import replace
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from containment_boot_test import State, parse_state, check_state, permitted_intermediate, MAX


def record(s):
    return b"STATE " + b" ".join(str(n).encode() for n in (s.step, *s.supervisor, *s.counters, s.probes))


def fixture(step):
    zero = (0,) * 6
    a = zero if step == 0 else (2, 1, 0, 0, 0, 0) if step == 1 else (
        (3, 1, 1, 1, 0, 0) if step < 4 else (4, 1, 1, 1, 1, 0))
    return State(step, (0, int(step >= 4), 0, int(step > 0), *a, *zero),
        (100 if step >= 2 else 0, 0, 50 if step >= 3 else 0,
         0 if step < 2 else 1 if step == 2 else 2, int(step >= 4)), 7 if step == 5 else 0)


class ContainmentHarnessTests(unittest.TestCase):
    def test_exact_settled_metadata(self):
        for step in range(6):
            s = fixture(step); self.assertEqual(parse_state(record(s)), s); check_state(s, step)
            for i in range(16):
                words = list(s.supervisor); words[i] ^= 1
                with self.subTest(step=step, word=i):
                    with self.assertRaises(RuntimeError): check_state(replace(s, supervisor=tuple(words)), step)
            for i, value in ((0, MAX), (1, 1), (2, MAX), (3, 3), (4, 2)):
                counters = list(s.counters); counters[i] = value
                with self.assertRaises(RuntimeError): check_state(replace(s, counters=tuple(counters)), step)
            with self.assertRaises(RuntimeError): check_state(replace(s, probes=s.probes ^ 1), step)

    def test_strict_record_shape(self):
        valid = record(fixture(4))
        for invalid in (valid + b" 0", valid.rsplit(b" ", 1)[0], valid + b"\n",
                        valid.replace(b"STATE 4", b"STATE 04"), valid.replace(b"STATE 4", b"STATE -4"),
                        valid.replace(b"STATE 4", b"STATE 6"), valid.replace(b"100", str(MAX + 1).encode())):
            with self.subTest(record=invalid):
                with self.assertRaises(RuntimeError): parse_state(invalid)

    def test_wait_only_for_explicit_startup_states(self):
        s = fixture(2)
        before = replace(s, supervisor=(0, 0, 0, 1, 3, 1, 0, 0, 0, 0, *(0,) * 6), counters=(0, 0, 0, 0, 0))
        self.assertTrue(permitted_intermediate(before, 2))
        for step in range(6):
            s = fixture(step)
            for i in (0, 1, 2, 9, 10):
                words = list(s.supervisor); words[i] = 99
                self.assertFalse(permitted_intermediate(replace(s, supervisor=tuple(words)), step))
            counters = list(s.counters); counters[3] = 3
            self.assertFalse(permitted_intermediate(replace(s, counters=tuple(counters)), step))
        self.assertFalse(permitted_intermediate(fixture(4), 4))


if __name__ == "__main__":
    unittest.main()
