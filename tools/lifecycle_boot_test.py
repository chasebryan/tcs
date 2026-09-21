"""Observe a separate release-kernel supervisor experiment, not general recovery."""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import selectors
import subprocess
import time

BANNER = b"TCS LIFECYCLE READY (release-kernel, TEST-ONLY, fixture approval)\n"
PASS = b"TCS LIFECYCLE PASS (test fixture, no resource reuse)\n"
PROMPT = b"fixture> "
ZERO = (0,) * 6


@dataclass(frozen=True)
class State:
    step: int
    supervisor: tuple
    broker: tuple
    counters: tuple


def parse_state(line):
    if not re.fullmatch(rb"STATE(?: (?:0|[1-9][0-9]{0,19})){28}", line):
        raise RuntimeError("Malformed lifecycle state record")
    numbers = tuple(map(int, line.split()[1:]))
    if any(n >= 1 << 64 for n in numbers) or numbers[0] > 8:
        raise RuntimeError("Lifecycle record exceeds bounds")
    return State(numbers[0], numbers[1:19], numbers[19:26], numbers[26:28])


def check_state(s, step):
    """Exact settled fixture state; counters checked separately across samples."""
    a = ZERO if step == 0 else (2, 1, 0, 0, 0, 0) if step == 1 else (
        (3, 1, 1, 1, 0, 0) if step == 2 else (4, 1, 1, 1, 1, 0) if step == 3 else (5, 1, 1, 0, 1, 1))
    b = ZERO if step < 5 else (2, 2, 0, 0, 0, 0) if step == 5 else (
        (4, 2, 1, 1, 1, 0) if step == 6 else (5, 2, 1, 0, 1, 1))
    fault = int(step >= 6)
    expected = (0, fault, 0 if step == 0 else 1 if step < 5 else 2, *a, *b)
    stats = (0, fault, 2 * fault, int(step >= 2) + fault,
             int(step >= 4) + int(step >= 7), 1 if step in (2, 3) else 2 if step == 6 else 0,
             0 if step == 0 else 1 if step < 5 else 3)
    if s.step != step or s.supervisor[:15] != expected or s.broker != stats:
        raise RuntimeError("Lifecycle transition/identity/receipt mismatch")
    ip, address, fsr = s.supervisor[15:]
    if fault:
        if not (0x200000 <= ip < 0x300000 and not ip & 3 and address == 0x0dead000 and
                fsr < 1 << 32 and (fsr >> 26) & 63 == 0x24 and fsr & (1 << 25) and
                not fsr & (15 << 7) and fsr & (1 << 6) and 4 <= fsr & 63 <= 7):
            raise RuntimeError("Wrong lifecycle kernel-fault evidence")
    elif (ip, address, fsr) != (0, 0, 0):
        raise RuntimeError("Unexpected lifecycle fault")
    if ((step < 2 and s.counters[0] != 0) or (step >= 2 and s.counters[0] == 0) or
            s.counters[1] != (128 if step >= 6 else 0)):
        raise RuntimeError("Worker progress evidence mismatch")


def permitted_intermediate(s, step):
    """Only explicit startup snapshots may be retried, never bad fault evidence."""
    if s.step != step or s.supervisor[0:2] != (0, 0) or s.supervisor[15:] != (0, 0, 0) or s.broker[0]:
        return False
    a, b = s.supervisor[3:9], s.supervisor[9:15]
    if step == 1:
        return (s.supervisor[2] == 1 and a in ((1, 1, 0, 0, 0, 0), (2, 1, 0, 0, 0, 0)) and
                b == ZERO and s.broker[:6] == (0,) * 6 and s.broker[6] in (0, 1) and s.counters == (0, 0))
    if step == 2:
        return (s.supervisor[2] == 1 and a in ((3, 1, 0, 0, 0, 0), (3, 1, 1, 1, 0, 0)) and
                b == ZERO and s.broker in ((0, 0, 0, 0, 0, 0, 1), (0, 0, 0, 1, 0, 1, 1)) and
                s.counters[1] == 0 and (s.counters[0] == 0 or s.broker[3] == 1))
    if step not in (5, 6) or s.supervisor[2] != 2 or a != (5, 1, 1, 0, 1, 1) or not s.counters[0]:
        return False
    if step == 5:
        return (b in ((1, 2, 0, 0, 0, 0), (2, 2, 0, 0, 0, 0)) and
                s.broker in ((0, 0, 0, 1, 1, 0, 1), (0, 0, 0, 1, 1, 0, 3)) and s.counters[1] == 0)
    return (b in ((3, 2, 0, 0, 0, 0), (3, 2, 1, 1, 0, 0)) and
            s.broker in ((0, 0, 0, 1, 1, 0, 3), (0, 0, 1, 1, 1, 0, 3),
                         (0, 0, 2, 1, 1, 0, 3), (0, 1, 2, 1, 1, 0, 3), (0, 1, 2, 2, 1, 2, 3)) and
            s.counters[1] in (0, 128) and (s.counters[1] == 0 or s.broker[3] == 2))


def run(qemu, image, log):
    command = [qemu, "-machine", "virt,virtualization=on", "-cpu", "cortex-a53",
        "-m", "2G", "-smp", "1", "-display", "none", "-chardev", "stdio,id=uart,signal=off",
        "-serial", "chardev:uart", "-monitor", "none", "-nic", "none", "-accel", "tcg",
        "-no-reboot", "-device", "loader,file=" + str(image.resolve()) + ",addr=0x70000000,cpu-num=0"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    transcript, pending = bytearray(), bytearray()
    selector = selectors.DefaultSelector(); selector.register(process.stdout, selectors.EVENT_READ)

    def response(command=None):
        if command is not None:
            for i in range(0, len(command), 8):
                process.stdin.write(command[i:i + 8]); process.stdin.flush(); time.sleep(0.002)
        deadline = time.monotonic() + 10
        while PROMPT not in pending:
            if time.monotonic() > deadline:
                raise RuntimeError(f"Lifecycle response timeout: {pending!r}")
            if selector.select(timeout=0.1):
                data = process.stdout.read1(65536)
                if not data: raise RuntimeError("QEMU exited before lifecycle response")
                transcript.extend(data); pending.extend(data.replace(b"\r", b""))
                if len(transcript) > 1024 * 1024: raise RuntimeError("Unbounded lifecycle output")
        end = pending.index(PROMPT) + len(PROMPT)
        out = bytes(pending[:end]); del pending[:end]
        if b"FAIL" in out or b"MON|ERROR" in out: raise RuntimeError("Guest lifecycle failure")
        return out

    def state_command(command, step):
        out = response(command)
        prefix = command if command is not None else BANNER
        suffix = PASS + PROMPT if command == b"next\n" and step == 8 else PROMPT
        if not out.startswith(prefix) or not out.endswith(suffix):
            raise RuntimeError(f"Unexpected lifecycle framing: {out!r}")
        record = out[len(prefix):-len(suffix)]
        if not record.endswith(b"\n") or record.count(b"\n") != 1:
            raise RuntimeError("Unexpected lifecycle record framing")
        return parse_state(record[:-1])

    def settled(command, step):
        state = state_command(command, step)
        # Startup/fault callbacks may finish between independent status RPCs.
        # Only phases with actual asynchronous worker startup may be retried.
        for _ in range(50):
            try:
                check_state(state, step)
                return state
            except RuntimeError:
                if not permitted_intermediate(state, step): raise
                time.sleep(0.01); state = state_command(b"status\n", step)
        check_state(state, step)
        return state

    try:
        settled(None, 0)
        if response(b"nextx\n") != b"nextx\nERROR fixture commands: status | next\n" + PROMPT:
            raise RuntimeError("Unexpected invalid-command handling")
        if response(b"next\x03") != b"next^C\nERROR discarded input\n" + PROMPT:
            raise RuntimeError("Unexpected cancellation handling")
        settled(b"status\n", 0)
        settled(b"next\n", 1)
        first = settled(b"next\n", 2)
        time.sleep(0.05)
        running = settled(b"status\n", 2)
        if running.counters[0] <= first.counters[0]:
            raise RuntimeError("Noncooperating worker did not make observable progress")
        stopped = settled(b"next\n", 3)
        for _ in range(5):
            time.sleep(0.02)
            if settled(b"status\n", 3).counters != stopped.counters:
                raise RuntimeError("Stopped worker counter changed")
        for step in range(4, 9):
            current = settled(b"next\n", step)
            if current.counters[0] != stopped.counters[0]:
                raise RuntimeError("Old worker resumed during replacement")
        for _ in range(3):
            if settled(b"status\n", 8).counters != current.counters:
                raise RuntimeError("Retired worker progress changed")
    finally:
        selector.close()
        if process.poll() is None: process.terminate()
        try: tail, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill(); tail, _ = process.communicate()
        transcript.extend(tail)
        log.parent.mkdir(parents=True, exist_ok=True); log.write_bytes(transcript)
    print("PASS real supervisor gate/stop/drain/replacement; progressing noncooperating worker becomes stable")
    print("PASS second worker kernel fault, stale-incarnation rejection, finite pool; no resource reclamation")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    run(args.qemu, args.image, args.log)
