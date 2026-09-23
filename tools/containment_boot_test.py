"""Test-only hung broker, independent reduction notification and retained work."""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import selectors
import subprocess
import time

BANNER = b"TCS CONTAINMENT READY (release-kernel, TEST-ONLY, fixture approval)\n"
PASS = b"TCS CONTAINMENT PASS (test fixture, missing drain blocks replacement)\n"
WAIT = b"WAIT notification\n"
PROMPT = b"fixture> "
ZERO = (0,) * 6
MAX = (1 << 64) - 1


@dataclass(frozen=True)
class State:
    step: int
    supervisor: tuple
    counters: tuple
    probes: int


def parse_state(line):
    if not re.fullmatch(rb"STATE(?: (?:0|[1-9][0-9]{0,19})){23}", line):
        raise RuntimeError("Malformed containment record")
    n = tuple(map(int, line.split()[1:]))
    if any(value > MAX for value in n) or n[0] > 5:
        raise RuntimeError("Containment record exceeds bounds")
    return State(n[0], n[1:17], n[17:22], n[22])


def check_state(s, step):
    a = ZERO if step == 0 else (2, 1, 0, 0, 0, 0) if step == 1 else (
        (3, 1, 1, 1, 0, 0) if step < 4 else (4, 1, 1, 1, 1, 0))
    if (s.step != step or s.supervisor != (0, int(step >= 4), 0, int(step > 0), *a, *ZERO) or
            s.probes != (7 if step == 5 else 0)):
        raise RuntimeError("Containment gate/stop/pending/replacement mismatch")
    worker, unused, broker, caller, receipt = s.counters
    if (unused or (worker != 0 if step < 2 else not 0 < worker < MAX) or
            (broker != 0 if step < 3 else not 0 < broker < MAX) or
            caller != (0 if step < 2 else 1 if step == 2 else 2) or receipt != int(step >= 4)):
        raise RuntimeError("Containment execution/notification evidence mismatch")


def permitted_intermediate(s, step):
    if s.step != step or s.probes or s.supervisor[:4] != (0, 0, 0, 1) or s.supervisor[10:] != ZERO:
        return False
    a = s.supervisor[4:10]
    worker, unused, broker, caller, receipt = s.counters
    if unused or receipt or worker == MAX or broker == MAX:
        return False
    if step == 1:
        return a in ((1, 1, 0, 0, 0, 0), (2, 1, 0, 0, 0, 0)) and s.counters == (0,) * 5
    if step == 2:
        return (a in ((3, 1, 0, 0, 0, 0), (3, 1, 1, 1, 0, 0)) and not broker and
                caller in (0, 1) and (not worker or a[2] == 1))
    if step == 3:
        return a == (3, 1, 1, 1, 0, 0) and worker > 0 and caller in (1, 2) and (not broker or caller == 2)
    return False


def run(qemu, image, log):
    command = [qemu, "-machine", "virt,virtualization=on", "-cpu", "cortex-a53", "-m", "2G", "-smp", "1",
        "-display", "none", "-chardev", "stdio,id=uart,signal=off", "-serial", "chardev:uart",
        "-monitor", "none", "-nic", "none", "-accel", "tcg", "-no-reboot",
        "-device", "loader,file=" + str(image.resolve()) + ",addr=0x70000000,cpu-num=0"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    transcript, pending = bytearray(), bytearray()
    selector = selectors.DefaultSelector(); selector.register(process.stdout, selectors.EVENT_READ)

    def response(command=None):
        if command is not None:
            for i in range(0, len(command), 8):
                process.stdin.write(command[i:i + 8]); process.stdin.flush(); time.sleep(0.002)
        deadline = time.monotonic() + 10
        while PROMPT not in pending:
            if time.monotonic() > deadline: raise RuntimeError(f"Containment response timeout: {pending!r}")
            if selector.select(timeout=0.1):
                data = process.stdout.read1(65536)
                if not data: raise RuntimeError("QEMU exited before containment response")
                transcript.extend(data); pending.extend(data.replace(b"\r", b""))
                if len(transcript) > 1024 * 1024: raise RuntimeError("Unbounded containment output")
        end = pending.index(PROMPT) + len(PROMPT)
        out = bytes(pending[:end]); del pending[:end]
        if b"FAIL" in out or b"MON|ERROR" in out: raise RuntimeError("Guest containment failure")
        return out

    def state_command(command, step):
        out = response(command)
        prefix = BANNER if command is None else command
        suffix = PASS + PROMPT if command == b"next\n" and step == 5 else PROMPT
        if not out.startswith(prefix) or not out.endswith(suffix):
            raise RuntimeError(f"Wrong containment framing: {out!r}")
        record = out[len(prefix):-len(suffix)]
        if record == WAIT and step == 4: return None
        if not record.endswith(b"\n") or record.count(b"\n") != 1:
            raise RuntimeError("Wrong containment record framing")
        return parse_state(record[:-1])

    def settled(command, step):
        s = state_command(command, step)
        for _ in range(50):
            if s is not None:
                try: check_state(s, step); return s
                except RuntimeError:
                    if not permitted_intermediate(s, step): raise
            time.sleep(0.01); s = state_command(b"status\n", step)
        raise RuntimeError("Containment did not reach its required state/notification receipt")

    try:
        settled(None, 0)
        if response(b"nextx\n") != b"nextx\nERROR fixture commands: status | next\n" + PROMPT:
            raise RuntimeError("Unexpected invalid-command handling")
        if response(b"next\x03") != b"next^C\nERROR discarded input\n" + PROMPT:
            raise RuntimeError("Unexpected cancellation handling")
        settled(b"status\n", 0); settled(b"next\n", 1)
        active = settled(b"next\n", 2); time.sleep(0.05)
        if settled(b"status\n", 2).counters[0] <= active.counters[0]:
            raise RuntimeError("Worker did not progress before broker hang")
        hung = settled(b"next\n", 3); time.sleep(0.05)
        running = settled(b"status\n", 3)
        if running.counters[0] <= hung.counters[0] or running.counters[2] <= hung.counters[2]:
            raise RuntimeError("Worker/broker did not progress with caller blocked")
        stopped = settled(b"next\n", 4)
        previous = stopped.counters[2]
        for _ in range(5):
            time.sleep(0.02); current = settled(b"status\n", 4)
            if current.counters[0] != stopped.counters[0] or current.counters[2] < previous:
                raise RuntimeError("Stopped worker changed or broker counter regressed")
            previous = current.counters[2]
        if previous <= stopped.counters[2]: raise RuntimeError("Broker no longer spinning after containment")
        denied = settled(b"next\n", 5)
        for _ in range(3):
            current = settled(b"status\n", 5)
            if current.counters[0] != stopped.counters[0] or current.counters[2] < denied.counters[2]:
                raise RuntimeError("Containment changed during refusal probes")
    finally:
        selector.close()
        if process.poll() is None: process.terminate()
        try: tail, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill(); tail, _ = process.communicate()
        transcript.extend(tail)
        log.parent.mkdir(parents=True, exist_ok=True); log.write_bytes(transcript)
    print("PASS hung broker keeps spinning/caller never returns; independent notification closes gate and stops worker")
    print("PASS five stable worker samples, notification-only receipt, retained pending ticket; no drain, no replacement")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args(); run(args.qemu, args.image, args.log)
