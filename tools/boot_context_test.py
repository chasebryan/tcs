"""Test-only host-generated boot context and real guest signature verification.

Uses only the public RFC fixture executable; never reads an operator credential.
QMP is private test control, not a supported administration or reset interface.
"""
import argparse
from pathlib import Path
import secrets
import selectors
import subprocess
import tempfile
import time

from terminal_boot_test import Qmp

KEY = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
DONE = b"TCS BOOT TEST DONE\r\n"


def expected(context, boot=0, transport=0, admission=0):
    text = f"TCS BOOT TEST ONLY\r\nBOOT status={boot}\r\n"
    if boot == 0:
        text += f"BOOT nonce={context[48:80].hex()}\r\nBOOT key={context[80:112].hex()}\r\n"
        text += f"COMMAND transport={transport}\r\n"
        if transport == 0:
            text += f"COMMAND admission={admission}\r\n"
            if admission == 0:
                text += "COMMAND duplicate=6\r\nCOMMAND replay=5\r\n"
    return text.encode() + DONE


def fixture(path, nonce):
    data = subprocess.run([str(path.resolve())], input=nonce, capture_output=True,
                          timeout=10, check=True).stdout
    if (len(data) != 304 or data[48:80] != nonce or data[80:112].hex() != KEY or
            data[:16] != b"TCS-BOOT\x01\x01" + bytes(6)):
        raise RuntimeError("Unexpected public test fixture")
    return data[:112], data[112:]


def check_transcript(output, wanted):
    if output != wanted:
        raise RuntimeError(f"Unexpected boot response: {output!r}")


def run_case(args, name, context, packet, wanted, *, dma=False, reset=False):
    with tempfile.TemporaryDirectory(prefix="tcs-boot-", dir="/tmp") as directory:
        root = Path(directory)
        command = [args.qemu, "-machine", "virt,virtualization=on", "-cpu", "cortex-a53",
                   "-m", "2G", "-smp", "1", "-display", "none", "-serial", "stdio",
                   "-monitor", "none", "-nic", "none", "-accel", "tcg", "-no-reboot",
                   "-global", "fw_cfg_mem.dma_enabled=" + ("on" if dma else "off"),
                   "-device", "loader,file=" + str(args.image.resolve()) + ",addr=0x70000000,cpu-num=0"]
        if reset:
            command += ["-qmp", f"unix:{root / 'control'},server=on,wait=off"]
        for item, data in (("context", context), ("command", packet)):
            if data is not None:
                path = root / item
                path.write_bytes(data)
                command += ["-fw_cfg", f"name=opt/tcs/test-{item},file={path}"]
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        output = bytearray()
        control = None
        completed = False
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                deadline = time.monotonic() + 25
                while time.monotonic() < deadline and DONE not in output:
                    if selector.select(timeout=0.1):
                        chunk = process.stdout.read1(4096)
                        if not chunk:
                            break
                        output.extend(chunk)
                        if len(output) > 16384:
                            raise RuntimeError("Oversized guest output")
                    if process.poll() is not None:
                        break
            check_transcript(bytes(output), wanted)
            if reset:
                control = Qmp(root / "control")
                control.command("system_reset")
                process.wait(timeout=5)
                if process.returncode != 0:
                    raise RuntimeError("Reset did not exit cleanly")
            completed = True
        finally:
            if control is not None:
                control.close()
            if process.poll() is None:
                process.terminate()
            try:
                tail, _ = process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                tail, _ = process.communicate()
            if not completed:
                args.log.parent.mkdir(parents=True, exist_ok=True)
                args.log.write_bytes(output + tail)
        if reset and tail:
            args.log.write_bytes(output + tail)
            raise RuntimeError("Unexpected output after reset; possible context reuse")
        return f"CASE {name}\n".encode() + bytes(output) + (b"PASS reset exits without reusing context\n" if reset else b"")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    first, packet = fixture(args.fixture, secrets.token_bytes(32))
    second, packet2 = fixture(args.fixture, secrets.token_bytes(32))
    if first[48:80] == second[48:80]:
        raise SystemExit("Fresh launch identities unexpectedly repeated")
    cases = [
        ("fresh-first", first, packet, expected(first), {"reset": True}),
        ("fresh-second", second, packet2, expected(second), {}),
        ("old-signed-command", second, packet, expected(second, admission=3), {}),
        ("missing-context", None, packet, expected(None, boot=5), {}),
        ("short-context", first[:-1], packet, expected(None, boot=6), {}),
        ("long-context", first + b"x", packet, expected(None, boot=6), {}),
        ("not-test-format", first[:9] + bytes(1) + first[10:], packet, expected(None, boot=7), {}),
        ("zero-boot", first[:48] + bytes(32) + first[80:], packet, expected(None, boot=7), {}),
        ("zero-key", first[:80] + bytes(32), packet, expected(None, boot=7), {}),
        ("changed-realm", first[:16] + bytes([first[16] ^ 1]) + first[17:], packet,
         expected(first[:16] + bytes([first[16] ^ 1]) + first[17:], admission=3), {}),
        ("changed-key", first[:80] + bytes([first[80] ^ 1]) + first[81:], packet,
         expected(first[:80] + bytes([first[80] ^ 1]) + first[81:], admission=4), {}),
        ("missing-command", first, None, expected(first, transport=5), {}),
        ("short-command", first, packet[:-1], expected(first, transport=6), {}),
        ("bad-signature", first, packet[:-1] + bytes([packet[-1] ^ 1]), expected(first, admission=4), {}),
        ("dma-enabled", first, packet, expected(None, boot=3), {"dma": True}),
    ]
    logs = bytearray()
    for name, context, request, wanted, options in cases:
        logs.extend(run_case(args, name, context, request, wanted, **options))
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_bytes(logs)
        print("PASS", name, flush=True)
    print(f"PASS {len(cases)} QEMU boot-context cases; real guest Ed25519; no policy authority or operator provisioning")


if __name__ == "__main__":
    main()
