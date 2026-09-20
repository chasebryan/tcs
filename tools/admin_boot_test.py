"""Run public-fixture signed administration through real guest IPC, then UART."""
import argparse
from pathlib import Path
import secrets
import selectors
import subprocess
import tempfile
import time

from boot_context_test import KEY


def expected_boot():
    generations = [1, 1, 2, 3, 4, 4, 5, 6, 7, 7, 8, 8]
    text = "TCS ADMIN IPC TEST ONLY\n"
    for i, generation in enumerate(generations):
        decision = 3 if i == 1 else 4 if i == 5 else 0
        applied = int(i not in (1, 5, 9, 11))
        text += f"ADMIN PASS seq={i+1} decision={decision} audit={int(i < 8)} applied={applied} generation={generation}\n"
    text += "TCS ADMIN IPC PASS commands=12 state=quarantined generation=8\n"
    return (text + "TCS TERMINAL READY (release-kernel, read-only)\ntcs> ").encode()


def consume_expected(pending, expected):
    # Check available bytes immediately; do not turn a short explicit FAIL into a timeout.
    n = min(len(pending), len(expected))
    if pending[:n] != expected[:n]:
        raise RuntimeError(f"Unexpected guest output: {pending!r}")
    if len(pending) < len(expected):
        return False
    del pending[:len(expected)]
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    nonce = secrets.token_bytes(32)
    fixture = subprocess.run([str(args.fixture.resolve())], input=nonce, capture_output=True,
                             check=True, timeout=10).stdout
    if (len(fixture) != 112 + 12 * 192 or fixture[48:80] != nonce or fixture[80:112].hex() != KEY or
            fixture[:16] != b"TCS-BOOT\x01\x01" + bytes(6)):
        raise RuntimeError("Invalid public-fixture scenario")
    with tempfile.TemporaryDirectory(prefix="tcs-admin-", dir="/tmp") as directory:
        root = Path(directory)
        command = [args.qemu, "-machine", "virt,virtualization=on", "-cpu", "cortex-a53",
                   "-m", "2G", "-smp", "1", "-display", "none", "-no-reboot",
                   "-chardev", "stdio,id=uart,signal=off", "-serial", "chardev:uart",
                   "-monitor", "none", "-nic", "none", "-accel", "tcg",
                   "-global", "fw_cfg_mem.dma_enabled=off", "-device",
                   "loader,file=" + str(args.image.resolve()) + ",addr=0x70000000,cpu-num=0"]
        items = [("test-context", fixture[:112])]
        items += [(f"admin-{i+1:02}", fixture[112+192*i:112+192*(i+1)]) for i in range(12)]
        for name, data in items:
            path = root / name; path.write_bytes(data)
            command += ["-fw_cfg", f"name=opt/tcs/{name},file={path}"]
        run(args, command)


def run(args, command):
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pending, transcript = bytearray(), bytearray()
    selector = selectors.DefaultSelector(); selector.register(process.stdout, selectors.EVENT_READ)

    def expect(expected):
        deadline = time.monotonic() + 25
        while not consume_expected(pending, expected):
            if time.monotonic() >= deadline:
                raise RuntimeError(f"Guest response timeout: {pending!r}")
            if selector.select(timeout=0.1):
                data = process.stdout.read1(65536)
                if not data:
                    raise RuntimeError("Guest exited before completing the scenario")
                transcript.extend(data); pending.extend(data.replace(b"\r", b""))
                if len(transcript) > 65536:
                    raise RuntimeError("Unbounded guest output")

    def check(line, result):
        for i in range(0, len(line), 8):
            process.stdin.write(line[i:i+8]); process.stdin.flush(); time.sleep(0.002)
        expect(line + result)

    try:
        expect(expected_boot())
        own_status = b"SELF state=quarantined generation=8 object=0 rights=0\ntcs> "
        check(b"status\n", own_status)
        check(b"read 6\n", b"READ DENIED status=5\ntcs> ")
        for line in (b"grant 1\n", b"restore 1\n", b"revoke 1\n", b"quarantine 1\n", b"status 2\n"):
            check(line, b"ERROR unknown or malformed command\ntcs> ")
            check(b"status\n", own_status)
        if pending:
            raise RuntimeError(f"Unexpected trailing output: {pending!r}")
        print("PASS real guest signed admin/policy/audit IPC: 12 correlated receipts, replay, stale state, reads, audit exhaustion, UART denial")
    finally:
        selector.close()
        if process.poll() is None:
            process.terminate()
        try:
            tail, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill(); tail, _ = process.communicate()
        transcript.extend(tail)
        args.log.parent.mkdir(parents=True, exist_ok=True); args.log.write_bytes(transcript)


if __name__ == "__main__":
    main()
