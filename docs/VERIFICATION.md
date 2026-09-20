# TCS seed verification — 2026-09-20

Observed locally on an Apple Silicon macOS host. Build: Microkit 2.3.0, `qemu_virt_aarch64/debug` (seed/terminal) and `qemu_virt_aarch64/release` (separate terminal), Zig 0.14.1; emulator: QEMU 11.1.1 using TCG.

| Check | Observed result |
| --- | --- |
| Native policy tests | Pass under address and undefined-behavior sanitizers |
| Terminal input core | Read-only command allowlist; exact-span parsing; integer overflow; line overflow, control-byte rejection, and recovery under sanitizers |
| Terminal byte sequences | 100,000 deterministic input bytes preserve bounded-buffer invariants; not a coverage-guided fuzzing claim |
| Terminal echo | All 256 byte values classified; bounded generated feedback, tab/backspace/delete cells, CRLF/cancel, no raw rejected controls |
| Terminal backpressure | Sanitized runtime adapter test verifies partial/full TX, echo-before-service-call ordering, transport-loss rejection and fail-stop on malformed driver replies |
| Runtime serial faults | Three QMP-injected UART breaks, with loss reports, rejection of interrupted commands, Enter/Ctrl-C recovery, and subsequent audit sequence checked in real QEMU |
| Release-kernel UART | Same commands, editing, status, denials, and three break/recovery cases pass with no boot preamble or debug diagnostics; no internal audit-counter observation in release |
| Profile guards | Valid debug/release/host-fixture headers accepted; missing/conflicting/mismatched flags rejected; host fixture rejected for freestanding compilation |
| Build profile routing | All six release ELFs link only release libraries; debug build remains separate; ambiguous CONFIG override rejected |
| Harness profile checks | Split banners accepted; wrong profile and release preamble rejected; debug monitor faults rejected; audit output expected only for debug |
| Serial queue | Wrap, overflow/loss ordering, and 100,000 deterministic transitions under sanitizers |
| Serial adapter | Mocked registers/IPC: malformed word counts, invalid byte values, unknown caller, full TX readiness, unrelated IRQ preservation, 64-read IRQ bound, error propagation |
| Terminal graph | Exact six-domain graph, UART mapping/IRQ only in serial, one-way driver notification, no policy-admin or audit-query route; 15 negative mutations |
| Self-status adapters | Sanitized real C adapters with host IPC routing: zero-word request enforcement at all three hops, caller binding, lifecycle freshness, no mutation/audit append, invalid reply normalization |
| Self-status kernel path | QEMU seed checks current grant/revoke/regrant/quarantine/restore state, different-subject isolation, payload rejection, admin-channel rejection, and metadata after audit-failed revocation |
| Self-status terminal | More than 64 consecutive UART status queries leave audit capacity intact; first later read is audit record 1; subject-selecting commands rejected |
| UART runtime | Actual QEMU serial commands, default-denied reads through policy, CRLF, editing, cancellation, control bytes, overflow and recovery; separate saved image and transcript |
| Transition sequences | 50,000 deterministic steps; generation monotonicity, subject separation, state/right consistency, and authorized grants checked |
| Graph validation | Five domains, six declared channel pairs, exact program/identity bindings, no extra resources, increasing RPC priorities, and all unused notification rights disabled |
| Graph rejection tests | Extra memory/device/IRQ/child authority, wrong images/identities, altered scheduling, duplicate/missing elements, and implicit notification rights rejected; checks stay active under Python -O |
| Cross-compilation | Five freestanding AArch64 server ELFs linked against the pinned SDK |
| Image construction | Microkit constructed the seL4 system image |
| QEMU boot | Kernel entered userspace; all five TCS domains started |
| Runtime access scenario | Default denial; grant/read; wrong-object and write rejection; revoke; stale session denial after regrant; quarantine; explicit restore without access |
| Runtime protocol negatives | Malformed storage request and attempted grant through storage rejected |
| Audit | Record readback matched the original denial; bounded capacity exercised; new access denied when full |
| Audit-failure reduction semantics | Host tests confirmed revoke/quarantine still reduce authority when logging fails |

The boot transcript ends with `TCS SEED PASS`. Copies of the final transcript and image digest are retained with the delivered artifacts. Native tests also cover invalid actor roles, invalid subjects/opcodes, generation exhaustion, and no unauthorized mutation on malformed requests.

The first boot attempt used 1 GiB RAM and halted inside seL4 because this SDK expects 2 GiB. The runner was corrected to match the board definition; the successful run used 2 GiB. The native macOS test linker also needed the installed 15.4 SDK selected explicitly; see BUILD.md.

These checks do not establish formal verification, production readiness, fault recovery, hardware support, cryptographic audit integrity, dynamic kernel-capability revocation, or security under concurrent in-flight operations. Independent bit-for-bit reproducibility and external review have not been established. Current remote validation is recorded per commit in [GitHub Actions](https://github.com/chasebryan/tcs/actions/workflows/check.yml).

The ordinary terminal still uses the debug kernel and its alternate UART writer. The release-kernel terminal disables that SDK printing route and passes the same observable terminal scenarios, with separate saved image/report/transcript. It does not establish authenticated input, a trusted-display proof, or production readiness. The upstream `CONFIG_VERIFICATION_BUILD` flag does not establish formal verification of this configuration or TCS. Native mocked register tests are not physical-hardware evidence. See [terminal limits](TERMINAL.md).
