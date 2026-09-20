"""Public-fixture host review/sign/one-shot launch and real signed UART tests."""
import argparse
import os
from pathlib import Path
import secrets
import selectors
import shutil
import subprocess
import tempfile
import time

from terminal_boot_test import Qmp

ACK = "--acknowledge-experimental"
PROMPT = b"tcs> "
PACKET_PROMPT = b"Enter exactly 384 lowercase hex digits; Enter submits, Ctrl-C cancels.\npacket> "
SAMPLE = int("43494e4958", 16)


def consume(pending, choices):
    for expected in choices:
        if pending.startswith(expected):
            del pending[:len(expected)]
            return expected
    if any(expected.startswith(pending) for expected in choices):
        return None
    raise RuntimeError(f"Unexpected guest bytes: {pending!r}")


class Guest:
    def __init__(self, command, label, log):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.selector = selectors.DefaultSelector(); self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.pending, self.transcript = bytearray(), bytearray()
        self.label, self.log = label, log

    def expect(self, *choices):
        deadline = time.monotonic() + 25
        while True:
            result = consume(self.pending, choices)
            if result is not None:
                return result
            if time.monotonic() >= deadline:
                raise RuntimeError(f"{self.label}: timeout: {self.pending!r}")
            if self.selector.select(timeout=0.1):
                chunk = self.process.stdout.read1(8192)
                if not chunk:
                    raise RuntimeError(f"{self.label}: exited: {self.pending!r}")
                self.transcript.extend(chunk); self.pending.extend(chunk.replace(b"\r", b""))
                if len(self.transcript) > 131072:
                    raise RuntimeError("Unbounded guest output")

    def send(self, data):
        # Bounded pacing avoids overrunning the small UART receive queue.
        for offset in range(0, len(data), 8):
            self.process.stdin.write(data[offset:offset+8]); self.process.stdin.flush(); time.sleep(0.003)

    def line(self, text, response):
        data = text.encode() + b"\n"; self.send(data); self.expect(data + response + PROMPT)

    def submit(self, packet, result, *, crlf=False):
        self.send(b"submit\r\n" if crlf else b"submit\n"); self.expect(b"submit\n" + PACKET_PROMPT)
        text = packet.hex().encode(); self.send(text + (b"\r\n" if crlf else b"\n"))
        self.expect(text + b"\n" + result + PROMPT)

    def close(self):
        self.selector.close()
        if self.process.poll() is None:
            self.process.terminate()
        try:
            tail, _ = self.process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill(); tail, _ = self.process.communicate()
        self.transcript.extend(tail)
        with self.log.open("ab") as output:
            output.write(f"CASE {self.label}\n".encode() + self.transcript)


def operator(args, *values, env=None, success=True):
    environment = {k: v for k, v in os.environ.items() if not k.startswith("TCS_TEST_")}
    environment.update(env or {})
    result = subprocess.run([str(args.fixture), *map(str, values)], capture_output=True, env=environment, timeout=10)
    if (result.returncode == 0) != success:
        raise RuntimeError(f"Host fixture command failed: {result.stderr!r}")
    return result.stdout


def prepare(args, root, name):
    identity, session = root / (name + "-id"), root / (name + "-session")
    operator(args, "create", identity, ACK)
    operator(args, "context", identity, session, ACK, env={"TCS_TEST_NONCE_HEX": secrets.token_hex(32)})
    return identity, session


def signed(args, identity, session, operation, sequence, generation):
    fields = (operation, "1", str(sequence), str(generation))
    review = operator(args, "review", identity, session, *fields).decode()
    approval = review.split("approval=")[1].strip()
    operator(args, "sign", identity, session, *fields, approval)
    packet = (session / f"request-{sequence}.bin").read_bytes()
    if len(packet) != 192:
        raise RuntimeError("Bad signed fixture length")
    return packet


def receipt(seq, generation, state, *, decision=0, audit=1, applied=1):
    return f"ADMIN seq={seq} decision={decision} audit={audit} applied={applied} state={state} generation={generation}\n".encode()


def status(guest, state, generation):
    guest.line("status", f"SELF state={state} generation={generation} object={42 if state == 'active' else 0} rights={int(state == 'active')}\n".encode())


def launcher(args, identity, session, image):
    return [str(args.fixture), "launch", str(identity), str(session), str(image), args.qemu, ACK]


def ready(guest, mode="PUBLIC-FIXTURE-ONLY"):
    guest.expect(f"TCS SIGNED TERMINAL READY (release-kernel, {mode})\ntcs> ".encode())


def lifecycle(args, root):
    identity, session = prepare(args, root, "lifecycle")
    packet1 = signed(args, identity, session, "grant", 1, 0)
    guest = Guest(launcher(args, identity, session, args.image), "one-shot-signed-lifecycle", args.log)
    try:
        ready(guest); status(guest, "restricted", 0)
        operator(args, "launch", identity, session, args.image, args.qemu, ACK, success=False)
        for command in ("grant 1", "revoke 1", "quarantine 1", "restore 1", "status 2"):
            guest.line(command, b"ERROR unknown or malformed command\n")
        # Whole-frame grammar failures must not consume any admin sequence.
        for text, echo in ((b"a" * 383, b"a" * 383), (b"a" * 385, b"a" * 384),
                           (b"abG" + b"a" * 381, b"ab"), (b"ab\x00cd", b"ab")):
            guest.send(b"submit\n"); guest.expect(b"submit\n" + PACKET_PROMPT)
            guest.send(text + b"\n"); guest.expect(echo + b"\nERROR discarded signed packet\ntcs> ")
        bad = bytearray(packet1); bad[-1] ^= 1
        guest.submit(bad, b"ADMIN REJECTED status=4\n")
        bad = bytearray(packet1); bad[104] = 0
        guest.submit(bad, b"ADMIN REJECTED status=2\n")
        status(guest, "restricted", 0)
        commands = [("grant",0,1,"active",0,1), ("grant",0,1,"active",3,0),
            ("revoke",1,2,"restricted",0,1), ("grant",2,3,"active",0,1),
            ("quarantine",3,4,"quarantined",0,1), ("grant",4,4,"quarantined",4,0),
            ("restore",4,5,"restricted",0,1), ("grant",5,6,"active",0,1),
            ("revoke",6,7,"restricted",0,1), ("grant",7,7,"restricted",0,0),
            ("quarantine",7,8,"quarantined",0,1), ("restore",8,8,"quarantined",0,0)]
        for seq, (operation, before, after, state, decision, applied) in enumerate(commands, 1):
            packet = packet1 if seq == 1 else signed(args, identity, session, operation, seq, before)
            guest.submit(packet, receipt(seq, after, {"restricted":0,"active":1,"quarantined":2}[state],
                                         decision=decision, audit=int(seq <= 8), applied=applied), crlf=seq == 1)
            status(guest, state, after)
            if seq == 1:
                guest.submit(packet, b"ADMIN REJECTED status=5\n")
            read = f"READ OK value={SAMPLE}\n".encode() if state == "active" and seq <= 8 else f"READ DENIED status={5 if seq > 8 else 4 if state == 'quarantined' else 1}\n".encode()
            guest.line(f"read {after}", read)
            if seq == 4:
                guest.line("read 1", b"READ DENIED status=3\n")
            if seq == 8:
                for _ in range(65):
                    guest.send(b"read 6\n")
                    ok = f"read 6\nREAD OK value={SAMPLE}\ntcs> ".encode()
                    full = b"read 6\nREAD DENIED status=5\ntcs> "
                    if guest.expect(ok, full) == full:
                        break
                else:
                    raise RuntimeError("Audit never filled")
        if guest.pending:
            raise RuntimeError("Unexpected trailing lifecycle output")
    finally:
        guest.close()
    print("PASS host review/sign/one-shot launch; signed UART lifecycle, replay, stale generation, exact frames, audit-full reductions", flush=True)
    return packet1, (session / "launch.context").read_bytes()


def cross_launch_and_operator_refusal(args, root, old_packet, old_context):
    identity, session = prepare(args, root, "fresh")
    if (session / "launch.context").read_bytes()[48:80] == old_context[48:80]:
        raise RuntimeError("Repeated fresh fixture nonce")
    packet = signed(args, identity, session, "grant", 1, 0)
    guest = Guest(launcher(args, identity, session, args.image), "cross-launch", args.log)
    try:
        ready(guest); guest.submit(old_packet, b"ADMIN REJECTED status=3\n"); status(guest,"restricted",0)
        guest.submit(packet, receipt(1,1,1)); status(guest,"active",1)
    finally:
        guest.close()
    identity, session = prepare(args, root, "operator-refusal")
    packet = signed(args, identity, session, "grant", 1, 0)
    guest = Guest(launcher(args, identity, session, args.operator_image), "operator-refuses-fixture", args.log)
    try:
        ready(guest, "experimental-operator")
        guest.submit(packet, b"ADMIN REJECTED status=1\n"); status(guest,"restricted",0)
    finally:
        guest.close()
    print("PASS cross-launch signature refusal; operator guest refuses fixture provisioning; no actual operator credential", flush=True)


def uart_breaks(args, root):
    # Explicit test-only direct launcher: adds a private QMP socket, never used
    # by the operator CLI. No networking/disk or credential is added.
    identity, session = prepare(args, root, "breaks")
    command = [args.qemu, "-machine","virt,virtualization=on","-cpu","cortex-a53","-m","2G","-smp","1",
        "-display","none","-no-reboot","-chardev","stdio,id=uart,signal=off","-serial","chardev:uart",
        "-monitor","none","-nic","none","-accel","tcg","-global","fw_cfg_mem.dma_enabled=off",
        "-device",f"loader,file={args.image},addr=0x70000000,cpu-num=0",
        "-fw_cfg",f"name=opt/tcs/launch-context,file={session / 'launch.context'}",
        "-qmp",f"unix:{root / 'qmp'},server=on,wait=off"]
    guest = Guest(command, "signed-UART-breaks-test-only", args.log); control = None
    try:
        ready(guest); control = Qmp(root / "qmp")
        cases = [("grant",96,"restricted","active"), ("revoke",384,"active","restricted"),
                 ("quarantine",192,"restricted","quarantined")]
        for seq, (operation, split, previous, final) in enumerate(cases, 1):
            packet = signed(args, identity, session, operation, seq, seq-1); text = packet.hex().encode()
            guest.send(b"submit\n"); guest.expect(b"submit\n" + PACKET_PROMPT)
            guest.send(text[:split]); guest.expect(text[:split])
            control.command("chardev-send-break", {"id":"uart"})
            guest.expect(b"\nINPUT LOST; discard until Enter or Ctrl-C\n")
            if seq == 3:
                guest.send(b"\x03"); guest.expect(b"^C\nCANCELLED\ntcs> ")
            else:
                guest.send(text[split:] + b"\n"); guest.expect(b"\nERROR discarded signed packet\ntcs> ")
            status(guest, previous, seq-1)
            guest.submit(packet, receipt(seq,seq,{"restricted":0,"active":1,"quarantined":2}[final]))
            status(guest, final, seq)
    finally:
        if control:
            control.close()
        guest.close()
    print("PASS three real UART breaks discard signed prefixes/full frames; Enter/Ctrl-C recovery preserves sequence and policy", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--operator-image", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    args.qemu = str(Path(shutil.which(args.qemu) or args.qemu).resolve())
    args.fixture, args.image, args.operator_image = args.fixture.resolve(), args.image.resolve(), args.operator_image.resolve()
    args.log.parent.mkdir(parents=True, exist_ok=True); args.log.write_bytes(b"")
    with tempfile.TemporaryDirectory(prefix="tcs-interactive-", dir="/tmp") as directory:
        root = Path(directory).resolve()
        packet, context = lifecycle(args, root)
        cross_launch_and_operator_refusal(args, root, packet, context)
        uart_breaks(args, root)


if __name__ == "__main__":
    main()
