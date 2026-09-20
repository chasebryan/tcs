# TCS development checkpoints

## Current direction

Continue through the roadmap in small, reviewable increments. Preserve the working seed, keep runtime claims narrower than the observed evidence, and never grant a new input path administrative authority by default.

## 2026-09-20 — signed-administration and replay foundation

Added exact 192-byte signed packets, real Ed25519 verification through pinned unmodified Monocypher 4.0.3, realm/boot binding, nonwrapping request sequences, one-pending-request admission, and definitive-completion handling. A separate policy-owner helper checks the signed expected generation immediately before candidate mutation, then uses existing audit commit semantics. None of this code is wired to a live admin endpoint or included in boot images yet.

Sanitized host tests pass RFC vectors, 1,536 one-bit corruptions, exact lengths/encoding, unaligned input, correctly signed malformed commands, replay/context/key mismatch, pending/wrong receipts, exhausted sequences, stale generation, and audit-failure behavior. A test explicitly demonstrates that reusing a boot identity after resetting volatile state permits replay. Fresh trusted boot identity is therefore a required integration gate, not an implemented feature. All four new files compile for freestanding AArch64; 29 Python tests include exact dependency-source provenance checks. No real credential was created/imported. See [administration contract](ADMIN.md).

All four existing image profiles rebuild byte-for-byte unchanged and pass their QEMU regression suites. The new native administration transcript is saved separately as `artifacts/admin-core-tests.log`; unchanged guest behavior is not evidence of a live authentication service.

## 2026-09-20 — observed release-kernel isolation faults

Added a separate test-only release image with six child probes, a parent fault observer, and a dedicated policy-owned canary page. Actual QEMU faults reject ungranted UART addresses, unmapped policy-page reads/writes, writing through a read-only mapping, and fetching from a non-executable page. Kernel messages are checked for identity/order, shape, address, access type, and syndrome. The canary and the caller's live restricted policy metadata are checked before launch and after each fault. All six reports are required; silence is failure. The real terminal and serial-break suite runs afterward.

Native sanitized tests cover syndrome decoding and the actual observer adapter, including duplicate/wrong faults, state tampering, message-register clobbering, and output backpressure. Twenty-eight Python tests include 21 isolation-graph mutations and malformed/missing/partial runtime evidence. Normal graphs, service protocols, and saved ordinary image bytes are unchanged. Test supervision and page-sharing authority exist only in the separately checked test image. See [isolation evidence and limits](ISOLATION.md); this is not a claim of arbitrary driver containment, physical-hardware isolation, or general server recovery.

## 2026-09-20 — separate release-kernel terminal

Added a release-kernel terminal with separate objects, ELFs, SDK headers/libraries, image, and saved evidence. Every server checks its intended profile against generated kernel flags. Banners identify the profile; the emulator harness rejects mismatches, any release boot preamble, and diagnostic interleaving. The same six-domain graph is used, with no new authority or administrative endpoint. Debug seed and terminal targets remain available.

Local sanitized tests and 22 Python tests pass, including profile-guard, build-routing, and harness-negative cases. All three newly built images and their saved copies pass actual QEMU scenarios. Both terminal variants pass three injected serial breaks and Enter/Ctrl-C recovery. Debug evidence still checks audit sequence numbers; release evidence checks observable rejection/denial/recovery without claiming access to silent internal counters.

Testing caught a Make pattern-rule overlap that could link release servers against debug libraries; explicit static target sets now separate those paths, with a regression test for every server link. The release configuration removes the shared SDK debug-printing route, but does not add authenticated input, physical-hardware support, formal verification, fault supervision, or production readiness. The upstream `CONFIG_VERIFICATION_BUILD` name is not a verification claim.

## 2026-09-20 — visible editing and real UART break recovery

Added bounded safe echo, single-cell tab/delete behavior, cancellation display, and deferred command execution until echo is flushed. Transport loss produces an explicit notice and continues rejecting the incomplete line until Enter or Ctrl-C. The host terminal adapter test covers full/partial writes, ordering, loss, and malformed replies. Exhaustive single-byte echo tests cover all 256 input values.

The actual QEMU test now injects three serial breaks through a private temporary QMP socket. It observes the prefix before injection and the driver loss report afterward, verifies that interrupted commands do not reach policy, then proves Enter/Ctrl-C recovery using fresh commands and audit sequence numbers. The test matches exact response bytes rather than treating echoed prompt text as completion. A sandbox initially blocked the local socket; the authorized local-socket test passed. No TCP listener or guest network interface was added.

Limits remain explicit: no Unicode/full-screen editing, no physical UART evidence, no coverage claim for every overrun/queue-loss schedule, and the debug kernel still shares the output device.

## 2026-09-20 — live channel-bound self-status

The terminal's `status` command now queries actual policy metadata through the existing client/storage chain. Every request has zero words; policy binds the subject to its storage channel. Added a bounded four-word snapshot contract and strict reply decoding. No additional capabilities, administrator route, mutation, or audit append was introduced. Snapshot errors expose no metadata, and a snapshot never replaces the normal access check.

Local evidence: sanitized core and adapter tests (including all 1–64-word malformed requests at each hop, unknown callers/versions, malformed replies, no mutation/audit effects, and lifecycle freshness), actual QEMU seed lifecycle/status checks, and 80 repeated UART status queries followed by a read that still becomes audit record 1. Both rebuilt profiles and their saved images pass their emulator scenarios. GitHub Actions provides clean-machine validation per published commit.

## 2026-09-20 — isolated UART and working read-only terminal

Implemented a separate six-domain boot profile with a PL011 driver, private bounded receive queue, transport-loss marker, partial writes, bounded IRQ/terminal handlers, and UART-driven command processing. The terminal has no policy-admin endpoint. `help` and `version` work; reads reach the real policy/audit chain and are denied by default. `status` reports its missing endpoint instead of fabricating live state. The seed's existing administrative test profile is preserved separately.

Local evidence: sanitized policy/terminal/serial tests, 100,000 serial-buffer transitions, mocked driver IPC/register negative cases, 35 topology mutations across both profiles, optimized-Python checker regression, and scripted real QEMU UART cases. The script checks malformed/admin commands, integer boundaries, CRLF, editing, cancellation, line overflow, control bytes, denied reads, and recovery. Both development images and transcripts are saved under `artifacts/`; clean-machine evidence is tracked in GitHub Actions for each published commit.

Limits: no input echo yet; debug-kernel output still shares the UART; no exclusive/trusted display path, authentication, administrator terminal, or physical-hardware support. Runtime input-loss injection is still a verification gate; native driver tests model it but do not establish real-device behavior. See [terminal contract](TERMINAL.md).

## 2026-09-20 — terminal foundation and explicit communication permissions

Implemented:

- Allocation-free terminal line handling and a read-only command parser, with explicit length, integer bounds, CRLF handling, cancellation, and whole-line rejection after overflow or transport loss.
- Disabled unused notification permissions on all 12 seed channel ends. The six existing protected-call paths remain.
- Replaced optimization-sensitive assertions in the system checker with always-enabled validation. The checker now validates the entire seed resource schema, program identities, channel bindings, notification rights, and scheduling configuration, rejecting additional memory mappings, IRQs, child domains, and unknown attributes.
- Added native terminal tests, 100,000 deterministic input bytes, topology mutation tests, and a Python optimized-mode regression test.

The terminal core is not yet wired to a running terminal. No administrator authentication, UART driver, or new policy endpoint is claimed in this checkpoint. See [terminal contract](TERMINAL.md).

Local validation passed: sanitized policy and terminal tests, 50,000 policy transitions, 100,000 terminal input bytes, 11 Python tests (including the topology mutation cases), and the real QEMU IPC scenario with notification sending disabled. The rebuilt image is recorded in `artifacts/`; the original seed remains available in Git history. Remote validation is recorded per commit in [GitHub Actions](https://github.com/chasebryan/tcs/actions/workflows/check.yml).

## Next bounded increment

Implement the trusted boot/incarnation-freshness and public-key provisioning boundary, then add the separate admin server's private IPC adapter and policy-side conditional mutation/receipt protocol. Use the signed-request core only with an independently authorized generated key and a fresh boot identity; a static reusable test image cannot provide deployment replay safety. Ordinary terminal/driver messages must not impersonate that admin channel. Keep real credential provisioning operator-controlled and do not embed test credentials as deployment authority.

Continue toward authenticated interactive administration and then lifecycle supervision. Retain the distinction between specific probe-fault evidence and containment/recovery of a compromised real service.
