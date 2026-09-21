# TCS implementation roadmap

Milestones are acceptance gates, not calendar promises.

| Milestone | Deliverable | Exit evidence |
| --- | --- | --- |
| 0.1 Seed — implemented | Five native servers, fixed graph, policy states, session revocation, bounded audit | Host invariant tests and a real QEMU IPC scenario pass |
| 0.2 Interactive administration | Isolated serial driver, ordinary terminal client, separate administration service, read-only status commands | User input cannot impersonate the admin channel; driver faults do not expose policy memory |
| 0.3 Lifecycle control | Independent supervisor, suspend/restart, server incarnations, capability/mapping ownership | Fault injection; old sessions invalid after restart; resources reclaimed only after quiescence |
| 0.4 Dynamic containment | Event-driven reductions, asynchronous control queues, deadlines, CPU/memory/IPC budgets | Compromised or hung workers cannot block containment; malformed telemetry cannot create privilege |
| 0.5 Persistent system | Storage driver/server, durable audit, versioned state, boot identity | Power-loss and rollback tests; no reuse of stale sessions after reboot; recovery paths exercised |
| 0.6 Controlled deployment | Signed image manifests, trust-root management, atomic deployment and recovery | Corrupt/unauthorized images rejected; interrupted updates recover; keys separated from build hosts |
| 0.7 Applications and hardware | One named physical board; optional network and analysis/radio domains | Device/DMA isolation measured; workflows operate with explicit rights; physical recovery tested |

## Decisions to resolve during lifecycle work

The [experimental lifecycle model](LIFECYCLE.md) begins this investigation with two one-use worker slots and separately correlated stop/drain evidence. It is not linked into a guest and does not satisfy milestone 0.3's runtime acceptance gate.

Microkit's fixed topology is suitable for Seed. Prototype a bounded pool of pre-created worker domains and separately evaluate a direct seL4 resource manager for arbitrary process creation. Select between them using memory overhead, recovery latency, revocation completeness, and proof burden. Do not pretend a thin portability interface makes their security semantics interchangeable.

## Userspace and interface direction

Native applications communicate through versioned service contracts. An optional POSIX compatibility server can be evaluated after basic storage and process lifecycle exist. It will not define the trusted core. Start with an interactive serial terminal; later add a display/input server and compositor as isolated services. A status view should show active sessions, held rights, quarantine reasons, and unmet recovery conditions.

## Package and update direction

Use complete system-image deployments for the fixed graph. Future service packages must bind code identity, protocol version, resource budget, requested capabilities, and state migration rules. Installing a package and activating its requested authority are separate transitions. Test rollback of both code and state; a signature by itself does not prevent downgrade or unsafe migrations.

## Verification direction

Expand the bounded policy into an explicit model with refinement tests against the implementation. Add concurrency schedules, RPC-failure injection, parser fuzzing, and negative resource-access tests. Scope any seL4 verification claim to the actual kernel configuration. Evaluate F*/Low* extraction for the small authority core only after its protocol and failure semantics stabilize.

## Reuse gate

Existing repositories are candidate workloads and design references. Before importing code, pin a revision, inspect the actual implementation, resolve licensing and dependency obligations, test the relevant property, and document what authority the component receives. Existing evidence scores and fixture signing keys never authorize a TCS release.
