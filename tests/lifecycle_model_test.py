"""Bounded reference-state exploration against the sanitized native C model.

Two slots, up to two issued requests per slot, every generated event at every
reachable state. This is finite testing, not a refinement proof or kernel test.
"""
from collections import deque
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("TCS_TEST_BUILD_DIR", ROOT / "build"))
MAX = (1 << 64) - 1
EMPTY = (0, (0, 0, 0, 0, 0, 0), (0, 0, 0, 0, 0, 0))
# State: last incarnation, then (phase, incarnation, issued, pending, stopped, drained).
UNUSED, STARTING, READY, ACTIVE, CLOSING, RETIRED = range(6)
RESERVE, STARTED, ACTIVATE, BEGIN, COMPLETE, CONTAIN, STOPPED, DRAINED = range(1, 9)
OK, INVALID, DENIED, STALE, BAD_STATE, AUDIT_REQUIRED, EXHAUSTED = range(7)
ROLES = {RESERVE: {1}, STARTED: {2}, ACTIVATE: {1}, BEGIN: {3},
         COMPLETE: {3}, CONTAIN: {1, 2, 4}, STOPPED: {2}, DRAINED: {3}}
PHASES = {RESERVE: {UNUSED}, STARTED: {STARTING}, ACTIVATE: {READY}, BEGIN: {ACTIVE},
          COMPLETE: {ACTIVE}, CONTAIN: {STARTING, READY, ACTIVE}, STOPPED: {CLOSING}, DRAINED: {CLOSING}}


def flatten(state):
    return (state[0], *state[1], *state[2])


def reference(state, event):
    actor, op, index, incarnation, sequence, audit = event
    def fail(status):
        return (status, 0), state
    if index >= 2 or op not in ROLES:
        return fail(INVALID)
    if actor not in ROLES[op]:
        return fail(DENIED)
    if ((op not in (COMPLETE, STOPPED, DRAINED) and sequence != 0) or
        (op == COMPLETE and sequence == 0) or
        (incarnation != 0 if op == RESERVE else incarnation == 0)):
        return fail(INVALID)
    phase, ident, issued, pending, stopped, drained = state[index + 1]
    if op != RESERVE and incarnation != ident:
        return fail(STALE)
    if phase not in PHASES[op]:
        return fail(BAD_STATE)
    last, value = state[0], ident
    if op == RESERVE:
        if any(slot[0] not in (UNUSED, RETIRED) for slot in state[1:]):
            return fail(BAD_STATE)
        if last == MAX:
            return fail(EXHAUSTED)
        if not audit:
            return fail(AUDIT_REQUIRED)
        last += 1
        ident = value = last
        phase = STARTING
    elif op in (STARTED, ACTIVATE):
        if op == ACTIVATE and not audit:
            return fail(AUDIT_REQUIRED)
        phase = READY if op == STARTED else ACTIVE
    elif op == BEGIN:
        if pending:
            return fail(BAD_STATE)
        if issued == MAX:
            return fail(EXHAUSTED)
        issued += 1
        pending = value = issued
    elif op == COMPLETE:
        if sequence != pending:
            return fail(STALE)
        pending, value = 0, sequence
    elif op == CONTAIN:
        phase = CLOSING
    else:
        if sequence != issued:
            return fail(STALE)
        if (stopped if op == STOPPED else drained):
            return fail(BAD_STATE)
        if op == STOPPED:
            stopped = 1
        else:
            drained, pending = 1, 0
        if stopped and drained:
            phase = RETIRED
    slots = list(state[1:])
    slots[index] = (phase, ident, issued, pending, stopped, drained)
    return (OK, value), (last, *slots)


def events(state):
    variants = set()
    for index, slot in enumerate(state[1:]):
        for op in ROLES:
            incarnation = 0 if op == RESERVE else slot[1]
            sequence = slot[3] if op == COMPLETE else slot[2] if op in (STOPPED, DRAINED) else 0
            for actor in range(7):
                for audit in range(2):
                    base = (actor, op, index, incarnation, sequence, audit)
                    variants.add(base)
            # Corruption matrix uses authorized actor; actor errors are above.
            base = (min(ROLES[op]), op, index, incarnation, sequence, 1)
            for field, values in ((0, (99,)), (1, (0, 9, MAX)), (2, (2, MAX)),
                                  (3, (0, 1, 2, 3, MAX)), (4, (0, 1, 2, 3, MAX))):
                for value in values:
                    changed = list(base); changed[field] = value
                    variants.add(tuple(changed))
    return sorted(variants)


class LifecycleModelTests(unittest.TestCase):
    def compare(self, cases):
        rows = [" ".join(map(str, (*flatten(state), *event))) for state, event, _ in cases]
        result = subprocess.run([str(BUILD / "lifecycle_probe")], input="\n".join(rows) + "\n",
            text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), len(cases))
        for line, (before, event, expected) in zip(lines, cases):
            actual = tuple(map(int, line.split()))
            decision, after = expected
            self.assertEqual(actual, (*decision, *flatten(after)), (before, event))
            # Independent temporal properties in addition to reference equality.
            self.assertGreaterEqual(after[0], before[0])
            if decision[0] != OK:
                self.assertEqual(after, before)
            self.assertLessEqual(sum(s[0] not in (UNUSED, RETIRED) for s in after[1:]), 1)
            for old, new in zip(before[1:], after[1:]):
                self.assertGreaterEqual(new[2], old[2])
                if old[0] == RETIRED:
                    self.assertEqual(new, old)
                if old[0] == CLOSING:
                    self.assertIn(new[0], (CLOSING, RETIRED))
                if old[0] != ACTIVE and new[0] == ACTIVE:
                    self.assertEqual((event[0], event[1], event[5]), (1, ACTIVATE, 1))
                if new[0] == RETIRED:
                    self.assertEqual(new[3:], (0, 1, 1))

    def test_bounded_reachable_states_and_adversarial_events(self):
        queue, seen, cases = deque([EMPTY]), {EMPTY}, []
        transitions = 0
        while queue:
            state = queue.popleft()
            for event in events(state):
                expected = reference(state, event)
                cases.append((state, event, expected)); transitions += 1
                after = expected[1]
                if all(s[2] <= 2 for s in after[1:]) and after not in seen:
                    seen.add(after); queue.append(after)
                if len(cases) >= 5000:
                    self.compare(cases); cases.clear()
        if cases:
            self.compare(cases)
        self.assertTrue(any(all(s[0] == RETIRED for s in state[1:]) for state in seen))
        self.assertTrue(any(s[3] == 2 and s[0] == CLOSING for state in seen for s in state[1:]))
        print(f"PASS lifecycle reference exploration: {len(seen)} states, {transitions} event comparisons; two slots, issued <= 2")

    def test_counter_boundaries(self):
        states = [(MAX, *EMPTY[1:]), (MAX - 1, *EMPTY[1:]),
                  (MAX, (ACTIVE, MAX, MAX, 0, 0, 0), EMPTY[2]),
                  (MAX, (ACTIVE, MAX, MAX, MAX, 0, 0), EMPTY[2]),
                  (MAX, (CLOSING, MAX, MAX, MAX, 0, 0), EMPTY[2])]
        self.compare([(s, e, reference(s, e)) for s in states for e in events(s)])

    def test_no_lifecycle_code_in_guest_builds(self):
        with tempfile.TemporaryDirectory(prefix="tcs-lifecycle-plan-") as directory:
            for target in ("image", "terminal-image", "terminal-release-image", "isolation-image",
                           "boot-test-image", "admin-test-image", "interactive-image", "interactive-fixture-image"):
                with self.subTest(target=target):
                    plan = subprocess.run(["make", "-n", target, "BUILD_DIR=" + directory,
                        "MICROKIT_SDK=/tcs-sdk", "ZIG=/tcs-zig"], cwd=ROOT, text=True,
                        capture_output=True, timeout=10)
                    self.assertEqual(plan.returncode, 0, plan.stderr)
                    commands = [shlex.split(line) for line in plan.stdout.splitlines()
                                if line.startswith('"/tcs-zig" cc ')]
                    self.assertTrue(commands)
                    for command in commands:
                        self.assertFalse(any(Path(arg).name.startswith("lifecycle") for arg in command), command)


if __name__ == "__main__":
    unittest.main()
