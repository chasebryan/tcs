# TCS development checkpoints

## Current direction

Continue through the roadmap in small, reviewable increments. Preserve the working seed, keep runtime claims narrower than the observed evidence, and never grant a new input path administrative authority by default.

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

Introduce a narrow, channel-bound self-status endpoint and wire `status` to actual policy state. Test that it cannot choose another subject, mutate policy, expose admin authority, or leak stale session state. Then add input echo and runtime transport-loss injection tests, and a separate release-kernel terminal profile to remove the shared debug output route.

Design the separate administration/authentication boundary before making any control command interactive. Continue into lifecycle supervision only after those boundaries have executable tests.
