# Experimental trusted-host operator workflow

Status: experimental host review/signing, a one-shot launcher, and a separate signed UART guest are implemented. End-to-end positive tests use **public RFC fixtures only**; the separate operator guest rejects those fixtures. No real operator credential was created/imported or used for a successful login. This host-held-key option does not settle the outstanding login-method choice or provide production provisioning, trusted display, or restart recovery.

## What works

`make operator-tools` builds `build/tcs-operator` from included C sources and pinned Monocypher 4.0.3. No downloads, key generation, ambient key lookup, network, serial connection, or guest launch occur during this build.

The command sequence is explicit:

1. `create` creates a new local identity containing a random realm and a private Ed25519 seed. It requires `--acknowledge-experimental` and a new directory.
2. `context` reads only the public identity and prepares a fresh, single-launch nonce in a separate new session directory. It also requires the acknowledgement flag. It does not start a guest; use `launch` once for that session.
3. `review` reads only public host-owned data and displays the exact realm, public key, boot identity, operation, subject, sequence, expected generation, object, and rights. It emits an approval digest bound to those values.
4. `sign` recomputes that review and requires its exact approval digest. Only then does it open the private seed, derive its public key, compare it with the selected identity/context, sign the canonical message, verify its own signature, and exclusively create `request-SEQUENCE.bin` in the session directory. It does not submit or execute the request.
5. `launch` reads only public identity/context, reserves the session exclusively, copies the selected image into the private session directory, then replaces itself with a fixed diskless/networkless QEMU process. It never opens the private seed. A second launch is refused, including after failed writes or failed execution. See the precise lifecycle limits below.

The grant operation is fixed to object 42, READ=1; other operations carry zero object/rights. Supported operations are `grant`, `revoke`, `quarantine`, and `restore`. Subjects are 0–3. Sequences start at 1, and sequence/generation arguments are canonical unsigned decimal integers up to `UINT64_MAX`, with no signs, leading zeroes, whitespace, or wrapping.

## Explicit operator use — not needed for tests

The following are syntax examples, **not setup steps run by the build or agent**. Use absolute real paths outside a Git checkout, on a trusted local Linux/macOS filesystem. The private seed is unencrypted. Do not create a real credential for deployment until the remaining integration/recovery gates are satisfied.

```text
build/tcs-operator create /absolute/private/operator --acknowledge-experimental
build/tcs-operator show /absolute/private/operator
build/tcs-operator context /absolute/private/operator /absolute/private/launch-1 --acknowledge-experimental
build/tcs-operator review /absolute/private/operator /absolute/private/launch-1 grant 1 1 0
build/tcs-operator sign /absolute/private/operator /absolute/private/launch-1 grant 1 1 0 APPROVAL_FROM_REVIEW
build/tcs-operator launch /absolute/private/operator /absolute/private/launch-1 /absolute/tcs/artifacts/interactive.img /absolute/qemu-system-aarch64 --acknowledge-experimental
```

Read the entire review before approving. The digest is a check against accidental command/context changes, not a password, independent authorization, or a trusted display proof. Anyone with access to the public files can compute it. The local operator, host, and executable remain trusted. Never derive automatic follow-up requests from unauthenticated serial output. Losing a response does not permit guessing the next sequence or retrying an uncertain action.

## One-shot launch and terminal use

Build the guest with `make interactive-image`, or use the saved `artifacts/interactive.img`. `launch` requires absolute image/QEMU paths and rejects commas/control characters; it does not invoke a shell or accept extra QEMU options. The selected executable, image, host, and filesystem are trusted inputs, not authenticated release assets. Image reads are bounded to 64 MiB, regular-file-only, and do not follow a final symlink. Parent image-path components are not subject to the credential-directory walker.

Before copying the image or executing QEMU, the launcher exclusively creates and synchronizes `launch.used`, an exact public-context copy. It then exclusively writes/synchronizes `launch.img`, pins its working directory to the already-open private session directory, closes all descriptors other than standard input/output/error, and executes QEMU with fixed relative snapshot names. Original image-path changes cannot redirect subsequent QEMU reads. Same-user/root modifications to private files and in-place mutation during copying remain outside this trusted-host model. Copies are retained; failures never remove a reservation or partial file.

The guest receives the public context through `opt/tcs/launch-context`, with firmware DMA disabled. It has one CPU, 2 GiB RAM, TCG, no disk/network, no monitor/QMP interface, and `-no-reboot`. Only bootstrap owns the firmware page; only serial owns UART/IRQ; terminal can submit signed packets to administrator, never call policy administration directly. Bootstrap has no terminal endpoint or request-file reader.

The launcher uses raw-image loading from its private copy. QEMU's [raw loader](https://github.com/qemu/qemu/blob/master/hw/core/loader.c) opens the image for a seek-to-end size probe and again for loading; direct `/dev/fd` image handoff failed in the macOS live test because those opens share an offset. The private-copy path is exercised by the real emulator suite, not just a fake executable.

At `tcs>`, enter `submit`, then exactly **384 lowercase hexadecimal digits** encoding the 192-byte signed request, and press Enter. No spaces, prefixes, uppercase, Backspace, or other editing are accepted in packet mode; Ctrl-C cancels. A host command such as `od -An -v -tx1 /absolute/private/launch-1/request-1.bin | tr -d ' \n'` prints the public packet for manual transfer. Pace input: the current UART has a small bounded queue. Any detected input loss invalidates the entire frame until Enter/Ctrl-C; it never submits a suffix.

The guest displays a correlated execution receipt with sequence, policy decision, audit status, applied flag, resulting state, and generation. State values are 0 restricted, 1 active, 2 quarantined; a decision of 0 alone does **not** mean applied. A malformed policy completion produces `ADMIN UNCERTAIN`, never a claim that execution was rejected: policy may already have changed and administrator stays pending. Do not retry or advance sequence. `status`/`read <generation>` use the ordinary client path. These terminal bytes are not authenticated host receipts. Plaintext `grant`, `revoke`, `quarantine`, and `restore` remain invalid.

There is no QEMU Ctrl-A monitor escape in this launcher, and Ctrl-C is sent to the guest for cancellation. Stop its process using your host process controls (for example, terminate its PID from another terminal). Shutdown ends the experiment; do not resume, reset, copy, restore, or relaunch the same session. A new invocation needs a new context and starts with restricted, volatile policy. This is not durable state migration, snapshot prevention against a privileged host, or service-restart recovery.

## Files and creation rules

Private directories must be owned by the current user with mode 0700. All input/output files, including public context, use 0600. File reads require an exact bounded size, a regular file, current-user ownership, one hard link, and no extended ACL. The path walker rejects symlink components, `.`/`..`, repeated/trailing slashes, relative paths, and Git repository/worktree markers in ancestors. The tool rejects set-user/group-ID execution and disables ordinary core dumps. It never uses an environment variable to locate a credential.

On macOS the ACL check uses the native extended-ACL API; on Linux it rejects POSIX access/default ACL attributes. Unsupported ACL errors fail closed (Linux filesystems reporting ACL unsupported are accepted under their mode-bit model). Network filesystems, alternate ACL/security models, backups, hostile same-user processes, root/hypervisor access, swap, debugger access, and physical side channels are not covered. Restrictive modes are not encryption. Secret buffers are explicitly wiped on normal completion/error paths, but there is no memory-locking or guaranteed cleanup after process termination.

Creation uses exclusive descriptor-relative opens; it never truncates or replaces an existing file. Writes handle short progress, and file/directory synchronization is requested before reporting success. A failed or uncertain write leaves its partial output present so a later attempt cannot silently overwrite it. A partial identity directory may likewise remain. The operator must inspect such a failure; do not delete a request reservation and retry an uncertain operation. This is not a power-loss/durable replay-state proof. Owner-controlled deletion, copying a session directory, snapshot rollback, and context reuse can defeat these local reservations.

`create` gets 64 bytes from the host OS `getentropy` interface: the first 32 are a seed and the next 32 the realm. `context` obtains a separate 32-byte boot nonce. Entropy failure has no fallback and creates no new directory. These are trusted-host CSPRNG assumptions, not measured entropy evidence. There is no seed/key import command, rotation, password storage, or hardware-backed key support.

## Exact formats

All three files have 16-byte headers, with mode at byte 15: 0 for experimental operator, 1 for public test fixture. All unused header bytes are zero. Expected mode comes from the executable/API caller, never a file-selected mode.

| File | Bytes | Header prefix | Body |
| --- | --- | --- | --- |
| `identity.key` | 80 | ASCII `TCS-KEY`, byte 1 | realm at 16, seed at 48; each 32 bytes |
| `identity.pub` | 80 | ASCII `TCS-PUB`, byte 1 | realm at 16, public key at 48; each 32 bytes |
| `launch.context` | 112 | ASCII `TCS-LAUNCH`, byte 1 | realm at 16, boot at 48, public key at 80; each 32 bytes |

This launch header is deliberately different from the existing `TCS-BOOT` guest-test format. No image silently upgrades its trust model. The allocation-free codecs check exact length/header/mode and nonzero public fields; they do not prove arbitrary public-key validity, human identity, independent authorization, nonce entropy, or freshness. The signer derives its public key from the stored seed; it never accepts an attacker-supplied 64-byte seed/public-key bundle. Public/context mismatch or private-seed mismatch produces no signed output.

Fixture mode accepts only RFC 8032 public keys 1–3, and operator mode rejects those known fixtures even if their mode byte is changed. This small exclusion set is defense in depth, **not proof that another key is privately held**. The separate fixture binary has deterministic public-only entropy and fault-injection hooks; neither its entropy source nor those hooks are linked into `tcs-operator`.

The review digest is lowercase hex of BLAKE2b-256 over exactly 176 bytes: a 16-byte header (ASCII `TCS-REVIEW`, byte 1, four zeroes, mode), the 32-byte public key, then the exact 128-byte signed administration message. The resulting 192-byte request uses the existing [signed protocol](ADMIN.md). No C structure padding or text normalization is signed.

## Evidence and limits

`make operator-test` runs sanitized context-codec tests and twelve subprocess test groups using only RFC fixture material. It covers 256 single-bit header corruptions, exact lengths/modes, zero error outputs, all three known fixture exclusions, actual signature verification through the existing admission core, an independent Python review-digest/wire check, all operations and integer boundaries, command/context changes after review, private/public mismatch, no-private-read review, restrictive modes, symlinks/hard links/FIFO rejection, actual extended ACL rejection, repository exclusion, entropy failure, four concurrent signers with one exclusive winner, partial writes, and injected synchronization failure. Launcher tests additionally check exact emulator arguments, private snapshots, no extra inherited descriptor, four concurrent launchers with one winner, failed execution, existing image preservation, and permanent failed reservations. Fault injection is not a real power-loss test. Tests invoke the operator binary only for missing-acknowledgement and fixture-refusal cases; they never run its real key-generation path.

`make test` includes these groups, signed-input/terminal adapters, the launch-context administrator adapter, exact interactive graph mutations, profile/build separation, and transcript corruption tests. All six existing images retain their prior authority graphs and behavior.

`make interactive-smoke` builds both profiles and exercises the actual one-shot launcher, host review/signing, twelve signed UART operations, whole-frame failures, replay, cross-launch refusal, live policy/read checks, audit-full authority reductions, and three real UART breaks. `make interactive-smoke-saved` uses the included images with a native fixture compiler, no SDK. The test-only direct QMP launch used for breaks is not an operator CLI feature. Public fixture mode is explicitly labeled and must never be deployed.

The same suite also directly boots twelve deliberately invalid firmware configurations, including missing/malformed/zero context, wrong mode/fixture key, legacy header, DMA, and a mode-flipped fixture key presented to the operator image. Each must keep administrator NOT_READY through repeated submissions, retain live restricted status, and deny reads. Native tests independently reject malformed bootstrap IPC and protect existing replay state from reinitialization. Terminal reply tests distinguish every defined admission status from uncertain or malformed completion and discard input loss during blocked echo. These are failure-path tests, not successful real-credential provisioning or recovery evidence.

Before live operator use, review independent authorization/provisioning, credential protection, trusted host context selection, and failure/restart procedures. Disable snapshot restoration and verifier-only restart until replay state has a real contract. Receipts remain volatile and unsigned to the host, so automated status/sequence recovery is not implemented. Synchronous policy/audit calls can still block. The one-shot reservation prevents ordinary accidental session reuse, not a privileged host deliberately rolling it back.

Dependency review: upstream 4.0.3 fixes the earlier signing timing leak, while comparison-result timing remains a documented caveat on some platforms. This tool compares only public fields with ordinary comparisons. Its generated machine code has not received a side-channel audit. See the primary [Ed25519 API](https://monocypher.org/manual/ed25519), [BLAKE2b API](https://monocypher.org/manual/blake2b), [known bugs](https://monocypher.org/bugs), and [changelog](https://monocypher.org/changelog). Host entropy and descriptor-relative file behavior follow the [Linux getentropy](https://man7.org/linux/man-pages/man3/getentropy.3.html) and [open](https://man7.org/linux/man-pages/man2/open.2.html) contracts, with native macOS ACL handling.
