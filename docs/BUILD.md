# Build and run TCS Seed

A fresh checkout contains all TCS source, system configuration, tests, a saved boot image, and upstream source archives. It does not require another workspace or an installed TCS system. Build tools are downloaded on first bootstrap; this is not an air-gapped or self-hosting SDK.

The guest target is `qemu_virt_aarch64`, one emulated Cortex-A53 CPU and **2 GiB guest RAM**. Seed and ordinary terminal targets use the debug configuration; `terminal-release-*` targets separately use the release configuration. The RAM size is part of the upstream kernel configuration. The runner attaches no disk or network device. There is no Linux kernel or distribution inside the guest.

## Prerequisites

Supported build hosts: Linux x86_64, Linux AArch64, and Apple Silicon macOS. Use a checkout path without spaces (the Make dependency paths require this). Native tests need a C11 compiler with address/undefined-behavior sanitizers, Make, and Python 3.9+. Bootstrap also needs curl, tar, gzip, and xz. Boot tests need `qemu-system-aarch64` on PATH.

Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install -y build-essential python3 curl ca-certificates tar xz-utils git qemu-system-arm
```

macOS: install Apple's Command Line Tools if needed (`xcode-select --install`), then install QEMU and xz using your package manager (Homebrew: `brew install qemu xz`). Ensure Python 3.9+ is available as `python3`.

## Fresh checkout

```sh
git clone https://github.com/chasebryan/tcs.git
cd tcs
make test
make bootstrap
make smoke
```

`make bootstrap` selects the host-specific versions in `tools/toolchains.json`: Microkit SDK **2.3.0** and Zig **0.14.1**. It downloads to `.tools/`, checks SHA-256 before extraction, and never runs an installer. Hashes come from the official release metadata. Detached publisher signatures have not been independently checked.

Bootstrap can be rerun. It rechecks the cached archive hash and reuses directories bearing its completion marker. The marker records extraction provenance; it does not prove the extracted files have not subsequently changed. Unknown existing directories are preserved and rejected. For a new extraction use `make bootstrap TOOLS_DIR=.tools-fresh`, followed by `make smoke TOOLS_DIR=.tools-fresh`. A partial download/extraction is never installed as a completed tool.

## Targets

| Command | Result |
| --- | --- |
| `make test` | Sanitized policy/terminal/serial and self-status IPC tests, deterministic transitions/input, strict graph/mutation checks, and offline bootstrap tests; no downloads |
| `make bootstrap` | Download and extract pinned SDK/compiler |
| `make operator-tools` | Build experimental host review/signing tool; no credential creation or guest provisioning |
| `make operator-test` | Sanitized public-context and host workflow tests using only public RFC fixtures |
| `make interactive-image` | Build experimental operator-mode signed UART image into `build/operator/interactive.img`; no credential creation |
| `make interactive-fixture-image` | Build separately labeled public-fixture signed UART image; never deploy |
| `make interactive-smoke` | One-shot host launcher, signed UART lifecycle/replay, and real serial-break tests using only public fixtures |
| `make interactive-smoke-saved` | Test both saved signed UART images; native fixture compiler needed, no SDK |
| `make boot-test-smoke` | Build/run test-only trusted-launch transport and guest Ed25519 cases; public fixtures only |
| `make boot-test-smoke-saved` | Run included boot-context test image; native C compiler required for fixture helper, no SDK required |
| `make admin-ipc-test` | Sanitized real administrator/policy adapters with mocked transport and corrupted receipts |
| `make admin-test-smoke` | Build/run separate signed administration IPC test profile with public fixtures |
| `make admin-test-smoke-saved` | Run saved administration test image; native C fixture compiler required, no SDK required |
| `make admin-cross-check` | Compile signed-administration, public launch-context codec, and pinned Ed25519 code for AArch64; no new endpoint/image |
| `make image` | Build five server ELFs and `build/loader.img` |
| `make smoke` | Build, boot, require `TCS SEED PASS`, save `build/boot.log`, stop the emulator |
| `make verify-artifacts` | Check the saved image, recorded source inputs, notices, and upstream archives |
| `make smoke-saved` | Verify and boot `artifacts/loader.img`; no SDK/compiler needed |
| `make terminal-image` | Build the separate six-domain UART terminal into `build/terminal.img` |
| `make terminal-smoke` | Script UART editing/commands and injected serial breaks; save `build/terminal-boot.log` |
| `make terminal-smoke-saved` | Verify and exercise `artifacts/terminal.img`; no SDK/compiler needed |
| `make terminal-run` | Interactive terminal with bounded echo/editing; Ctrl-A, then X exits QEMU |
| `make terminal-release-image` | Build separate release objects/ELFs and `build/release/terminal.img` |
| `make terminal-release-smoke` | Test release-kernel UART responses, no debug preamble, and serial-break recovery |
| `make terminal-release-smoke-saved` | Verify and test `artifacts/terminal-release.img`; no SDK/compiler needed |
| `make terminal-release-run` | Interactive release-kernel terminal; Ctrl-A, then X exits QEMU |
| `make isolation-image` | Build test-only observer/probes and `build/isolation/isolation.img` using the release kernel |
| `make isolation-smoke` | Observe six required memory/device faults, check protected state, then exercise the UART terminal |
| `make isolation-smoke-saved` | Verify and test `artifacts/isolation.img`; no SDK/compiler needed |

The smoke test waits at most 30 seconds for a verdict. The guest deliberately idles after the automated scenario; no login prompt is expected. The runner terminates only its own emulator process. Generated build files and compiler caches are confined to `build/` (or the specified `BUILD_DIR`).

Paths may be overridden explicitly:

```sh
make smoke BUILD_DIR=build-custom MICROKIT_SDK=/path/to/microkit-sdk-2.3.0 \
  ZIG=/path/to/zig QEMU=/path/to/qemu-system-aarch64
```

Use a fresh build directory when changing SDK/compiler paths or versions; Make does not fingerprint tool executables. This milestone checks the exact SDK/compiler version but does not rebuild the SDK itself. See [upstream source](../third_party/README.md) for kernel/runtime source and upstream rebuild instructions.

## Kernel profile separation

Do not set `CONFIG=release` on debug targets; the build rejects that ambiguous override. Release targets place every object and ELF under `$(BUILD_DIR)/release`, use only the release SDK include/library paths, and construct the image with `--config release`. Both variants use `system/terminal.system`; no additional authority is granted by choosing release. Avoid concurrent Make invocations against the same build directory.

Every server includes a compile-time guard after the SDK headers. Release requires `CONFIG_DEBUG_BUILD` and `CONFIG_PRINTING` to be absent and the pinned release configuration's `CONFIG_VERIFICATION_BUILD` to be present. Debug requires the debug/printing flags. The latter flag name is an upstream build setting, **not evidence of a verified TCS system or this kernel configuration**. Native IPC fixtures are explicitly marked host-only and are rejected in a freestanding build. Header guards and build-plan tests catch accidental profile mixing; they do not authenticate a modified SDK, compiler, Makefile, or build host.

The terminal banner and `version` response include `debug-kernel` or `release-kernel`. The smoke harness rejects a mismatched image, and release testing requires no serial output before its banner and no diagnostic interleaving with subsequent responses. Debug runs retain audit sequence observations; the silent release run makes no internal audit-sequence claim. See [terminal limits](TERMINAL.md).

The explicitly selected `--isolation` harness mode requires six structured fault reports and protected-state checks before that banner. This mode is only for `system/isolation.system`, never an exception to normal release-image expectations. See the [test authority and evidence boundaries](ISOLATION.md).

## Local emulator control

The terminal smoke tests use a private temporary Unix-domain socket under `/tmp` for QEMU control, with no TCP listener or guest network device. The harness negotiates QMP and injects UART breaks, then closes the socket and removes its own temporary directory. Sandboxed environments must permit this local socket. A bind-permission error is a host test-environment limitation, not evidence that the guest boot failed. Interactive `terminal-run` does not require this test-control socket.

## macOS linker troubleshooting

If native tests fail on a system-library `.tbd` mentioning an unsupported architecture such as `arm64e.x1`, select a compatible installed Apple SDK with `SDKROOT` and retry using a fresh `BUILD_DIR`. The initial host passed with:

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk make test
```

That path is an example, not a TCS dependency; it must exist on your host. Matching/updating the installed Command Line Tools and SDK is the general fix. Freestanding TCS compilation does not use the Apple SDK.

## Automation and saved evidence

The [GitHub Actions workflow](https://github.com/chasebryan/tcs/actions/workflows/check.yml) runs native tests, verifies saved evidence, bootstraps twice, builds on Ubuntu, and boots all eight newly built and saved image profiles. Consult the run for the exact commit, not the existence of the workflow alone.

The saved image and transcript are local development evidence, not signed releases or independent security attestations. After intentionally changing source, build and test a fresh image before updating `artifacts/` and running `python3 tools/verify_artifacts.py --record`. Never regenerate the record merely to hide an unexplained mismatch. Byte-for-byte independent reproducibility is not claimed.

Signed-administration and [host operator workflow](OPERATOR.md) tests run within `make test` using included, unmodified Monocypher 4.0.3 source; no additional download/package manager is needed. Keys in those tests are public RFC fixtures, stored only in private temporary test directories. No actual operator key file or ambient/environment credential is read. Local Linux/macOS filesystem mode/ACL behavior is required for the operator tests. See [the protocol and deployment gates](ADMIN.md).

Interactive signed profiles use separate `operator/` and `interactive-fixture/` objects for terminal, administrator, and bootstrap, sharing only mode-independent release libraries/servers. Their required compile-time mode cannot be selected by incoming data. The one-shot launcher disables QEMU monitor controls; use host process termination to stop it. Ctrl-C cancels guest input and Ctrl-A/X is not an exit shortcut in this profile. See [the complete launch lifecycle](OPERATOR.md#one-shot-launch-and-terminal-use).
