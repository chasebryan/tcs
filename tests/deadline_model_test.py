"""Finite timing composition, not hardware/scheduling or real-time evidence.

Timing is independently implemented below, composing the existing independent
Python lifecycle/reduction references. The C probe uses the actual C libraries.
"""
from collections import deque
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

import lifecycle_model_test as lc
import reduction_model_test as rd

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("TCS_TEST_BUILD_DIR", ROOT / "build"))
# State: reduction state, last accepted clock tick, sticky clock fault, two ends.
EMPTY = (rd.EMPTY, 0, 0, (0, 0))


def flatten(state):
    control, tick, fault, ends = state
    return (*rd.flatten(control), tick, fault, *ends)


def observe(state, sample, ok, tick):
    control, last, fault, ends = state
    fault = int(bool(fault or not ok or tick < last))
    if fault:
        sample |= 3
    else:
        last = tick
        sample |= sum(1 << i for i, end in enumerate(ends) if end and end <= tick)
    return rd.observe(control, sample), last, fault, ends


def reference(state, action):
    kind, sample, ok, tick, duration, *event = action
    after = observe(state, sample, ok, tick)
    control, last, fault, ends = after
    if not kind:
        return (int(bool(fault or control[2])), 0), after
    _, op, index, _, _, _ = event
    if (op == lc.RESERVE and not 0 < duration <= lc.MAX - last) or (op != lc.RESERVE and duration):
        return (lc.INVALID, 0), after
    result, control = rd.reference(control, (1, 0, *event))
    if result[0] == lc.OK and op == lc.RESERVE:
        ends = tuple(last + duration if i == index else end for i, end in enumerate(ends))
    return result, (control, last, fault, ends)


def actions(state):
    # Small clocks, unit lifetimes, one issued ticket. Explicit poll transitions
    # explore expiry between events; boundary tests cross fresh reads with calls.
    for sample in (0, 1, 2, 3, 4):
        for ok in range(2):
            for tick in range(3):
                yield (0, sample, ok, tick, 0, 0, 0, 0, 0, 0, 0)
    for index, slot in enumerate(state[0][0][1:]):
        for op in lc.ROLES:
            inc = 0 if op == lc.RESERVE else slot[1]
            seq = slot[3] if op == lc.COMPLETE else slot[2] if op in (lc.STOPPED, lc.DRAINED) else 0
            for actor in (min(lc.ROLES[op]), 5):
                for audit in range(2):
                    yield (1, 0, 1, state[1], int(op == lc.RESERVE), actor, op, index, inc, seq, audit)


class DeadlineModelTests(unittest.TestCase):
    def compare(self, cases):
        rows = [" ".join(map(str, (*flatten(s), *a))) for s, a, _ in cases]
        result = subprocess.run([str(BUILD / "deadline_probe")], input="\n".join(rows) + "\n",
            text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), len(cases))
        for line, (before, action, (decision, after)) in zip(lines, cases):
            self.assertEqual(tuple(map(int, line.split())),
                (*decision, *flatten(after), *rd.observations(after[0])), (before, action))
            self.assertGreaterEqual(after[1], before[1])
            self.assertGreaterEqual(after[2], before[2])
            self.assertEqual(after[0][1] & before[0][1], before[0][1])
            if before[2]:
                self.assertEqual(after[1], before[1])
            if after[2]:
                self.assertEqual(after[0][1], 3)
            if action[0] and decision[0] != lc.OK:
                self.assertEqual(after, observe(before, *action[1:4]))
            for index, end in enumerate(after[3]):
                if before[3][index]:
                    self.assertEqual(end, before[3][index])  # Never renewed or cleared.
                slot = after[0][0][index + 1]
                self.assertEqual(bool(end), slot[0] != lc.UNUSED)
                if end and end <= after[1]:
                    self.assertTrue(after[0][1] & (1 << index))
                if slot[0] == lc.ACTIVE:
                    self.assertLess(after[1], end)
                    self.assertFalse(after[2])
                old = before[0][0][index + 1]
                if old[0] == lc.RETIRED:
                    self.assertEqual(slot, old)
                if not action[0]:
                    self.assertEqual(slot[1:], old[1:])  # Clock creates no stop/drain/completion.

    def test_bounded_reachable_composition(self):
        queue, seen, cases = deque([EMPTY]), {EMPTY}, []
        transitions = 0
        while queue:
            state = queue.popleft()
            for action in actions(state):
                expected = reference(state, action)
                cases.append((state, action, expected)); transitions += 1
                after = expected[1]
                if (max(after[3]) <= 2 and all(s[2] <= 1 for s in after[0][0][1:]) and after not in seen):
                    seen.add(after); queue.append(after)
                if len(cases) >= 5000:
                    self.compare(cases); cases.clear()
        if cases:
            self.compare(cases)
        self.assertTrue(any(s[2] and s[0][0][1][3] for s in seen))
        self.assertTrue(any(s[0][1] == 1 and s[0][0][2][0] == lc.ACTIVE for s in seen))
        self.assertTrue(any(all(slot[0] == lc.RETIRED for slot in s[0][0][1:]) for s in seen))
        print(f"PASS deadline reference exploration: {len(seen)} states, {transitions} comparisons; ticks/ends <= 2, duration 1, issued <= 1")

    def test_fresh_read_event_matrix_and_u64_boundaries(self):
        active = ((1, (lc.ACTIVE, 1, 1, 1, 0, 0), lc.EMPTY[2]), 0, 0)
        maximum = ((lc.MAX, (lc.ACTIVE, lc.MAX, lc.MAX, lc.MAX, 0, 0), lc.EMPTY[2]), 0, 0)
        states = (EMPTY, (rd.EMPTY, lc.MAX, 0, (0, 0)),
                  (active, 13, 0, (15, 0)), (maximum, lc.MAX - 1, 0, (lc.MAX, 0)))
        cases = []
        for state in states:
            for event in lc.events(state[0][0]):
                for tick in (0, state[1], min(state[1] + 1, lc.MAX), lc.MAX):
                    for ok, sample in ((1, 0), (0, 0), (1, 1), (1, 2), (1, 4)):
                        for duration in (0, 1, lc.MAX):
                            action = (1, sample, ok, tick, duration, *event)
                            cases.append((state, action, reference(state, action)))
                            if len(cases) >= 5000:
                                self.compare(cases); cases.clear()
        if cases:
            self.compare(cases)

    def test_excluded_from_all_ten_guests_and_separate_cross_compile(self):
        with tempfile.TemporaryDirectory(prefix="tcs-deadline-plan-") as directory:
            for target in ("image", "terminal-image", "terminal-release-image", "isolation-image",
                           "boot-test-image", "admin-test-image", "interactive-image", "interactive-fixture-image",
                           "lifecycle-image", "containment-image", "deadline-cross-check"):
                with self.subTest(target=target):
                    plan = subprocess.run(["make", "-n", target, "BUILD_DIR=" + directory,
                        "MICROKIT_SDK=/tcs-sdk", "ZIG=/tcs-zig"], cwd=ROOT, text=True,
                        capture_output=True, timeout=10)
                    self.assertEqual(plan.returncode, 0, plan.stderr)
                    commands = [shlex.split(line) for line in plan.stdout.splitlines()
                                if line.startswith('"/tcs-zig" cc ')]
                    self.assertTrue(commands)
                    if target == "deadline-cross-check":
                        self.assertEqual(len(commands), 1)
                        self.assertIn("lib/deadline.c", commands[0]); self.assertIn("-c", commands[0])
                        self.assertIn("-DTCS_RELEASE_PROFILE=1", commands[0])
                    else:
                        for command in commands:
                            self.assertFalse(any(Path(arg).name.startswith("deadline") for arg in command), command)


if __name__ == "__main__":
    unittest.main()
