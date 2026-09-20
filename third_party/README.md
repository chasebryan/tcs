# Upstream source accompanying the seed image

These unmodified source archives accompany the upstream components in `artifacts/loader.img`. They are source distributions, not executables to run. Original copyright, license, and attribution files remain inside every archive. TCS's top-level AGPL license does not replace their licenses.

| Component | Immutable source revision | Role |
| --- | --- | --- |
| seL4 | `6e7c3b733d296cfd88d5fbf635c96e447a882374` | Microkernel and its user API |
| Microkit | `8780fab8699f5aeec109b21325ff37c741736b24` (2.3.0) | Loader, monitor, runtime library, initialiser, system builder, board configurations |
| rust-sel4 | `dbe6445d56059ed9a757e53c7137892aece1d179` | Initialiser/runtime dependencies |
| Rust registry packages | Exact versions and checksums in `microkit-Cargo.lock` | Locked initialiser and SDK-tool dependency sources |

Kernel and Microkit revisions come from the [2.3.0 release manifest](https://github.com/seL4/microkit-manifest/blob/2.3.0/default.xml), copied as `microkit-manifest.xml`. rust-sel4 and crate pins come from [Microkit's lockfile](https://github.com/seL4/microkit/blob/2.3.0/Cargo.lock), copied without changes as `microkit-Cargo.lock`.

`sources.json` records every archive's upstream URL, revision/version, size, and SHA-256. Registry archives were checked against the upstream Cargo.lock checksums. GitHub source archives were fetched by full commit ID and their archive hashes recorded locally. `make verify-artifacts` checks the distributed copies against the saved record.

## Rebuilding the substrate

The normal TCS build consumes the hash-pinned official SDK; it does not compile the kernel. To develop the substrate itself, extract the matching Microkit and seL4 source archives into separate directories, install the prerequisites in Microkit's `DEVELOPER.md`, and use its included `build_sdk.py`. The selected TCS configuration is:

```sh
python3 build_sdk.py --sel4=/path/to/seL4 --boards=qemu_virt_aarch64 --configs=debug --skip-docs
```

Run this from the extracted Microkit source directory. The upstream developer instructions, `flake.lock`, `Cargo.lock`, and build scripts define its dependencies; its Python requirement is newer than TCS's own helper scripts. Rust crate/source archives are included for inspection and source availability; configuring Cargo to use an offline vendor directory is a separate setup step. Compilers, Rust standard libraries, host packages, and the full SDK binary archive are not bundled here.

An SDK built this way has **not** been independently compared bit-for-bit to the official SDK used for this seed. Rebuilding TCS against a different SDK requires a fresh build directory, retesting, and a new evidence record. No kernel source modifications were made for TCS.
