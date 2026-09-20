# TCS architecture decision 0001

Status: accepted for Seed, 2026-09-20. The user renamed CINIX to TCS (Typed Capability System), with repository https://github.com/chasebryan/tcs. The architecture remains a new operating system using substrates, communicating microkernel servers, and dynamic security. This rename changes source identifiers and presentation without adding a new capability type system or changing the wire protocol.

## Substrates and ownership

TCS owns its service protocols, authority rules, lifecycle behavior, user environment, and eventual update model. The initial execution substrate is seL4 with Microkit 2.3.0. Microkit provides a fixed initial arrangement of protection domains; TCS implements changing access decisions within it. Arbitrary server creation and topology replacement require a future resource/lifecycle manager. This distinction follows the upstream [Microkit component model](https://docs.sel4.systems/projects/microkit/).

One microkernel arbitrates the machine. The cooperating servers run in userspace. Using several servers does not mean running one kernel per server.

The future architecture has three layers of responsibility:

1. Execution substrate: address spaces, kernel capabilities, IPC, scheduling, interrupts.
2. TCS authority and lifecycle substrate: bounded policy decisions, session management, resource ownership, containment, restart, and evidence collection.
3. Service substrates: device, storage, network, terminal, and application services behind typed contracts.

These are architectural boundaries. Separate interchangeable substrate implementations do not yet exist.

## Seed domains

| Domain | Priority | Authority and responsibility |
| --- | --- | --- |
| console | 10 | Trusted boot test driver; admin calls to policy; calls client; reads audit records |
| client | 20 | Requests fixture reads; cannot administer policy or directly append audit records |
| storage | 30 | Owns the read-only fixture and asks policy about subject 1 derived from its client channel |
| policy | 40 | Owns a four-subject authorization table; evaluates and commits state transitions |
| audit | 50 | Accepts policy decisions and retains 64 records until reset |

The console is privileged test machinery, not an authenticated human session. Replace it with a separate authenticated administration domain before adding an ordinary shell.

All seed channel ends explicitly disable notification sending; the current handlers do not use it. The system checker rejects additional resources, altered program images, and undeclared communication authority. The separate serial/terminal profile declares its distinct device, IRQ, and notification needs explicitly rather than inheriting the test console's authority; see [terminal architecture](TERMINAL.md).

Self-status uses the existing client → storage → policy path, with zero-word requests and a fixed channel-bound subject. It returns validated current metadata without changing authority or consuming audit capacity. It is observable during quarantine and audit exhaustion, but is not a cached authorization decision; resource reads still recheck policy. Neither profile exposes a payload-selectable status subject.

The synchronous call graph has increasing priorities and no cycles. This fits [Microkit's protected-call constraints](https://docs.sel4.systems/projects/microkit/manual/2.3.1/#protected-procedures). The compiled API and SDK are pinned to 2.3.0; live documentation can describe newer versions. Channels carry kernel-provided caller identities; request payloads never supply an admin identity. Storage is trusted to map its incoming client channel to its fixed subject. Compromised storage could disclose its own fixture, so the policy service alone is not a proof that a compromised resource owner enforces access.

## Dynamic security contract

Seed implements three states per subject:

- Restricted: no resource rights; explicit administrative grant can activate a session.
- Active: exact object and read permission with a current, nonzero generation.
- Quarantined: grants and reads are rejected; administrative restore returns only to Restricted.

An administrative revoke removes rights and invalidates the session. Each grant issues a new generation, so a later grant cannot resurrect an older session. Counters never wrap: exhaustion permanently prevents new grants for that subject in the current boot.

A generation is a lookup/version value bound to the caller's subject, not a cryptographic token or kernel capability. An authorized caller may repeat reads while its generation is current. This is not request-level anti-replay protection. Seed has no delegation, time-based leases, or persistent sessions across reboots.

Future detectors may request reductions through narrowly scoped endpoints. They will not grant authority. Restoration requires an explicit administrative decision plus checks that the replacement server and its state meet the selected policy. An anomaly score must not become an unbounded administrative capability.

## Ordering, evidence, and failure

The policy server computes a candidate transition and submits its decision to audit. It commits a grant only after the audit server acknowledges the record. Reads also require a successful audit acknowledgement. Revoke and quarantine take effect even if audit is full; the response then reports `AUDIT_FULL` and the debug console records the missing audit.

Records describe authorization decisions before commit. They do not prove operation completion, persistence, a trusted clock, or that a compromised logger cannot alter them. There is no cryptographic hash chain or durable append-only store in Seed.

The supported revocation guarantee is that a subsequently checked request using invalidated authority is denied. Earlier authorized work and data already delivered cannot be recalled. Shared-memory mappings and DMA must eventually be revoked separately, with quiescence acknowledgements before memory reuse. Do not promote this seed's serial call scenario into a concurrency guarantee.

If policy or audit stops responding, dependent synchronous requests block. Bounded handlers and scheduling budgets are present, but availability under malicious or crashed servers is not demonstrated. An independent supervisor and asynchronous deadline protocol are required before health-driven automatic isolation can be claimed.

## Trust and verification boundary

The current access property trusts seL4, Microkit's loader/initializer/monitor/runtime, the system description, policy, storage, audit, and the privileged test console. Build tools and the host that supplies the image are also trusted. The SDK configuration used here is `debug`, with verification build flags disabled. Upstream kernel proofs do not establish whole-TCS correctness or automatically cover this exact configuration.

No secure/measured boot chain, device/DMA isolation, remote attestation, encrypted filesystem, or signed runtime policy is implemented. These omissions describe the seed's actual boundary, not implied guarantees.

## Language and reuse decisions

Seed uses a small C11 core and C Microkit adapters because this keeps the first ABI and dependency set small. It is freestanding, bounded, and allocation-free. Rust is the intended default for growing userspace services; language bindings and unsafe interfaces need their own review. F*/Low* remains a candidate for specifying and extracting narrow policy logic after the behavior stabilizes.

Reuse concepts from SEAL's authority decisions, Wuci-Ji/Daylight's claim discipline, and Latticra's boundaries. Kaiju and radio tools belong in later isolated application domains. No code from those repositories has been copied into this seed. Their APIs, licenses, tests, and actual implementations still need a source-level reuse review; the earlier corpus pass was a partial public-document inspection, not a complete audit. Experimental cryptography, fixture trust roots, and project scores have no authority in TCS.
