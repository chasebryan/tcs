"""Exercise real UART input against the diskless, networkless terminal profile."""
import argparse
import json
from pathlib import Path
import re
import selectors
import socket
import subprocess
import tempfile
import time


def consume_isolation(pending):
    """Require six kernel-observed faults before accepting a test-image banner."""
    begin = b"TCS ISOLATION BEGIN release-kernel cases=6\n"
    if not begin.startswith(pending[:len(begin)]):
        raise RuntimeError("Unexpected isolation boot preamble")
    if b"TCS ISOLATION FAIL" in pending:
        raise RuntimeError("Guest isolation verifier rejected a fault or protected state")
    lines = pending.split(b"\n", 8)
    if len(lines) < 9:
        if len(pending) > 4096:
            raise RuntimeError("Oversized isolation evidence")
        return False
    addresses = (0x09000018, 0x04000000, 0x05000000, 0x05000000, 0x06000000, 0x07000000)
    pattern = rb"FAULT PASS child=(\d+) label=(\d+) words=(\d+) ip=(\d+) address=(\d+) instruction=(\d+) fsr=(\d+)"
    for child, line in enumerate(lines[1:7], 1):
        match = re.fullmatch(pattern, line)
        if not match:
            raise RuntimeError("Malformed isolation fault evidence")
        identity, label, words, ip, address, instruction, fsr = map(int, match.groups())
        execute, write, permission = child == 6, child in (2, 4, 5), child >= 5
        code = fsr & 63
        valid_code = 13 <= code <= 15 if permission else 4 <= code <= 7
        valid_ip = ip == address if execute else 0x200000 <= ip < 0x300000
        if not (identity == child and label == 6 and words == 4 and not ip & 3 and
                valid_ip and address == addresses[child - 1] and instruction == execute and
                fsr < 1 << 32 and (fsr >> 26) & 63 == (0x20 if execute else 0x24) and
                fsr & (1 << 25) and not fsr & (15 << 7) and
                (fsr >> 6) & 1 == write and valid_code):
            raise RuntimeError(f"Isolation fault mismatch for child {child}")
    if lines[7] != b"TCS ISOLATION PASS cases=6 canary=intact policy=restricted":
        raise RuntimeError("Missing protected-state isolation verdict")
    del pending[:sum(len(line) + 1 for line in lines[:8])]
    return True


def consume_boot(pending, profile):
    banner = f"TCS TERMINAL READY ({profile}-kernel, read-only)\ntcs> ".encode()
    if banner not in pending:
        marker = b"TCS TERMINAL READY ("
        start = pending.find(marker)
        if start >= 0 and b"\n" in pending[start:]:
            line = bytes(pending[start:]).split(b"\n", 1)[0]
            if line != banner.split(b"\n", 1)[0]:
                raise RuntimeError("Terminal image does not match requested kernel profile")
        return False
    start = pending.index(banner)
    if profile == "release" and start != 0:
        raise RuntimeError("Unexpected output before release terminal banner")
    end = start + len(banner)
    if b"MON|ERROR" in pending[:end]:
        raise RuntimeError("Runtime fault during boot")
    del pending[:end]
    return True


def denied_output(profile, audit_count):
    # Debug tests observe audit sequence numbers. Release has no debug channel;
    # its denial output alone does not establish the internal audit sequence.
    audit = f"TCS audit decision {audit_count}\n" if profile == "debug" else ""
    return (audit + "READ DENIED status=1\ntcs> ").encode()


class Qmp:
    """Bounded local control connection used only by the emulator test harness."""
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.stream = None
        self.sequence = 0
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    self.socket.connect(str(path))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("QMP socket did not become ready")
                    time.sleep(0.01)
            self.stream = self.socket.makefile("rb")
            if "QMP" not in self.read(time.monotonic() + 5):
                raise RuntimeError("Missing QMP greeting")
            self.command("qmp_capabilities")
        except BaseException:
            self.close()
            raise

    def read(self, deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("QMP response deadline exceeded")
        self.socket.settimeout(remaining)
        line = self.stream.readline(65537)
        if not line or len(line) > 65536 or not line.endswith(b"\n"):
            raise RuntimeError("Invalid or oversized QMP response")
        message = json.loads(line)
        if not isinstance(message, dict):
            raise RuntimeError("Invalid QMP message type")
        return message

    def command(self, name, arguments=None):
        self.sequence += 1
        deadline = time.monotonic() + 5
        message = {"execute": name, "id": self.sequence}
        if arguments is not None:
            message["arguments"] = arguments
        self.socket.settimeout(5)
        self.socket.sendall(json.dumps(message).encode() + b"\n")
        for _ in range(256):
            reply = self.read(deadline)
            if "event" in reply:
                continue
            if reply.get("id") != self.sequence or "return" not in reply:
                raise RuntimeError(f"QMP command failed: {reply}")
            return reply["return"]
        raise RuntimeError("Too many QMP events before command reply")

    def close(self):
        if self.stream is not None:
            self.stream.close()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--profile", choices=("debug", "release"), default="debug")
    parser.add_argument("--isolation", action="store_true", help="Require test-only fault evidence before terminal tests")
    args = parser.parse_args()
    if args.isolation and args.profile != "release":
        parser.error("--isolation requires --profile release")
    # A short, private path also fits macOS's Unix-domain socket path limit.
    with tempfile.TemporaryDirectory(prefix="tcs-qmp-", dir="/tmp") as directory:
        run(args, Path(directory) / "control.sock")


def run(args, control_path):
    command = [args.qemu, "-machine", "virt,virtualization=on", "-cpu", "cortex-a53",
               "-m", "2G", "-smp", "1", "-display", "none",
               "-chardev", "stdio,id=uart,signal=off", "-serial", "chardev:uart",
               "-qmp", f"unix:{control_path},server=on,wait=off",
               "-monitor", "none", "-nic", "none", "-accel", "tcg", "-device",
               "loader,file=" + str(args.image.resolve()) + ",addr=0x70000000,cpu-num=0"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    transcript, pending = bytearray(), bytearray()
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    qmp = None

    def pump(deadline):
        if time.monotonic() >= deadline:
            raise RuntimeError(f"Timed out waiting for terminal response; pending={pending!r}")
        if selector.select(timeout=0.1):
            data = process.stdout.read1(65536)
            if not data:
                raise RuntimeError("QEMU exited before terminal response")
            transcript.extend(data)
            pending.extend(data.replace(b"\r", b""))
            if len(transcript) > 1024 * 1024:
                raise RuntimeError("Unexpected unbounded terminal output")

    def expect_output(expected):
        deadline = time.monotonic() + 20
        while len(pending) < len(expected):
            pump(deadline)
        actual = bytes(pending[:len(expected)])
        del pending[:len(expected)]
        if actual != expected:
            raise RuntimeError(f"Expected {expected!r}, got {actual!r}")

    def send(data):
        # Pace input below the emulated UART's capacity, including long lines.
        for start in range(0, len(data), 8):
            process.stdin.write(data[start:start + 8])
            process.stdin.flush()
            time.sleep(0.002)

    def check(data, expected, echo=None):
        send(data)
        if echo is None:
            echo = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").replace(b"\t", b" ")
        expect_output(echo + expected)

    try:
        qmp = Qmp(control_path)
        deadline = time.monotonic() + 20
        if args.isolation:
            while not consume_isolation(pending):
                pump(deadline)
        while not consume_boot(pending, args.profile):
            pump(deadline)
        version = f"TCS 0.2-dev / {args.profile}-kernel / read-only terminal\ntcs> ".encode()
        help_text = b"help | version | status | read <generation>\nNo administration commands.\ntcs> "
        invalid = b"ERROR unknown or malformed command\ntcs> "
        discarded = b"ERROR discarded input line\ntcs> "
        own_status = b"SELF state=restricted generation=0 object=0 rights=0\ntcs> "
        def denied(audit_count):
            return denied_output(args.profile, audit_count)
        check(b"version\r\n", version)
        check(b"help\n", help_text)
        check(b"status\n", own_status)
        # More queries than audit slots: status must not consume that capacity.
        for _ in range(80):
            check(b"status\n", own_status)
        check(b"read 1\n", denied(1))
        check(b"read 18446744073709551615\n", denied(2))
        for data in (b"grant 1\n", b"revoke 1\n", b"restore 1\n", b"quarantine 1\n",
                     b"read 0\n", b"read -1\n", b"read 18446744073709551616\n",
                     b"help now\n", b"READ 1\n", b"status 2\n", b"status 1 grant\n"):
            check(data, invalid)
        for data in (b"help\x00\n", b"help\x1b[A\n", b"help" + b" " * 124 + b"\n"):
            echo = b"help\n" if len(data) < 128 else b"help" + b" " * 123 + b"\n"
            check(data, discarded, echo=echo)
            check(b"version\n", version)
        check(b"versiom\x08n\n", version, echo=b"versiom\b \bn\n")
        check(b"\x08\x7fversion\n", version, echo=b"version\n")
        check(b"version\tx\x7f\n", version, echo=b"version x\b \b\n")
        check(b"read 1\x03", b"CANCELLED\ntcs> ", echo=b"read 1^C\n")
        check(b"version\nhelp\n", b"version\n" + version + b"help\n" + help_text, echo=b"")
        check(b"tcs> MON|ERROR microkit_test\n", invalid)
        check(b"help" + b" " * 123 + b"\n", help_text)
        check(b"read 1\n", denied(3))
        check(b"status\n", own_status)
        lost = b"\nINPUT LOST; discard until Enter or Ctrl-C\n"
        for prefix, suffix in ((b"read 1", b"\n"), (b"sta", b"tus\n")):
            send(prefix)
            expect_output(prefix)  # Prove the prefix arrived before injecting the fault.
            qmp.command("chardev-send-break", {"id": "uart"})
            expect_output(lost)  # Prove the actual driver reported transport loss.
            check(suffix, discarded, echo=b"\n")
            check(b"status\n", own_status)
        qmp.command("chardev-send-break", {"id": "uart"})
        expect_output(lost)
        check(b"read 1\x03", b"CANCELLED\ntcs> ", echo=b"^C\n")
        check(b"read 1\n", denied(4))  # Debug audit count proves no interrupted read arrived.
        check(b"status\n", own_status)
        if pending:
            raise RuntimeError(f"Unexpected trailing output: {pending!r}")
        print(f"PASS QEMU {args.profile}-kernel UART echo/editing, live status, injected serial breaks, discarded commands, and recovery")
        if args.isolation:
            print("PASS six observed isolation faults, policy-owned canary, and post-fault terminal/service liveness")
    finally:
        if qmp is not None:
            qmp.close()
        selector.close()
        if process.poll() is None:
            process.terminate()
        try:
            tail, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            tail, _ = process.communicate()
        transcript.extend(tail)
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_bytes(transcript)


if __name__ == "__main__":
    main()
