# Boot context transport experiment

Status: **test-only release-kernel image**, not operator provisioning or a live administration service. The ordinary seed/terminal/isolation images retain their existing graphs. The new two-domain profile contains only a boot probe and the isolated serial driver: no policy server, storage, or administrator endpoint exists to receive the probe's admitted command.

This increment moves the actual Ed25519 verifier into a guest and tests how trusted launch context reaches it without passing through serial input. A host harness creates a fresh 32-byte public launch nonce with Python's OS-backed `secrets.token_bytes`, then the public RFC 8032 vector-1 fixture signs a command bound to that nonce. The image itself contains no signing secret. The fixture helper accepts only a nonce, never an operator credential; it is deliberately not a general signing tool. Nothing generated here is a deployment identity.

## Authority and transport

The test graph maps QEMU's firmware-configuration page only into `boot_probe`. The serial server owns only its UART page/IRQ. The probe has one protected output call to serial; serial cannot call the probe or provide its boot data. An independent complete topology allowlist rejects any other map, domain, image, channel, or attribute; native mutation tests change every declared attribute and add unexpected children. Normal-profile validation rejects this test graph.

The device is QEMU-specific, not a portable hardware trust root. Its address is `0x09020000` on the inspected [QEMU ARM virt implementation](https://github.com/qemu/qemu/blob/v11.1.1/hw/arm/virt.c). The reader uses the [fw_cfg selector/data interface](https://www.qemu.org/docs/master/specs/fw_cfg.html), with big-endian selector writes and byte reads. It verifies the device signature and requires feature value **exactly 1**, refusing DMA or unknown features before looking up any file. The launcher supplies `-global fw_cfg_mem.dma_enabled=off`; an actual DMA-enabled launch is rejected by the guest. Mapping this page with DMA enabled would expose additional device authority; do not remove this gate.

QEMU's [8.2.2 fw_cfg implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/nvram/fw_cfg.c) only maps the DMA register when enabled. The tests establish the guest feature check and refusal under the tested emulator, not arbitrary device/DMA containment or an independent audit of QEMU. Host/QEMU integrity remains trusted.

The allocation-free reader bounds the directory to 64 entries and requested data to 256 bytes. It rejects malformed/unterminated names, reserved bits, unsupported selector ranges, aliased selectors, duplicate target names, missing targets, wrong sizes, and callback failures. Failed bounded reads clear their output. A single exclusive owner controls the selector; this API is not safe for concurrent unsynchronized peripheral users. A lying device can fabricate its metadata and data; the transport does not authenticate an untrusted hypervisor.

## Explicit test format

`opt/tcs/test-context` is exactly 112 bytes: the 16-byte prefix `TCS-BOOT`, version byte 1, **test-only flag byte 1**, six zero reserved bytes; then realm, launch nonce, and public key, each 32 bytes. The test decoder rejects any different prefix/flag and any all-zero field. Nonzero tests do not establish key validity, authorization, entropy, or uniqueness. `opt/tcs/test-command` contains the exact 192-byte [signed administration packet](ADMIN.md).

The test-only prefix/entry names must not become deployment authority. An operator-selected key, authorized realm, explicit provisioning workflow, authenticated context discovery for a real signer, and private policy IPC are still missing. No arbitrary key import, password endpoint, operator key creation, or credential file access is implemented.

## Freshness and restart boundary

Each supported **test harness launch** generates a new nonce before creating its QEMU process. Two launches of the same image accept their own signed packets; the second rejects a valid packet bound to the first. This observes context binding, not a proof of random-number quality or universal uniqueness. The security assumption is a trustworthy host CSPRNG and a fresh process/context for every verifier incarnation.

`-no-reboot` is tested with a real private QMP reset request: QEMU must exit successfully, with no second boot output. The socket exists only in a private temporary test directory, with no TCP listener. That test is not a VM snapshot/rollback defense. Reusing the context file, restoring a snapshot, bypassing the launcher, or restarting the verifier inside the same VM can repeat the identity and re-enable signatures. No supervisor/restart API is offered by this profile. Before any live administration or recovery implementation, define and enforce its new-incarnation protocol; never silently reuse the boot value.

## Run and evidence

- `make boot-test-smoke`: build the release guest and sanitized public-fixture helper, then run 15 QEMU cases.
- `make boot-test-smoke-saved`: verify and run the included image; requires a native C compiler for the fixture helper, but no SDK/cross-compiler download.
- `make test`: sanitized boot reader tests, callback-failure injection, exact context grammar, topology mutations, and strict transcript-corruption tests.

The guest suite requires exact output for two fresh launches, old signed commands, missing/short/long context, wrong format, zero nonce/key, changed realm/key, missing/short command, bad signature, and enabled DMA. Successful admission is followed by busy and replay denials. The probe's completion acknowledgement is only a local test action: **no policy execution or real policy receipt occurs**. Saved image, build report, and transcript are in `artifacts/`; the transcript contains public, per-run test nonces and is not expected to reproduce byte-for-byte across launches.
