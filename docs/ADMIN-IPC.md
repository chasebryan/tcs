# Signed administration IPC and execution receipts

Status: implemented and exercised in a **separate test-only release image**. This is not operator provisioning, an interactive administration UI, or production authorization. It uses public RFC fixtures and the explicitly test-only boot context. Ordinary seed/terminal/isolation/boot-probe images retain their prior roles and graphs.

## Server boundary

The eight-domain test profile adds an administrator at priority 25 and a test boot-data provider at 70 to the terminal/client/storage/policy/audit/serial arrangement. The scripted test terminal can submit signed packets to administrator channel 0, but has **no direct policy-admin channel**. Administrator calls policy channel 0; ordinary storage reaches policy channel 1. The provider alone maps firmware configuration and returns context to the administrator and public signed fixtures to the test client on different channels. Serial owns only its UART. The exact graph checker rejects additional authority, changed images, and modified attributes.

`servers/admin.c` initializes once from a private boot-provider call and refuses requests if initialization failed. It currently accepts only the test boot format. Each exact 192-byte packet is copied from message registers into private storage before signature verification or nested calls. Key and actor are not input fields. Valid admission consumes its sequence before forwarding; one pending request is allowed. Signature success does not imply policy execution.

`servers/admin_policy.c` replaces the legacy policy adapter only in this profile. Channel 0 accepts the new exact conditional-mutation contract, **not legacy four-word grant/revoke calls**. It independently requires the next sequence and never wraps. It consumes a valid execution sequence even when generation is stale or audit denies the mutation. Wrong sequence/shape does not execute or reset anything. Channel 1 retains ordinary checks and fixed-subject status, never administration authority.

## Experimental wire contract

These labels are version-2 test-integration contracts, separate from the version-1 signed-message domain. Message words are 64-bit; byte packets are packed little-endian explicitly, never by serializing C structure padding.

| Label | Exact words | Meaning |
| --- | --- | --- |
| `0x201` | 24 | Client → administrator: signed packet |
| `0x202` | 6 | Administrator → policy: sequence, expected generation, operation, subject, object, rights |
| `0x280` | 13 | Policy receipt, forwarded by administrator only after validation |
| `0x281` | 1 | Admission-layer rejection/status, not an execution receipt |

Receipt words 0–5 echo the complete conditional request. Word 6 is the policy decision **before audit commit**, word 7 is audit success (0/1), and word 8 is applied (0/1). Words 9–12 are the post-state, generation, object, and rights. The request generation used by ordinary reads is implicitly zero in administration; its separate signed expected generation is the precondition.

Policy compares generation and computes its candidate locally, then calls audit with the existing seven-field decision record. Request/decision data survives nested message-register overwrites. Audit acknowledgement must have the correct reply shape, success status, and a count in 1–64. Failure blocks grant and restore, but cannot block an authorized revoke/quarantine. The resulting receipt explicitly distinguishes those outcomes; the older generic `AUDIT_FULL` result alone could not.

Administrator requires an exact receipt shape, echoed command identity, legal flags, a valid policy snapshot, and decision/effect/generation/state consistency. A stale receipt must show a different generation; an applied grant must show active authority at the next generation; reductions and restores must show their allowed post-state. Sequence/generation counters never wrap. Only a validated receipt clears pending. A malformed or mismatched reply leaves the request pending even if policy already committed. No terminal acknowledgement, completion RPC, or automatic retry can clear it. This fail-closed state is not fault recovery.

## Evidence

`make admin-ipc-test` runs the real administrator and conditional-policy adapters under sanitizers with a mocked kernel/audit transport. It covers exact channel/length/label checks, boot failure, rejection of legacy administration, nested register clobbering, policy changes after authentication, stale decisions, replay, malformed audit acknowledgements, audit-failed reductions versus blocked grants/restores, and nonwrapping sequences/generations. Corrupting each of the 13 receipt words or its shape leaves the administrator pending after an actual model commit. Further requests fail busy; a duplicate direct policy execution is rejected.

`make admin-test-smoke` builds and boots the eight-domain release image with a fresh host nonce and public signed fixtures. Twelve real IPC commands exercise grant, stale generation, revoke, regrant, quarantine, isolated grant denial, restore, and audit exhaustion. The test client compares every receipt with a fresh status query through client → storage → policy, and checks actual reads and stale sessions. After audit fills, signed revoke/quarantine still apply, while grant/restore do not. Bad signatures, malformed submissions, a replay, and ordinary-client escalation are rejected. It ends quarantined at generation 8; live UART `status`, denied reads, and rejected plain-text administrative commands preserve that state.

`make admin-test-smoke-saved` runs the saved image, requiring a native C compiler for the public-fixture helper but no SDK download. Python tests mutate every topology attribute, add unexpected child elements, run optimized-mode rejection, and corrupt/fragment the expected transcript. Saved evidence is in `artifacts/admin-test.img`, `admin-test-report.txt`, and `admin-ipc-boot.log`.

## Limits and next gate

The native corrupt-receipt/failure tests are not real crashed-service recovery tests. Synchronous calls can still block if policy/audit hangs. Kernel channel ownership authenticates the policy peer within the trusted graph; receipts are not cryptographically signed for a remote host, encrypted, durable, or proof against a compromised policy server. The existing volatile audit record lacks the new request sequence/expected-generation fields and does not substitute for this execution receipt.

Test fixtures must never become deployment trust roots. A real operator-selected key/realm, trusted context discovery, explicit host signing workflow, interactive transport, and a restart/incarnation contract remain necessary. Snapshot restoration or verifier-only restart is unsupported; the [boot-context limitations](BOOT-CONTEXT.md) still apply. No real credential has been generated or imported. The terminal here remains read-only after the scripted test, even though its test wrapper has a signed-submission endpoint.
