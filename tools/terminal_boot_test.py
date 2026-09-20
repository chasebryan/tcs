"""Exercise real UART input against the diskless, networkless terminal profile."""
import argparse
from pathlib import Path
import selectors
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    args = parser.parse_args()
    command = [args.qemu, "-machine", "virt,virtualization=on", "-cpu", "cortex-a53",
               "-m", "2G", "-smp", "1", "-display", "none", "-serial", "stdio",
               "-monitor", "none", "-nic", "none", "-accel", "tcg", "-device",
               "loader,file=" + str(args.image.resolve()) + ",addr=0x70000000,cpu-num=0"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    transcript, pending = bytearray(), bytearray()
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)

    def receive(prompts=1):
        deadline = time.monotonic() + 20
        while pending.count(b"tcs> ") < prompts:
            if time.monotonic() >= deadline:
                raise RuntimeError("Timed out waiting for terminal response")
            if selector.select(timeout=0.1):
                data = process.stdout.read1(65536)
                if not data:
                    raise RuntimeError("QEMU exited before terminal response")
                transcript.extend(data)
                pending.extend(data)
                if len(transcript) > 1024 * 1024:
                    raise RuntimeError("Unexpected unbounded terminal output")
                if b"MON|ERROR" in transcript or b"microkit_" in data:
                    raise RuntimeError("Runtime fault or invalid Microkit operation")
        end = 0
        for _ in range(prompts):
            end = pending.index(b"tcs> ", end) + len(b"tcs> ")
        result = bytes(pending[:end]).replace(b"\r", b"")
        del pending[:end]
        return result

    def check(data, expected, prompts=1):
        # Pace input below the emulated UART's capacity, including long lines.
        for start in range(0, len(data), 8):
            process.stdin.write(data[start:start + 8])
            process.stdin.flush()
            time.sleep(0.002)
        actual = receive(prompts)
        if actual != expected:
            raise RuntimeError(f"For {data!r}: expected {expected!r}, got {actual!r}")

    try:
        banner = receive()
        if not banner.endswith(b"TCS TERMINAL READY (read-only, no echo)\ntcs> "):
            raise RuntimeError("Missing actual terminal boot banner")
        version = b"TCS 0.2-dev / read-only terminal\ntcs> "
        help_text = b"help | version | status | read <generation>\nNo administration commands.\ntcs> "
        invalid = b"ERROR unknown or malformed command\ntcs> "
        discarded = b"ERROR discarded input line\ntcs> "
        own_status = b"SELF state=restricted generation=0 object=0 rights=0\ntcs> "
        def denied(audit_count):
            return f"TCS audit decision {audit_count}\nREAD DENIED status=1\ntcs> ".encode()
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
            check(data, discarded)
            check(b"version\n", version)
        check(b"versiom\x08n\n", version)
        check(b"read 1\x03", b"CANCELLED\ntcs> ")
        check(b"version\nhelp\n", version + help_text, prompts=2)
        check(b"help" + b" " * 123 + b"\n", help_text)
        check(b"read 1\n", denied(3))
        check(b"status\n", own_status)
        print("PASS QEMU UART profile, live self-status, audit independence, read-only IPC, hostile input, and recovery")
    finally:
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
