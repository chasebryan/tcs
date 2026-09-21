# Test-only runtime supervisor experiment

This is a separate release-kernel image, not production service recovery or a
change to the signed administrator. It connects the [lifecycle model](LIFECYCLE.md)
to actual kernel stop/resume operations and a deliberately narrow broker. No
operator credentials or policy/audit server are present. Administrative approval
and audit success are **test fixtures**, not authenticated decisions.

Run `make lifecycle-smoke` to build and exercise it in diskless, networkless
AArch64 QEMU; `make lifecycle-smoke-saved` uses its included image without an SDK.
The harness controls only this separate image, requires structured observations,
and terminates its own emulator. It does not install anything on hardware.

## Authority and scheduling

| Domain | Priority; budget/period (microseconds) | Authority |
| --- | --- | --- |
| controller | 10; 10000/10000 | Test-only control/status RPCs, serial IPC, read-only worker-counter pages |
| worker_a | 20; 1000/10000 | Broker channel 0, own writable counter page; intentionally loops forever |
| worker_b | 19; 1000/10000 | Broker channel 1, own writable counter page; deliberate unmapped write fault |
| broker | 40; 1000/10000 | Worker notifications and gate-owner RPC; private pending-ticket state |
| supervisor | 50; 10000/10000 | Lifecycle state, exactly two child TCBs, their kernel fault messages; no outgoing RPC |
| serial | 60; 2000/10000 | Sole PL011 page/IRQ; existing bounded serial implementation |

The single-core supervisor has a full scheduling budget and higher priority than
both workers. Its initialization stops both children before returning to its
event loop; it makes no blocking call during that bootstrap. Workers have
fractional CPU budgets, so the spinning fixture does not consume all available
CPU time. These settings and sampled QEMU observations are not a worst-case
latency proof. Supervisor code and scheduling configuration remain trusted.

The exact system graph is checked independently, including every mapping,
notification direction, parent/child relationship, program and scheduling field.
Workers have neither controller authority nor a direct supervisor channel.
The eight pre-existing images keep their original graphs and image bytes.

## Where containment takes effect

The **supervisor owns the gate**, not a delayed copy inside the broker. For each
ticket admission and completion the broker makes a bounded protected call to
that gate owner. Worker slot identity comes from the broker's incoming kernel
channel, never from the worker payload. Incarnation and sequence are checked
against supervisor-owned state and the broker's remembered ticket.

CONTAIN changes that single owner's state to closing. From that linearization
point, later BEGIN and COMPLETE requests are rejected. The supervisor does not
call or wait on the broker, worker, UART or audit server. It then invokes the
child TCB stop operation, and only records STOPPED after the call returns. This
adapter has no queue of deferred start/resume operations; those effects execute
synchronously in its single owner. The SDK's stop/resume helpers do not return
normally on kernel error. Native failure injection checks that nonreturning
errors cannot record a false stop confirmation.

This is a conditional availability result: the supervisor and kernel are
trusted, and no fixed containment deadline is claimed. A hung supervisor can
block broker calls. The test controller also makes blocking broker calls, so
this is not a complete management path resilient to a hung broker or serial
server. No automatic detector or timer-driven deadline protocol is implemented.

## What drain means here

The broker owns exactly one pending ticket for the selected worker. It sends no
disk, network or DMA request, delegates no operation to another resource owner,
and exposes no application data. A worker doing local computation may hold that
ticket until stopped. The broker's DRAIN operation checks the supervisor's
closing state and confirmed stop, discards its own ticket, then submits the
correlated DRAINED event. Unlike the abstract model's either-order allowance,
this adapter requires stop before drain.

The broker cannot accept a late old-incarnation completion or new work after
gate closure. Malformed replies after a potentially committed transition make
it fail closed rather than retry. A native router test can still call the
supervisor directly when the broker has entered that failure state. The current
UART controller itself fails closed if broker status is unavailable; this is
not a surviving user control path. Uncertain state is not cleared by a terminal
command or a restart.

This establishes **ticket quiescence for this fixture**, not general resource
quiescence. Kernel capabilities and mappings are retained, pages are not reused,
and retired workers are never resumed. A production resource owner would need
its own in-flight operation, mapping, device and durable-state protocol.

## Observed scenario

The UART controller accepts only `status` and `next` in this explicitly labeled
fixture. `next` advances a fixed eight-step scenario; it is not an ordinary
terminal command and is not present in existing images. Cancellation, transport
loss and invalid commands do not advance it. Echo is flushed before a control
action, with bounded input/output processing.

The harness first observes worker A progressing through a shared counter. A does
not poll for stop or return from its handler. After containment, five separate
status samples require that counter to remain identical while model/broker
state shows stopped but not yet drained. Only after broker drain can the second,
distinct worker be selected and activated with a new incarnation.

Worker B attempts two stale-incarnation operations and a malformed identity
request through its real broker channel. Broker-owned rejection counts—not a
worker-provided verdict—are checked. It then obtains its own ticket, writes a
known counter value, and deliberately faults on an unmapped address. The parent
checks child identity, message shape, instruction address, fault address, access
type and syndrome before containment. The controller reports the captured words;
the host independently validates them. Neither fault reply nor in-place restart
is used. Unexpected or duplicate faults disable the supervisor and stop both
children.

Both workers end retired. Attempts to select either consumed slot are refused.
Worker A's counter must stay unchanged throughout replacement; final samples
also check worker B remains stable. Counter sampling is observational evidence,
not a proof over arbitrary malicious code or all future schedules.

## Evidence and remaining gates

Sanitized tests exercise the actual supervisor, broker and controller adapters
with mocked kernel/serial transports: exact shapes and caller roles, stop/drain
ordering, late results, nonreturning stop/resume failures, malformed post-commit
replies, fault mutations, cancellation/loss and partial output. Graph tests
reject 338 element/attribute mutations and remain active under Python `-O`.
The host evidence parser rejects malformed records and contradictory fault
reports; it retries only explicitly allowed startup snapshots.

Saved evidence: `artifacts/lifecycle-boot.log`, `lifecycle-runtime-tests.log`,
`lifecycle-report.txt` and `lifecycle-test.img`. These are builder-produced
records, not independent attestations or signed releases. GitHub Actions also
builds and runs this profile and its saved image.

Still required: authenticated lifecycle administration, real audit integration,
independent management paths for hung brokers/drivers, trusted timers/deadlines,
resource reclamation, persistent incarnation state, resource-owner/DMA
quiescence, supervisor recovery and physical-hardware testing. The experiment
does not complete roadmap milestone 0.3 and confers no authority on the ordinary
terminal or signed administration profiles.
