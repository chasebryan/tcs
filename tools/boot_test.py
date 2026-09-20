"""Boot in a disposable, diskless, networkless QEMU and require real test output."""
import argparse
from pathlib import Path
import selectors
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    command = [args.qemu, "-machine", "virt,virtualization=on", "-cpu", "cortex-a53",
               "-m", "2G", "-smp", "1", "-display", "none", "-serial", "stdio",
               "-monitor", "none", "-nic", "none", "-accel", "tcg", "-device",
               "loader,file=" + str(args.image.resolve()) + ",addr=0x70000000,cpu-num=0"]
    output = bytearray()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                events = selector.select(timeout=0.2)
                if events:
                    chunk = process.stdout.read1(65536)
                    if not chunk:
                        break
                    output.extend(chunk)
                    if b"TCS SEED PASS" in output or b"TCS SEED FAIL" in output:
                        break
                if process.poll() is not None:
                    break
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            tail, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            tail, _ = process.communicate()
        output.extend(tail)
        args.log.write_bytes(output)
    text = output.decode("utf-8", errors="replace")
    sys.stdout.write(text)
    if "TCS SEED PASS" not in text or "FAIL " in text or "TCS SEED FAIL" in text:
        raise SystemExit("TCS boot smoke test failed; inspect " + str(args.log))
    print("PASS QEMU boot and inter-server security scenario")


if __name__ == "__main__":
    main()
