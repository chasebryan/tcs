# Experimental reduction latch

Status: native-tested model and C11 atomic mailbox, cross-compiled for AArch64;
**not linked into any of the nine guest images**. This is a foundation for an
independent management path, not observed containment of a hung broker/driver.
The [existing runtime experiment](LIFECYCLE-RUNTIME.md) remains unchanged.

## Why a latch instead of a command queue

The pinned Microkit 2.3.0 [manual](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/docs/manual.md)
describes blocking protected calls, nonblocking notifications that may coalesce,
and budget/period scheduling. It provides no timer/sleep API. Its
[event loop](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/libmicrokit/src/main.c)
dispatches callbacks sequentially; a stuck callback cannot process another
notification. These exact sources are included in `third_party/sources`.

The current lifecycle controller calls broker and serial synchronously. Giving
that same controller another operation does not make it survive those calls.
An independent producer and an independently runnable supervisor are necessary
runtime gates. This increment does not add either to a guest.

For the existing two **one-use** worker slots, reduction requests need no payload,
queue, incarnation counter or reset: bit 0 permanently inhibits slot 0; bit 1
permanently inhibits slot 1. Multiple requests can merge. A request for an unused
slot must prevent its future selection, not disappear before it has an incarnation.
This design is unsuitable for reusable slots or general management commands.

## Owner contract

`struct tcs_reduction` contains the existing lifecycle model, a private sticky
inhibit mask and a private sticky malformed-input flag. One trusted owner alone
accesses it. Zero-initialize once after trusted bootstrap has established two
distinct unused/stopped workers, as required by the [lifecycle model](LIFECYCLE.md).
Never reset it after a fault, reload it from an untrusted snapshot, or share its
memory with a requester. Structural validation is not tamper or rollback protection.

`tcs_rd_poll` takes one copied word. It unions valid request bits into private
state and closes any selected starting, ready or active slot's gate. Pending work,
incarnation, sequence and physical confirmations remain unchanged. A zero, older
or repeated word cannot clear an observed request. An old slot's bit does not
target a distinct replacement slot. Any unknown bit permanently inhibits both
slots and sets `input_fault`; this returns MALFORMED **after committing the
reductions**, not an unchanged-state rejection.

`tcs_rd_apply` polls before considering a lifecycle event. Reserve/activate of
an inhibited slot are denied even with approval. Other events keep the original
caller, incarnation, sequence, state and audit rules. Failed/unauthorized events
do not roll back independently sampled reductions. This intentionally differs
from the underlying lifecycle function's unchanged-state-on-error contract.
An adapter must use this wrapper for every event, never bypass it by mutating the
embedded model or invoking `tcs_lc_apply` directly.

`tcs_rd_stop_mask` reports closing slots without a stop confirmation. It is an
intent, not evidence that anything stopped. Both correlated STOPPED and DRAINED
events are still required for retirement; the wrapper never invents either.
The abstract model retains either confirmation order. A particular adapter may
require stop before drain, as the existing runtime fixture does. No missing,
stuck or failed broker is allowed to stand in for a drain confirmation.

Invalid private state cannot authorize work or be repaired by an input word.
Poll returns INVALID_STATE without mutation, apply returns INVALID, and the stop
mask requests both stops conservatively. A future adapter must fail closed and
deal with actual kernel-stop failure; this library performs no kernel operation.

## Publication is not acceptance

The separate mailbox contains one aligned `_Atomic(uint64_t)` word. Its helper
publishes only valid bits using release atomic OR; the owner takes one acquire
load. There is no mutable multiword payload to reread, no clearing operation, no
acknowledgement write, and no notification-count dependency. Native builds require
lock-free 64-bit atomics. Lock-free does not mean wait-free or provide a deadline.

Every writer able to modify the mailbox can inhibit **both** slots, including
denying future use. It cannot grant access, choose an actor, provide audit
approval, confirm a stop/drain or resume a worker through this format. Writer
identity/authority must come from an exact future mapping/channel graph, not a
field inside the word. The C function signature itself is not access control.
Ordinary subject/object/rights checks remain necessary for any resource effect.

Publication only means the word changed. Acceptance linearizes when the owner
samples and latches it. A request published after a sample can race a transition
using that older sample; the next observation closes the gate. A native test
deliberately demonstrates this window rather than claiming instantaneous
revocation. A hostile writer may also withdraw an unobserved request. Private
stickiness protects only requests already observed; a raw cell is not a durable
history of arbitrary adversarial writes. Lost/delayed wakeups and an unscheduled
owner have no progress guarantee in this model.

The C11 threaded tests are in one host process. Cross-protection-domain memory
mapping, object initialization, alignment, cache attributes, generated atomic
instructions, and architecture/compiler memory-ordering rules require separate
validation. This word is not a portable wire format or authenticated receipt.

## Evidence and next gates

`make reduction-test` runs sanitized boundary and concurrent-publication tests,
plus a separately implemented reduction reference composed with the existing
independent lifecycle reference. Exploration visits 365 states and compares
566,504 generated actions with at most one issued request per slot. Transitions
past that request bound are compared but not expanded. Counter-boundary and all
64 single-bit samples are checked separately. These are finite comparisons,
not a formal proof, exhaustive 64-bit search or a concurrent scheduler model.

Two native publisher threads perform 100,000 atomic publications in total while
the single model owner samples 100,000 times. Additional tests cover preselection,
late startup, activation ordering, retained pending work, missing/forged/late
confirmations, both confirmation orders, rejected events, malformed input,
corrupted private state, coalesced requests and hostile clearing after observation.
Address/undefined-behavior sanitizers are enabled; no race-detector or coverage
claim is implied. Saved evidence is `artifacts/reduction-tests.log`.

`make reduction-cross-check` compiles a separate AArch64 object. Build-plan tests
exclude it from all nine guests. No production graph, policy, signed administrator,
credential, image format or saved boot image is changed.

Next: a separately checked test-only producer/supervisor profile that remains
independent of the UART and broker; actual hung-broker injection; visible,
independent stop evidence; observation before admissions and on independent
wakeups; bounded notification handling under flooding; trusted clocks/deadlines
and scheduling analysis. Supervisor handlers must not block on services they
are intended to contain, and late resume work must not undo stop. Drain remains
an explicit resource-owner obligation. This does not complete roadmap 0.3 or 0.4.
