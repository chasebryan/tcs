# TCS dependencies and provenance

Original TCS source continues the local CINIX seed and uses the AGPL-3.0 license already present in this repository; see `LICENSE`. No implementation code from the user's other repositories was imported. The upstream license file has been preserved unchanged.

The generated image contains upstream components from the [seL4 Microkit 2.3.0 SDK](https://github.com/seL4/microkit/releases/tag/2.3.0). seL4 kernel material is GPL-2.0-only; Microkit/runtime components have their own notices, including BSD-2-Clause material. Upstream components are not relicensed under TCS's AGPL license. The original SDK license inventory is retained in `artifacts/upstream-licenses/`; original copyright/license notices remain in the bundled source archives. Rust dependencies retain the licenses in their individual archives.

Exact kernel, Microkit, rust-sel4, and locked Rust dependency source distributions accompany the image in [third_party/](third_party/README.md). This includes upstream build scripts/configuration and the release manifest. TCS's original server source and build scripts are in this repository. The kernel and TCS userspace are separate programs communicating through the seL4 interface.

[Zig 0.14.1](https://ziglang.org/download/0.14.1/release-notes.html) is used as a cross-compilation tool. [QEMU](https://www.qemu.org/) runs the boot test. Neither tool distribution is bundled with TCS source.

Primary technical references:

- [Microkit overview](https://docs.sel4.systems/projects/microkit/)
- [Microkit releases and publisher signatures](https://docs.sel4.systems/releases/microkit.html)
- [Microkit 2.3.0 manual source](https://github.com/seL4/microkit/blob/2.3.0/docs/manual.md) — exact compiled headers come from SDK 2.3.0
- [seL4 capabilities](https://docs.sel4.systems/Tutorials/capabilities.html)
- [seL4 IPC](https://docs.sel4.systems/Tutorials/ipc)
- [seL4 verified configurations](https://docs.sel4.systems/projects/sel4/verified-configurations.html)
