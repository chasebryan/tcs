# TCS boot artifacts

`loader.img` is the TCS debug image for QEMU AArch64. It contains the seL4 kernel, Microkit boot/runtime components, and five TCS servers. TCS source is in this repository; exact upstream source archives and notices are in [third_party/](../third_party/README.md) and `upstream-licenses/`.

`terminal.img` is the six-domain read-only terminal **debug-kernel** image. `terminal-boot.log` records scripted actual-UART tests; `terminal-report.txt` records its capability allocation. Run `make terminal-smoke-saved` to exercise it without a compiler.

`terminal-release.img` uses the same graph with the SDK's **release** kernel/runtime and separately linked servers. `terminal-release-boot.log` begins directly with its profile-tagged terminal banner; `terminal-release-report.txt` records construction. Run `make terminal-release-smoke-saved` without an SDK/compiler. This profile disables kernel/runtime debug printing. Its test checks terminal responses and serial-break recovery, but cannot observe the debug audit sequence. No administrative endpoint is exposed in either terminal profile. None of these images is a production release or proof of formal verification.

`isolation.img` is **test-only**, with six faulting probe domains and a parent observer added to the release terminal graph. `isolation-boot.log` contains the six checked kernel-fault reports, protected-state verdict, and subsequent terminal tests; `isolation-report.txt` records the additional test authority. Run `make isolation-smoke-saved`. The [evidence scope](../docs/ISOLATION.md) is specific memory/device accesses, not arbitrary driver compromise or production fault recovery. Normal images do not contain these probes or observer capabilities.

`admin-core-tests.log` records the sanitized native signed-request and policy tests. This log is **not guest-authentication evidence**; trusted boot freshness and key provisioning remain required before live administration. See [the administration contract](../docs/ADMIN.md).

`operator-tests.log` records the public-context codec and ten [host operator workflow](../docs/OPERATOR.md) test groups. They exercise review/signing and file/failure semantics using public RFC fixtures only. No private operator identity, context, or request is shipped here. The new host context format is not accepted by any current image; these tests are not guest provisioning or power-loss evidence.

`boot-test.img` is the separate two-domain [boot-context experiment](../docs/BOOT-CONTEXT.md). It executes real signature verification with public fixtures and host-generated launch identities, but has no policy server or administrative authority. `boot-context.log` records 15 actual guest cases including malformed/missing context, key/realm/signature mismatch, replay, DMA refusal, and reset termination; `boot-test-report.txt` records construction. `make boot-test-smoke-saved` needs a native C compiler for the public fixture helper, but not an SDK/cross-compiler. This is not operator provisioning or snapshot/rollback protection.

`admin-test.img` is the eight-domain [signed administration IPC test](../docs/ADMIN-IPC.md), with an administrator, private conditional policy endpoint, and test-only fixture provider. `admin-ipc-boot.log` records 12 real signed commands, definitive receipts, and subsequent UART denials; `admin-test-report.txt` records its exact construction. `admin-ipc-tests.log` records sanitized adapter/malformed-receipt tests. Run `make admin-test-smoke-saved` with a native C compiler for public fixtures; no SDK download is needed. It uses no operator credentials and does not offer an interactive administration command.

From the repository root, run the saved image without rebuilding:

```sh
make smoke-saved
```

This needs Python 3.9+, Make, and QEMU's `qemu-system-aarch64` on PATH, but no compiler or downloaded SDK. The test uses one CPU and 2 GiB of guest RAM, with no disk/network attachment. It finishes the automated scenario and stops the emulator; the image has no login prompt.

`boot.log` is a successful emulator transcript. `microkit-report.txt` records image construction and capability allocation. `build.json` records the tested configuration and image digest. `manifest.json` binds the saved evidence to the included TCS source/build inputs and upstream source archives. Run `make verify-artifacts` to check these hashes; source changes intentionally invalidate this saved snapshot until it is rebuilt and retested.

The records were produced by the same builder. They are not signed release manifests, formal proofs, or independent attestations. SHA-256 detects mismatches against the checked-out record; it does not authenticate a maliciously replaced record.
