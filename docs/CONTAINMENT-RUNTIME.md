# Test-only independent control with a hung broker

This separate release-kernel image tests **worker containment while its broker
is stuck**, not containment/recovery of the broker itself. It integrates the
[reduction latch](REDUCTION.md) with a supervisor-owned gate and actual child TCB
stop operations. The nine pre-existing images and their authority graphs remain
unchanged. Approval and audit are explicit fixtures, never operator credentials.

Run `make containment-smoke` to build/test, or `make containment-smoke-saved` to
run the included image without an SDK. The harness uses one Cortex-A53 CPU, 2 GiB
RAM, TCG, no disk/network or control socket, and terminates its own QEMU process.

## Independent paths

| Domain | Priority; budget/period (microseconds) | Authority |
| --- | --- | --- |
| observer | 10; 10000/10000 | UART, fixture supervisor control/status, supervisor/caller notifications, request-page writes; diagnostic pages read-only |
| caller | 30; 1000/10000 | Broker protected call, own phase page; intentionally becomes blocked |
| broker | 40; 1000/10000 | Worker channels, supervisor bridge, own progress page; intentionally spins without replying |
| supervisor | 50; 10000/10000 | Private latch/lifecycle state, two child TCBs, read-only request page, writable notification-stop receipt; no outgoing protected call |
| worker_a / worker_b | 20 / 19; 1000/10000 each | Own progress page, own kernel-bound broker channel; B remains unused/stopped |
| serial | 60; 2000/10000 | Sole PL011 page and IRQ, existing bounded serial implementation |

Observer has **no broker call channel**. Caller does not hold the reduction page
or supervisor endpoint. Broker and workers cannot write the request or receipt
page. Supervisor has no dependency on broker, observer, UART, audit or a worker
RPC when processing the reduction notification. The exact XML allowlist includes
every mapping, caller identity, notification direction, budget, parent and image;
507 element/attribute mutations are rejected, including under Python `-O`.

Higher-priority full-budget supervisor initialization stops both children before
yielding on the single tested CPU. Workers and broker have fractional budgets,
so their deliberate loops leave CPU time for the observer. This is a tested
scheduling arrangement, not a worst-case latency or notification-flood proof.
Kernel, supervisor, observer and the test configuration remain trusted.

## Scenario and evidence

The observer's UART accepts `status` and `next`, with bounded echo, cancellation
and whole-line loss handling. Five fixed fixture steps perform:

1. Reserve A; its broker-bound startup handshake makes it ready.
2. Activate A; notify caller, which asks broker to wake A. A obtains one ticket
   and spins forever updating its counter, without cooperating with stop.
3. Notify caller again. It records phase 2 before calling broker HANG. After
   checking the live ticket, broker spins updating its counter and never replies.
   Any return changes caller phase to 3 and fails the host's check.
4. Observer publishes bit 0 with release atomic OR and notifies supervisor.
   Supervisor acquire-loads the word, latches closure, stops A, and records the
   matching STOPPED event only after the kernel operation returns.
5. Attempt reuse/reactivation of A and selection of B. All are refused: A is
   inhibited; A's pending ticket and missing drain prevent selecting B.

Before reduction, the host requires both worker and broker counters to advance
while caller remains in phase 2. After reduction, five samples must keep A's
counter identical while broker continues to advance. Pending remains 1,
STOPPED is 1, DRAINED is 0, B remains unused with a zero counter, and caller still
has not returned. Three final samples check that refusal probes do not undo this.
Neither worker nor broker supplies an unverified success verdict.

Counter/phase pages are diagnostic observations with graph-enforced single
writers and read-only observer mappings. They are not cryptographic attestations
or proofs over arbitrary compromised implementations/all future schedules.
The broker/caller source implements the injected infinite call path explicitly.

## No status-poll rescue

Supervisor polls its request page before **every** incoming protected call and
on the authorized notification. Polling closes the same gate even before an
invalid command is rejected. Arguments are captured before kernel operations
can clobber message registers. All resume effects are synchronous in the same
owner; there is no deferred-resume queue that can undo a later stop.

For evidence specific to the asynchronous path, supervisor writes a separate
receipt bit **only** when a notification callback actually performs a stop and
records STOPPED. Bootstrap and ordinary protected-call polling cannot set it.
Repeating a notification after an earlier polling stop cannot fabricate it.

After publication, observer makes no supervisor call until that receipt is 1.
It reports WAIT while zero; invalid receipt bits fail immediately. Thus periodic
host `status` requests cannot quietly rescue a dropped notification by causing
the supervisor to poll. A native withheld-notification test checks this exact
path and its absence of RPCs. This receipt is a fixture-only diagnostic, not a
general acknowledgement protocol, freshness scheme or authorization token.

## Failures and retained uncertainty

Kernel stop/resume helpers do not return normally on error. Native injection
checks that a nonreturning stop failure leaves the gate closed, work pending,
STOPPED unset and receipt zero. Unexpected child faults fail the supervisor and
request stops for both children; no fault reply resumes a child. A failed trusted
supervisor has no implemented recovery or independent parent-containment scheme.

Malformed post-commit broker replies fail the broker closed without retrying or
clearing model pending state. Native tests can still deliver the independent
reduction. Worker identity is the broker's kernel channel, not a payload slot.
No RPC in this fixture accepts remote STOPPED or DRAINED claims. There is no drain
implementation, resource I/O, data disclosure, DMA, memory reuse or replacement.
Stopping A does not establish that a compromised broker has no residual effects.

The mailbox uses the tested aligned normal-cacheable pages and acquire/release
operations on one AArch64 CPU. No mutable multiword request exists. Private
stickiness prevents an observed request from being cleared. Publication still
does not mean observation; post-sample writes may race an earlier transition,
and unobserved withdrawn requests are not durable history. This boot evidence
does not establish another compiler/architecture, SMP or noncoherent-device
memory model. See the [underlying contract](REDUCTION.md).

## Limits and next gates

Native address/undefined-behavior tests exercise actual supervisor, broker,
caller, worker and observer adapters with mocked kernel/serial operations and
a test-only nonreturning-loop trap. They cover caller roles, malformed lengths,
register clobbering, stop/resume failure, early inhibition, delayed startup,
malformed replies, repeated notifications, withheld notifications, fake receipts,
cancellation/loss and partial echo. The real infinite loops are tested in QEMU,
not inferred from those native traps. Host parser tests reject contradictory
state and allow retries only for explicitly identified startup states.

The observer still uses the serial server to receive commands and report
results. A hung serial driver can block it **before publication**; this is not
serial-fault-resilient management. Trusted timers, autonomous detection,
deadline enforcement, bounded notification flooding, supervisor recovery,
authenticated lifecycle control/audit, real resource-owner drain/reclamation,
persistent incarnation state and physical hardware remain open. Broker keeps
running in this experiment; no claim of recovering or containing it is made.
Milestones 0.3 and 0.4 remain incomplete.

Saved evidence: `artifacts/containment-test.img`, `containment-boot.log`,
`containment-report.txt` and `containment-runtime-tests.log`. These are
builder-produced records, not signed releases or independent attestations.
