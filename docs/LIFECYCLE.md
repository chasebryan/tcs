# Experimental worker-lifecycle model

Status: native model and freestanding cross-compilation only. No guest links this
code. It does not stop a server, revoke a kernel capability, reclaim memory, or
complete roadmap milestone 0.3. Existing boot images and signed administrator
state are unchanged.

The model in `lib/lifecycle.c` explores a deliberately small replacement scheme:
two distinct pre-created worker domains, used once each, at most one selected
worker at a time, and one outstanding broker request. It retains retired
resources until the enclosing system is shut down. It is not arbitrary process
creation or in-place restart.

## Substrate evidence

The pinned Microkit 2.3.0 [`microkit_pd_restart` implementation](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/libmicrokit/include/microkit.h)
writes the program counter and requests resumption. Its name does not imply a
fresh address space, cleared memory, replaced capabilities, or reset-safe replay
state. The bundled hierarchy example demonstrates the narrower PC-reset behavior.

The pinned [manual](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/docs/manual.md)
says protected calls block until the callee returns; this version trusts the
callee to complete in bounded time. Notifications do not block but may coalesce.
Initialization is not globally synchronized, and Microkit does not supply a
timer/sleep API. The [runtime](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/libmicrokit/src/main.c)
dispatches entry points sequentially and manages a reply capability. The source
archives for these exact revisions are included under `third_party/sources`.

These observations motivate an independent supervisor and explicit control
protocol; they do not establish a correct runtime adapter. Existing isolation
tests demonstrate six specific faults, not fresh-process replacement.

## State and transition contract

Start with a zero-initialized model **once**, only after a trusted bootstrap has
established that both distinct workers are stopped, unused, and have no pending
effects. There is no reset API. Structural validation detects inconsistent
fields, not tampering, authenticity, historical provenance, or rollback.
One trusted owner serializes every model call. The C structure is not atomic
and must not be concurrently accessed; explored event orders are serialized
model schedules, not a shared-memory concurrency guarantee.

| Event | Trusted actor | Required state | Effect |
| --- | --- | --- | --- |
| RESERVE | Administrator | Unused slot; every other slot unused or retired; successful audit | Consume a new nonzero incarnation; starting, gate closed |
| STARTED | Supervisor adapter | Starting, matching incarnation | Ready, gate still closed |
| ACTIVATE | Administrator | Ready, matching incarnation; successful audit | Active work gate; no resource rights granted |
| BEGIN | Broker | Active, no pending request | Issue next nonzero request sequence; remember pending |
| COMPLETE | Broker | Active, exact pending sequence and incarnation | Accept completion in model; clear pending |
| CONTAIN | Administrator, supervisor, or reduction-only detector | Starting, ready, or active | Close gate immediately, retain pending work regardless of audit failure |
| STOPPED | Supervisor adapter | Closing, exact incarnation and issued sequence | Record stop confirmation |
| DRAINED | Broker | Closing, exact incarnation and issued sequence | Record no remaining old effects; discard pending result |

Both STOPPED and DRAINED are required to enter **retired**, in either order.
Neither alone permits selecting another slot. A missing or failed confirmation
is not represented as success: the slot remains closing. Duplicate containment
does not erase an existing confirmation. Retired slots never become unused.
When both slots are consumed, RESERVE refuses with BAD_STATE. Nonwrapping
integer exhaustion returns EXHAUSTED; containment remains possible.

The event fields are operation, slot, incarnation, and sequence. They are native
model values, not a serialized IPC ABI. RESERVE uses incarnation zero; all later
events require the exact assigned incarnation. COMPLETE uses a nonzero pending
sequence. STOPPED and DRAINED use the last issued sequence at containment, which
may be zero. All other events require sequence zero. Correlation is sufficient
only for this one-start/one-containment, one-use-slot model; an eventual reusable
slot protocol needs a separately designed operation epoch.

Actor identity and audit evidence are separate trusted inputs, never claims
accepted from the event payload. Unknown and worker identities cannot apply any
event. A broker must derive the worker slot from its authenticated transport
endpoint and bind work to the supervisor-issued incarnation; forwarding a
worker's chosen slot/incarnation with the broker role would defeat this model.
Counters are public correlation values, not unforgeable bearer capabilities.
A detector can only request containment. All unsuccessful transitions
return value zero and leave state unchanged. Successful BEGIN/COMPLETE return
the request sequence; other successes return the incarnation. OK describes a
model transition, never proof of a completed physical action.

`tcs_lc_allows` is only an additional gate. Existing subject/object/rights policy
checks remain mandatory. Activation does not inherit the retired worker's
grants. A broker must check incarnation, request sequence, current gate, and
ordinary policy before exposing a result; a result completed after containment
cannot be accepted through COMPLETE. Data already disclosed cannot be recalled.

The model takes audit success as an input for RESERVE/ACTIVATE and ignores it
for reductions and confirmations. It implements no logger. A future adapter
must not synchronously block containment while trying to obtain audit evidence.
BEGIN does not itself authorize an I/O effect or stand in for its policy/audit
check. The existing signed-administration replay/pending state is not modified.

## Evidence

`make lifecycle-test` runs sanitized native boundary tests plus an independently
written Python reference model compared to the sanitized C implementation.
Breadth-first exploration visits 185 states for two slots and at most two issued
requests per slot, comparing 79,232 generated events, including invalid actors,
fields, replayed receipts, audit failures, and boundary values. Outgoing events
that would exceed the exploration bound are compared but not expanded further.
This is bounded differential testing, not a formal refinement proof, exhaustive
64-bit state exploration, concurrent runtime test, or measured availability.

Separate tests cover synthetic near-maximum incarnation/request counters,
inconsistent state, delayed startup after containment, pending work, both
confirmation orders, duplicate receipts, permanent pool exhaustion, and refusal
to replace a worker when stop confirmation is absent. The synthetic counter
states are structurally valid but not all reachable from two fresh slots.
`make lifecycle-cross-check` compiles an AArch64 object without linking a guest.
Build-plan tests exclude lifecycle objects/sources from all eight guest targets.

## Required runtime gates

Before attaching this model to kernel effects, implement and test:

- A separate test-only supervisor profile with its own scheduling context,
  bounded handlers, exact authority-graph checks, and no blocking worker RPC.
- A trusted startup adapter. STARTED means the selected worker reached the
  chosen ready condition; mere return from a resume syscall is insufficient.
  Unexpected execution/fault of an unused worker violates the bootstrap
  precondition and must disable selection, not be ignored as an invalid event.
- Ordered start/stop effects. STOPPED must confirm the worker is stopped **and**
  earlier queued startup/resume operations cannot execute afterward. A delayed
  STARTED message is rejected by this model but that alone cannot undo a late
  physical resume. Stop failure must never generate STOPPED.
- Broker-owned quiescence. DRAINED certifies all pre-containment effects are
  settled and no new old-incarnation work can enter; it is not the worker's
  assertion of health. Model-state closure must actually gate every resource
  path. Pending side effects may have happened: do not retry uncertain work.
- Mailbox ownership, acquire/release ordering, bounded draining and hostile
  producer tests. Notifications are wakeups, not durable operation counts;
  copy and validate hostile data before use and do not reread mutable fields.
- Trusted deadlines and CPU budgets, actual hung-worker/fault injection, and
  observation that the supervisor remains responsive. A model with no clock
  cannot demonstrate timely containment.

This first pool intentionally does not reclaim old resources. Reuse requires
separate kernel-capability, mapping, IPC, resource-owner and device/DMA
quiescence evidence. Physical hardware, supervisor recovery, administrator or
policy restart, boot-persistent freshness and dynamic process creation remain
out of scope. A new boot or restored snapshot must never silently reuse this
volatile model as fresh authority.
