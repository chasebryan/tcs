# Experimental lifetime deadlines

Status: **native-only model**, separately cross-compiled for AArch64. None of the
ten guest profiles links it. No timer, clock/device mapping, IRQ, new authority,
credential or physical-hardware support is added. The existing
[hung-broker experiment](CONTAINMENT-RUNTIME.md) still depends on UART before
publication and does not provide a timing guarantee.

## A fixed lifetime, not a renewable health lease

`struct tcs_deadline` wraps the [reduction latch](REDUCTION.md) and its one-use
[lifecycle model](LIFECYCLE.md). A successful, audited administrator RESERVE sets
that slot's absolute end to the current trusted sample plus a positive duration.
The addition must fit in `uint64_t`; zero/overflow is rejected. The end is committed
with the reservation, before a future adapter could resume the child. Starting,
ready and active time all count. Activation does not restart the lifetime.

The end cannot be extended, cancelled, cleared or renewed, even by the model's
administrator. Other operations require duration zero. Worker messages and claimed
progress never establish health or extend authority. A distinct unused replacement
gets its own end only after the existing model accepts separate stop and drain
evidence for the old worker. Retired slots and their ends remain consumed.

This is intentionally narrower than a general watchdog or renewable lease. A future
renewal design needs independently authorized renewal evidence, freshness and race
semantics; it must not reinterpret this one-use API.

## Observation rules

One trusted, serialized owner keeps all state private. Zero-initialize once only
after the lifecycle bootstrap establishes two distinct unused/stopped domains.
Do not reload untrusted snapshots, reset after faults, or bypass this wrapper by
calling the embedded lifecycle/reduction functions directly. Structural validation
does not authenticate memory or protect against rollback or coherent corruption.

Every call to `tcs_dl_apply` polls first, even if the event, actor, approval or
duration is invalid. Independent wakeups use `tcs_dl_poll`. At a successful sample
with `tick >= end`, the corresponding slot is permanently inhibited. Startup or
active state becomes closing. The original pending ticket, incarnation and issued
sequence survive; elapsed time does not create a completion, STOPPED or DRAINED
confirmation. Equality is expired, and a skipped/coalesced sample may expire a
slot on the next observation without replaying intermediate ticks.

The copied request word retains the reduction latch's behavior: observed bits are
permanent; unknown bits inhibit both slots and latch `input_fault`. A failed clock
read or a tick less than the last accepted tick latches `clock_fault` and inhibits
both slots, including future selection of an unused slot. Last tick freezes after
a clock fault. A later good read cannot recover it. Real correlated stop/drain
confirmations may still retire a closing slot, but do not remove either inhibition.

Poll returns OK for ordinary expiry and valid requests, FAULT while either input
or clock fault is latched, and INVALID_STATE for malformed private state. FAULT
has committed reductions, not rejected them. Apply returns the existing lifecycle
event result after observation; a failed event cannot roll back the observation.
Invalid private state remains unchanged, denies work, and conservatively requests
both stops. A future adapter must handle this and actual kernel-stop failure.

`tcs_dl_stop_mask` is intent, not proof of a physical stop. `tcs_dl_allows` is a
snapshot relative to the **last** poll, not a clock read and never sufficient
authorization for a resource effect. Adapters must sample before admission and
still check ordinary policy. Time can advance after a sample; these functions do
not promise instantaneous physical revocation or retract effects already admitted.

## The trusted-time boundary

`struct tcs_dl_reading { tick, ok }` is a trusted adapter input, **not a message
format or an authentication mechanism**. The adapter must freshly read one fixed,
coherent source without a protected call to any service it is intended to contain.
It must validate source identity, launch/epoch, units, frequency behavior, access
success and read coherence before setting `ok`. Requesters must not supply either
field. A numerically monotonic attacker-provided value is not trusted time.

Ticks are unsigned, nonwrapping integers in a single fixed unit and epoch. This
model does not read a hardware counter, extend a narrow/wrapping counter, convert
units, choose a frequency or trust host wall time. An adapter must either establish
a nonwrapping range for the launch or fail closed before ambiguous wrap. A maximum
end is valid and expires at `UINT64_MAX`; wrap to zero is a clock fault. Source
switches, migration, suspend/resume and snapshots have no recovery path here.

Equal consecutive ticks are valid: multiple events can occur within one clock
quantum. Consequently a frozen clock, replayed equal sample or absent callback is
**not detectable by this model alone**. The native test deliberately demonstrates
that 100,000 equal samples cannot establish elapsed time. An independent source
or external trust assumption is necessary to detect failure of the clock itself.
No callback means no observation and no containment progress guarantee.

## Wakeups, flooding and eventual runtime bounds

The pinned Microkit 2.3.0 [manual](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/docs/manual.md)
and [event loop](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/libmicrokit/src/main.c)
are bundled under `third_party/sources`. Notifications can coalesce, protected
calls block, callbacks are sequential, and there is no Microkit timer/sleep API.
The new library uses none of these interfaces and establishes no new kernel fact.

A future independent timer path must wake an independently runnable supervisor
that does not call UART, broker, worker or audit before containment. A notification
is only a hint to sample the clock/request word, never elapsed-time credit or a
message count. One call performs fixed two-slot work with no allocation, queue,
retry or external call. This bounds model work, not interrupt or scheduling cost.
Repeated or delayed wakeups cannot renew an end or manufacture stop/drain evidence.

Flood resistance still needs a runtime contract: exact sender capabilities,
bounded IRQ acknowledgement/rearming, bounded handler work, priority/budget/period
analysis, and a wakeup that cannot be starved by request traffic or a stuck driver.
A high-priority reduction endpoint can be a denial-of-service path even when it
cannot create privilege. Budget exhaustion must not allow unbounded timer delay;
fairness and prompt service must be demonstrated, not inferred from priorities.

Any physical-stop bound must include clock quantization/error, the worst interval
to a fresh observation (timer delivery, scheduling and preceding handler work),
gate transition cost and actual kernel-stop latency. None is measured or bounded
by these native tests. No numeric wall-clock deadline is asserted. A wedged kernel,
supervisor or sole time source remains outside this model's containment guarantees.

## Verification and next gate

`make deadline-test` runs sanitized native boundary/failure tests and a separately
implemented Python timing reference composed with the existing independent
lifecycle/reduction references. Finite exploration compares 201,254 actions over
2,141 states with ticks/ends at most two, unit lifetimes and at most one issued
ticket. Out-of-bound next states are compared but not expanded. Separate event
matrices cover fresh reads during admission, every existing event/actor corruption,
missing audit, malformed requests, clock failures/regressions, and 64-bit limits.
This is not a formal proof or an exhaustive timing/concurrency exploration.

Native checks also cover preselection inhibition, expiry in all live phases,
nonrenewal, late startup, missing/forged/stale confirmations, both confirmation
orders, replacement, pending-ticket retention, all 64 single-bit request words,
invalid private state and 200,000 repeated observations before/after expiry.
Address/undefined-behavior sanitizers are enabled. `make deadline-cross-check`
compiles one separate AArch64 object; build-plan tests exclude it from all ten
guest builds. Saved native evidence is `artifacts/deadline-tests.log`.

Next gate: inspect the pinned platform/kernel timer interfaces and choose a
test-only driver-independent wakeup and fixed clock source. Specify the exact
authority graph and failure/scheduling contracts before adding device or IRQ
access. Then exercise loss, coalescing, notification floods, stuck serial/broker,
and withheld stop evidence in a separate runtime fixture. Do not weaken stop/drain
requirements to make a deadline test pass. Milestones 0.3 and 0.4 remain incomplete.
