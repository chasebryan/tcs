# Signed administration foundation — not a live login endpoint

Status: a real Ed25519 request-verification core, executed in the [boot-context guest](BOOT-CONTEXT.md) and a separate [test-only administrator/policy IPC profile](ADMIN-IPC.md). It is **not linked into ordinary seed/terminal/isolation images**. An [experimental host workflow](OPERATOR.md) now supports explicit local identity creation, public launch-context preparation, command review, and signing. It is not connected to a guest launcher or provisioning endpoint. No operator login, guest boot-entropy service, or interactive mutation command is implemented. The login-method choice remains open; signed commands are the proposed host-held-key path, not a claim that the user has selected it.

This foundation separates three decisions: whether an operator signed the exact command, whether it is fresh in this administration instance, and whether policy can apply it to the intended state. A C command structure is not an unforgeable capability or proof of authentication. The test IPC graph enforces the administrator's private policy channel; any future deployment graph must preserve that boundary.

## Canonical signed packet

Packets are exactly **192 bytes**. Pure Ed25519 signs the first 128 bytes; the final 64 are the signature. There is no JSON normalization, struct padding, endian-dependent cast, detached user-selected key, or unsigned operation field. The fixed domain prefix is ten ASCII bytes `TCS-ADMIN-`, byte `01`, then five zero bytes. Version changes require a new prefix/contract.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 16 | Fixed protocol/domain prefix |
| 16 | 32 | Trusted system/realm identity |
| 48 | 32 | Fresh trusted boot/administration-incarnation identity |
| 80 | 8 | Sequence, unsigned little-endian, starting at 1 |
| 88 | 8 | Operation, unsigned little-endian |
| 96 | 8 | Subject, unsigned little-endian |
| 104 | 8 | Object, unsigned little-endian |
| 112 | 8 | Rights, unsigned little-endian |
| 120 | 8 | Expected current policy generation, unsigned little-endian |
| 128 | 64 | Ed25519 signature over bytes 0–127 |

The first core has one configured operator public key, authorized for the seed's four subjects. GRANT requires object 42 and READ=1. REVOKE, QUARANTINE, and RESTORE require zero object/rights. CHECK is not an administration operation. Subject, operation, rights, expected generation, context, and sequence are all signed. A correctly signed malformed command is still rejected. Key selection and actor identity never come from the packet.

## Admission, execution, and receipts

`tcs_admin_init` operates once on a zero-initialized private instance. Reinitializing an active instance is rejected; there is no reset/rekey message. The supplied public key must already be correctly generated and independently authorized. The API's nonzero checks do **not** validate arbitrary curve points, establish key ownership, generate entropy, or prove identity uniqueness. There is no arbitrary-key import endpoint.

`tcs_admin_admit` accepts only the exact next sequence, matching context, permitted command shape, and valid signature. Failure clears the output command and does not consume a sequence. Success consumes it **before forwarding** and records one pending request. Admission does not mutate policy or mean execution succeeded. While a request is pending, all further admissions fail busy. Completion accepts only the pending sequence, after the owner has validated a real policy receipt. A terminal acknowledgement, timeout, or lost response must not clear it. Sequence exhaustion never wraps; accepting `UINT64_MAX` permanently exhausts the instance after that pending operation finishes.

The owner must provide an immutable private packet snapshot, not concurrently writable shared memory. State and buffers must not alias. This is a single-threaded ownership contract, not a lock-free/multi-threaded implementation.

`tcs_policy_admin_compare_apply` runs **inside the policy owner** in the test IPC profile, against a current candidate, with no intervening operation between generation comparison and candidate mutation. It requires the trusted admin actor and exact current subject generation. A stale command makes no candidate change. The existing audit/commit step then applies: audit failure blocks grants/restores, while an authorized revoke/quarantine still reduces authority. An admitted stale or audit-failed request has still consumed its administration sequence. A snapshot checked earlier in an admin server cannot replace this policy-side precondition. The [conditional adapter and receipts](ADMIN-IPC.md) are exercised in the separate test profile, not deployment provisioning.

## Freshness is an unresolved deployment requirement

The caller supplies the boot identity. The core checks equality but cannot prove that the value is fresh. **Resetting volatile administration state and reusing a boot identity re-enables previously valid signatures.** An executable test demonstrates this limitation, rather than claiming restart-safe replay resistance. Every new verifier incarnation must obtain a never-reused trusted identity, or preserve a durable non-rollbackable replay state. A static value embedded in a reusable boot image is not acceptable. Nonzero bytes are not evidence of randomness.

Before enabling operator administration, finish independently authorized public-key provisioning, authenticated-context discovery for the host signer, and failure/restart handling. Preserve the tested fresh-launch, strict private-IPC, and definitive-receipt contracts. Public RFC keys and deterministic test boot identities must never authorize deployment. Any credential creation/import requires an explicit operator action; no real credential was created in this increment.

## Dependency and scope

Verification uses unmodified [Monocypher 4.0.3](https://monocypher.org/download/) with its optional Ed25519/SHA-512 module. The upstream archive's published SHA-512 was matched; local tests compare the vendored files byte-for-byte with that pinned archive. Original notices and the complete source distribution are included. This is provenance evidence, not an independent cryptographic audit.

Upstream reports that 4.0.3 fixes the earlier EdDSA/Ed25519 signing timing issue, while a comparison-result timing issue remains on some compiler/platform combinations. This core compares only public protocol fields and verifies public signatures; it does not use comparison results as secrets. No blanket constant-time claim or approval for secret-buffer comparison is made. The experimental host signer also compares only public identities; all executed signing tests use public fixtures. No real credential has been created/imported and no generated-code side-channel audit is claimed. See [upstream known bugs](https://monocypher.org/bugs), [changelog](https://monocypher.org/changelog), and [host limitations](OPERATOR.md).

Authentication does not encrypt the serial channel, authenticate displayed replies, prevent a compromised transport from dropping requests, or prove that the signing host shows the true command. Availability budgets, multi-operator roles, key rotation/revocation, durable receipts, and secure update integration remain open. Ordinary images expose no new authority as a result of this library. The separate boot test adds a firmware-page owner but has no policy service or administration channel.

## Evidence

`make test` uses address/undefined-behavior sanitizers and checks [RFC 8032 section 7.1](https://www.rfc-editor.org/rfc/rfc8032.html#section-7.1) vectors 1–3, exact encoding and lengths, unaligned buffers, all 1,536 one-bit packet mutations, signed malformed commands, key/realm/boot mismatch, replay, busy/incorrect completion, reset rejection, sequence exhaustion, and policy generation/audit behavior. It also checks state changed **after admission but before execution**, and documents the reused-boot-identity limitation. All test key material is public algorithm data, not a deployed identity.

`make admin-cross-check` compiles both original administration files and both upstream cryptographic source files for the target. `make boot-test-smoke` links the verifier without a policy endpoint; `make admin-test-smoke` additionally exercises real administrator/policy/audit IPC with public fixtures. Neither is an operator login. Existing normal QEMU profiles are regression-tested separately; passing them is not evidence of guest authentication.
