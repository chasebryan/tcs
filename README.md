# TCS — Typed Capability System

TCS is a new operating system built from isolated, communicating servers on a microkernel substrate. Its security direction is explicit authority that can be reduced and restored under policy as conditions change.

Repository: [chasebryan/tcs](https://github.com/chasebryan/tcs). TCS replaces the working name CINIX. The project continues from the bootable seed developed under that name.

**Current milestone: 0.1.0 Seed.** Boots seL4/Microkit in AArch64 QEMU and runs five native protection domains. The boot scenario exercises access grants, per-request checks, session revocation, quarantine, recovery, and audit exhaustion across actual kernel IPC.

The console runs an automated scenario; it is not yet an interactive terminal. Storage serves one read-only fixture. Hardware drivers, persistent storage, signed updates, dynamic process creation, and a user login system come in later milestones. The typed capability design will extend the seed's versioned operation contracts; compile-time typed capability handles are not implemented yet.

## Run

Host tests require a C11 compiler, Make, and Python 3.9+:

```sh
make test
```

For the microkernel build, install QEMU (`qemu-system-aarch64`), then obtain the pinned SDK/compiler:

```sh
git clone https://github.com/chasebryan/tcs.git
cd tcs
make bootstrap
make test
make smoke
```

The same commands work on supported Linux x86_64/AArch64 and Apple Silicon hosts. The smoke test uses an emulated Cortex-A53, 2 GiB of guest RAM, one CPU, no network interface, and no attached disk. Success ends with `TCS SEED PASS`. It then stops its own QEMU process. See [build details](docs/BUILD.md) for prerequisites and a macOS SDK workaround.

To boot the included image without downloading a compiler or SDK, install QEMU and run `make smoke-saved`. This is a standalone bootable seed, not yet a complete general-purpose OS or an offline toolchain distribution.

## First server graph

```mermaid
flowchart LR
    console -->|test request| client
    client -->|read| storage
    storage -->|check access| policy
    console -->|administer| policy
    policy -->|record decision| audit
    console -->|read audit| audit
```

Arrows are allowed protected calls. Kernel-assigned channel identities determine the caller's authority. The client has no policy-admin channel. Each storage operation checks its current subject, object, rights, and session generation before releasing the fixture.

The dynamic behavior currently changes application authorization inside a fixed kernel capability graph. Generation numbers are subject-bound session identifiers; they are not transferable seL4 capabilities. Seed revocation does not delete kernel capabilities, unmap memory, stop a process, or retract data already read. Those are later lifecycle requirements.

## Source map

| Path | Purpose |
| --- | --- |
| `system/tcs.system` | Five protection domains and their permitted communication paths |
| `servers/` | Freestanding native Microkit programs |
| `lib/policy.c` | Allocation-free policy state machine shared by runtime and host tests |
| `include/tcs/` | Policy types and versioned IPC definitions |
| `tests/policy_test.c` | Invariants, negative cases, audit failure, counter exhaustion, transition sequences |
| `tools/` | Pinned toolchain retrieval, topology check, automated QEMU boot |
| `docs/` | Architecture, protocol, build instructions, roadmap, verification record |
| `artifacts/` | Saved image, transcript, construction report, and hash record |
| `third_party/` | Immutable upstream source archives and their provenance |

[Architecture](docs/ARCHITECTURE.md) · [IPC protocol](docs/PROTOCOL.md) · [Roadmap](docs/ROADMAP.md) · [Verification](docs/VERIFICATION.md) · [License](LICENSE) · [Upstream dependencies](NOTICE.md)

The [saved boot image and transcript](artifacts/README.md) can be inspected or run without rebuilding.
