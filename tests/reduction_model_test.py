"""Finite reduction/lifecycle composition, not a scheduler or refinement proof.

The lifecycle reference is shared with its existing independent C comparison.
Reduction sampling and temporal properties are implemented separately here.
"""
from collections import deque
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

import lifecycle_model_test as lc

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("TCS_TEST_BUILD_DIR", ROOT / "build"))
EMPTY = (lc.EMPTY, 0, 0)
SAMPLES = (0, 1, 2, 3, 4, lc.MAX)


def flatten(state):
    life, inhibited, fault = state
    return (*lc.flatten(life), inhibited, fault)


def observe(state, sample):
    life, inhibited, fault = state
    malformed = bool(sample & ~3)
    inhibited |= 3 if malformed else sample
    slots = list(life[1:])
    for index, slot in enumerate(slots):
        if inhibited & (1 << index) and slot[0] in (lc.STARTING, lc.READY, lc.ACTIVE):
            slots[index] = (lc.CLOSING, *slot[1:])
    return ((life[0], *slots), inhibited, int(fault or malformed))


def reference(state, action):
    kind, sample, *event = action
    state = observe(state, sample)
    if kind == 0:
        return (int(bool(sample & ~3)), 0), state
    life, inhibited, fault = state
    _, op, index, _, _, _ = event
    if index < 2 and inhibited & (1 << index) and op in (lc.RESERVE, lc.ACTIVATE):
        return (lc.DENIED, 0), state
    result, life = lc.reference(life, event)
    return result, (life, inhibited, fault)


def observations(state):
    life, _, _ = state
    stop = sum(1 << i for i, slot in enumerate(life[1:])
               if slot[0] == lc.CLOSING and not slot[4])
    return (stop, *(int(s[0] == lc.ACTIVE) for s in life[1:]))


def actions(state):
    life = state[0]
    for sample in SAMPLES:
        yield (0, sample, 0, 0, 0, 0, 0, 0)
    # Full invalid actor/field matrix without a fresh request; the separate
    # lifecycle test already explores it in depth. Cross every request sample
    # with correctly shaped events, every actor, and approval success/failure.
    for event in lc.events(life):
        yield (1, 0, *event)
    for index, slot in enumerate(life[1:]):
        for op in lc.ROLES:
            inc = 0 if op == lc.RESERVE else slot[1]
            seq = slot[3] if op == lc.COMPLETE else slot[2] if op in (lc.STOPPED, lc.DRAINED) else 0
            for sample in SAMPLES[1:]:
                for actor in range(7):
                    for audit in range(2):
                        yield (1, sample, actor, op, index, inc, seq, audit)


class ReductionModelTests(unittest.TestCase):
    def compare(self, cases):
        rows = [" ".join(map(str, (*flatten(s), *a))) for s, a, _ in cases]
        result = subprocess.run([str(BUILD / "reduction_probe")], input="\n".join(rows) + "\n",
            text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), len(cases))
        for line, (before, action, expected) in zip(lines, cases):
            decision, after = expected
            self.assertEqual(tuple(map(int, line.split())),
                             (*decision, *flatten(after), *observations(after)), (before, action))
            self.assertEqual(after[1] & before[1], before[1])
            self.assertGreaterEqual(after[2], before[2])
            self.assertGreaterEqual(after[0][0], before[0][0])
            sampled = observe(before, action[1])
            if action[0] and decision[0] != lc.OK:
                self.assertEqual(after, sampled)  # Failed event cannot undo sampling.
            for index, (old, new) in enumerate(zip(before[0][1:], after[0][1:])):
                if after[1] & (1 << index):
                    self.assertNotIn(new[0], (lc.STARTING, lc.READY, lc.ACTIVE))
                if old[0] == lc.RETIRED:
                    self.assertEqual(old, new)
                if old[0] == lc.CLOSING:
                    self.assertIn(new[0], (lc.CLOSING, lc.RETIRED))
                if new[0] == lc.RETIRED:
                    self.assertEqual(new[3:], (0, 1, 1))
                if action[0] == 0:
                    self.assertEqual(new[1:], old[1:])  # No fabricated physical evidence.

    def test_bounded_reachable_composition(self):
        queue, seen, cases = deque([EMPTY]), {EMPTY}, []
        transitions = 0
        while queue:
            state = queue.popleft()
            for action in actions(state):
                expected = reference(state, action)
                cases.append((state, action, expected)); transitions += 1
                after = expected[1]
                if all(s[2] <= 1 for s in after[0][1:]) and after not in seen:
                    seen.add(after); queue.append(after)
                if len(cases) >= 5000:
                    self.compare(cases); cases.clear()
        if cases:
            self.compare(cases)
        self.assertIn((lc.EMPTY, 3, 1), seen)
        self.assertTrue(any(s[1] == 1 and s[0][2][0] == lc.ACTIVE for s in seen))
        self.assertTrue(any(s[2] and slot[3] for s in seen for slot in s[0][1:]))
        self.assertTrue(any(all(slot[0] == lc.RETIRED for slot in s[0][1:]) for s in seen))
        print(f"PASS reduction reference exploration: {len(seen)} states, {transitions} event comparisons; issued <= 1")

    def test_counter_and_single_bit_boundaries(self):
        lives = [(lc.MAX, *lc.EMPTY[1:]),
                 (lc.MAX, (lc.ACTIVE, lc.MAX, lc.MAX, lc.MAX, 0, 0), lc.EMPTY[2]),
                 (lc.MAX, (lc.CLOSING, lc.MAX, lc.MAX, lc.MAX, 1, 0), lc.EMPTY[2])]
        cases = []
        for life in lives:
            state = (life, 0, 0)
            for sample in (0, 1, 2, 3, lc.MAX, *(1 << i for i in range(64))):
                action = (0, sample, 0, 0, 0, 0, 0, 0)
                cases.append((state, action, reference(state, action)))
            for action in actions(state):
                cases.append((state, action, reference(state, action)))
        self.compare(cases)

    def test_excluded_from_all_nine_guests_and_separate_cross_compile(self):
        with tempfile.TemporaryDirectory(prefix="tcs-reduction-plan-") as directory:
            for target in ("image", "terminal-image", "terminal-release-image", "isolation-image",
                           "boot-test-image", "admin-test-image", "interactive-image", "interactive-fixture-image",
                           "lifecycle-image", "reduction-cross-check"):
                with self.subTest(target=target):
                    plan = subprocess.run(["make", "-n", target, "BUILD_DIR=" + directory,
                        "MICROKIT_SDK=/tcs-sdk", "ZIG=/tcs-zig"], cwd=ROOT, text=True,
                        capture_output=True, timeout=10)
                    self.assertEqual(plan.returncode, 0, plan.stderr)
                    commands = [shlex.split(line) for line in plan.stdout.splitlines()
                                if line.startswith('"/tcs-zig" cc ')]
                    self.assertTrue(commands)
                    if target == "reduction-cross-check":
                        self.assertEqual(len(commands), 1)
                        self.assertIn("lib/reduction.c", commands[0]); self.assertIn("-c", commands[0])
                        self.assertIn("-DTCS_RELEASE_PROFILE=1", commands[0])
                    else:
                        for command in commands:
                            self.assertFalse(any(Path(arg).name.startswith("reduction") for arg in command), command)


if __name__ == "__main__":
    unittest.main()
