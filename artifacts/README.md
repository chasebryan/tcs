# TCS boot artifacts

`loader.img` is the TCS debug image for QEMU AArch64. It contains the seL4 kernel, Microkit boot/runtime components, and five TCS servers. TCS source is in this repository; exact upstream source archives and notices are in [third_party/](../third_party/README.md) and `upstream-licenses/`.

`terminal.img` is the six-domain read-only terminal **debug-kernel** image. `terminal-boot.log` records scripted actual-UART tests; `terminal-report.txt` records its capability allocation. Run `make terminal-smoke-saved` to exercise it without a compiler.

`terminal-release.img` uses the same graph with the SDK's **release** kernel/runtime and separately linked servers. `terminal-release-boot.log` begins directly with its profile-tagged terminal banner; `terminal-release-report.txt` records construction. Run `make terminal-release-smoke-saved` without an SDK/compiler. This profile disables kernel/runtime debug printing. Its test checks terminal responses and serial-break recovery, but cannot observe the debug audit sequence. No administrative endpoint is exposed in either terminal profile. None of these images is a production release or proof of formal verification.

`isolation.img` is **test-only**, with six faulting probe domains and a parent observer added to the release terminal graph. `isolation-boot.log` contains the six checked kernel-fault reports, protected-state verdict, and subsequent terminal tests; `isolation-report.txt` records the additional test authority. Run `make isolation-smoke-saved`. The [evidence scope](../docs/ISOLATION.md) is specific memory/device accesses, not arbitrary driver compromise or production fault recovery. Normal images do not contain these probes or observer capabilities.

From the repository root, run the saved image without rebuilding:

```sh
make smoke-saved
```

This needs Python 3.9+, Make, and QEMU's `qemu-system-aarch64` on PATH, but no compiler or downloaded SDK. The test uses one CPU and 2 GiB of guest RAM, with no disk/network attachment. It finishes the automated scenario and stops the emulator; the image has no login prompt.

`boot.log` is a successful emulator transcript. `microkit-report.txt` records image construction and capability allocation. `build.json` records the tested configuration and image digest. `manifest.json` binds the saved evidence to the included TCS source/build inputs and upstream source archives. Run `make verify-artifacts` to check these hashes; source changes intentionally invalidate this saved snapshot until it is rebuilt and retested.

The records were produced by the same builder. They are not signed release manifests, formal proofs, or independent attestations. SHA-256 detects mismatches against the checked-out record; it does not authenticate a maliciously replaced record.
