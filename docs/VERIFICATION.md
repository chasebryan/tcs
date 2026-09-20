# TCS seed verification — 2026-09-20

Observed locally on an Apple Silicon macOS host. Build: Microkit 2.3.0, `qemu_virt_aarch64/debug`, Zig 0.14.1; emulator: QEMU 11.1.1 using TCG.

| Check | Observed result |
| --- | --- |
| Native policy tests | Pass under address and undefined-behavior sanitizers |
| Terminal input core | Read-only command allowlist; exact-span parsing; integer overflow; line overflow, control-byte rejection, and recovery under sanitizers |
| Terminal byte sequences | 100,000 deterministic input bytes preserve bounded-buffer invariants; not a coverage-guided fuzzing claim |
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
