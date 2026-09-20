# TCS development checkpoints

## Current direction

Continue through the roadmap in small, reviewable increments. Preserve the working seed, keep runtime claims narrower than the observed evidence, and never grant a new input path administrative authority by default.

## 2026-09-20 — terminal foundation and explicit communication permissions

Implemented:

- Allocation-free terminal line handling and a read-only command parser, with explicit length, integer bounds, CRLF handling, cancellation, and whole-line rejection after overflow or transport loss.
- Disabled unused notification permissions on all 12 seed channel ends. The six existing protected-call paths remain.
- Replaced optimization-sensitive assertions in the system checker with always-enabled validation. The checker now validates the entire seed resource schema, program identities, channel bindings, notification rights, and scheduling configuration, rejecting additional memory mappings, IRQs, child domains, and unknown attributes.
- Added native terminal tests, 100,000 deterministic input bytes, topology mutation tests, and a Python optimized-mode regression test.

The terminal core is not yet wired to a running terminal. No administrator authentication, UART driver, or new policy endpoint is claimed in this checkpoint. See [terminal contract](TERMINAL.md).

Local validation passed: sanitized policy and terminal tests, 50,000 policy transitions, 100,000 terminal input bytes, 11 Python tests (including the topology mutation cases), and the real QEMU IPC scenario with notification sending disabled. The rebuilt image is recorded in `artifacts/`; the original seed remains available in Git history. Remote validation is recorded per commit in [GitHub Actions](https://github.com/chasebryan/tcs/actions/workflows/check.yml).

## Next bounded increment

Implement and test an isolated, bounded PL011 serial driver plus a separate read-only terminal boot profile. Keep the seed test console and its administrative authority out of this profile. Add scripted QEMU input tests that prove the actual UART-to-terminal path, invalid-input recovery, and absence of a policy-admin route.

After that: introduce a narrow self-status service contract, then design the separate administration/authentication boundary before making any control command interactive. Continue into lifecycle supervision only after those boundaries have executable tests.
