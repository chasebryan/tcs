# TCS development checkpoints

## Current direction

Continue through the roadmap in small, reviewable increments. Preserve the working seed, keep runtime claims narrower than the observed evidence, and never grant a new input path administrative authority by default.

## 2026-09-23 — native-only nonrenewable lifetime deadlines

Added a private single-owner deadline wrapper around the reduction/lifecycle model. Successful audited reservation commits a checked, nonrenewable lifetime before startup; activation and worker traffic cannot extend it. Every event observes trusted time and reduction requests first, including rejected events. Equality expires a slot; failed or regressing clock observations permanently inhibit both slots. Pending tickets survive, and only the existing separately correlated stop/drain evidence can retire them. No guest, timer/device access, authority graph or credential changes.

Sanitized native tests cover all live phases, late startup, denied renewal, overflow/wrap, clock failure, malformed requests, invalid private state, missing/forged/stale confirmations, both confirmation orders and 200,000 repeated observations. A separately implemented timing reference matches 201,254 actions over 2,141 bounded states, with additional fresh-read/event and 64-bit boundary matrices. Full native checks and 77 Python tests pass, including all graph/parser/policy regressions. The separate AArch64 object compiles, compiler static analysis reports no diagnostics, and build-plan tests exclude it from all ten guests. All ten images rebuild byte-for-byte unchanged and pass their actual QEMU regression suites. Native evidence is saved in `artifacts/deadline-tests.log`; clean-machine results are tracked per published commit in GitHub Actions.

The [trusted-time contract](DEADLINES.md) explicitly treats repeated notifications as wakeup hints, not elapsed time. Equal samples cannot detect a frozen/replayed clock; no callback means no progress guarantee. This is model evidence, not an independent timer, flood-resistant scheduling or physical-stop deadline proof. Runtime clock/wakeup authority, clock-source validation and worst-case scheduling remain next gates; milestones 0.3/0.4 remain incomplete.

## 2026-09-21 — independent worker containment while broker is hung

Added a separate seven-domain release fixture with an independent observer, deliberately blocked caller, spinning broker, supervisor and two pre-created workers. The observer has no broker RPC. A one-way request page plus notification reaches the supervisor, which polls the reduction latch, closes the worker gate and records STOPPED only after actual TCB suspension. The broker continues spinning, the caller never returns, and the outstanding ticket remains pending. No drain endpoint exists; reuse/reactivation and replacement attempts are refused.

The host observes worker and broker progress before reduction, then five identical worker-counter samples while broker progress continues. A separate supervisor-owned receipt records only stops performed inside the notification callback. After publication, the observer refuses all supervisor RPCs until that receipt appears, so status polling cannot rescue missing notification delivery. The native withheld-notification test checks this explicitly. Malformed receipts fail rather than being treated as a wait condition.

Sanitized tests exercise the actual five new adapters with mocked kernel/serial operations, nonreturning-loop traps, stop/resume failure, message-register clobbering, early inhibition, late startup, malformed post-commit replies, caller/shape rejection, repeated notifications, input loss and partial echo. The native negative matrix was corrected to avoid invoking the valid intentional-hang branch outside its active trap scope; the corrected tests pass. All native checks and 74 Python tests pass; the exact authority graph rejects 507 mutations. All nine earlier images rebuild byte-for-byte unchanged and pass their emulator suites. The tenth image and its evidence are saved separately.

See [the runtime contract](CONTAINMENT-RUNTIME.md). This contains a worker despite a stuck broker; it does not contain/recover the broker, survive a stuck serial driver before publication, establish a deadline, authenticate lifecycle administration or reclaim resources. Approval/audit remain fixtures, and milestones 0.3/0.4 remain incomplete. No credential or ordinary/signed-profile authority changes.

## 2026-09-21 — native-only sticky reduction latch

Added a private single-owner wrapper around the existing lifecycle model and a separate one-word C11 atomic mailbox. Observed containment bits remain set for each one-use slot, including requests before selection. Every wrapped event polls first; rejected or unauthorized events cannot roll back independently observed reductions. Malformed words inhibit both slots without discarding pending work or inventing physical stop/drain evidence. No code is linked into a guest, and no new authority or credential is introduced.

Sanitized native tests cover startup/activation ordering, stale/forged/missing confirmations, malformed input, private-state corruption, counter boundaries, coalesced requests, hostile clearing after observation and late publication. Two host publisher threads perform 100,000 atomic publications while the owner samples 100,000 times. A separate reduction reference composed with the existing lifecycle reference matches 566,504 actions across 365 bounded states. All native checks and 67 Python tests pass; the new object cross-compiles for AArch64. All nine images rebuild byte-for-byte unchanged and pass their actual QEMU regression suites.

See [the exact reduction contract](REDUCTION.md) and `artifacts/reduction-tests.log`. Publication is not an acknowledgement of observation or containment: a post-sample request can race an earlier-sampled transition, and a hostile withdrawn request can go unobserved. Native C11 threads do not establish cross-domain memory ordering, notification delivery, scheduling or deadlines. The independent runtime management path and actual hung-broker injection are still next; milestones 0.3/0.4 remain incomplete. The stale bottom-of-file next-step text has been updated to reflect the already completed signed UART experiment.

## 2026-09-21 — separate runtime supervisor and ticket-drain experiment

Added a six-domain release-kernel test profile with an independent supervisor, broker, two one-use workers, UART controller and existing serial driver. The supervisor owns the lifecycle gate and makes no outgoing protected call. Broker admissions/completions check that owner and bind worker identity to kernel channels. The supervisor records stop only after actual TCB suspension returns; broker drain waits for stop and discards its private pending ticket. All approval/audit decisions are explicit fixtures; no policy server or operator credential is involved.

Actual QEMU shows a noncooperating worker counter progressing, then stable across five observations after stop. A distinct second worker rejects two stale-incarnation requests and a malformed identity request, then produces a specifically checked kernel fault. Both workers retire without reuse, and attempts to select consumed slots fail. The native adapter tests cover kernel-operation failures, malformed post-commit replies, control/worker separation, fault corruption, input cancellation/loss and partial output. All native checks and 64 Python tests pass; graph tests reject 338 mutations. The eight pre-existing images remain byte-identical and pass their QEMU suites; the new ninth image is saved separately.

One full regression attempt hit UART input loss during an existing signed-packet test. The guest discarded the incomplete frame; an isolated rerun passed with unchanged image bytes. No transport protection was disabled. Native fault-test development also corrected a positive test syndrome from same-EL to lower-EL data abort, and the evidence checker now retries only explicitly allowed startup snapshots, never contradictory fault evidence.

See [runtime contract and limits](LIFECYCLE-RUNTIME.md). This is observed kernel stop plus ticket-only quiescence, not general reclamation, authenticated lifecycle administration, persistent recovery or a deadline proof. The UART controller can still block/fail with a hung broker/serial service; an independent surviving management path is a next gate. Milestone 0.3 remains incomplete. No ordinary terminal/admin authority or image bytes changed.

## 2026-09-20 — bounded one-use worker lifecycle model

Added an allocation-free, single-owner lifecycle model for two distinct pre-created worker slots. Selection and activation require separate audited administration; containment closes an additional work gate without waiting on audit. Replacement waits for independently correlated supervisor stop and broker drain confirmations. Old-incarnation completions and repeated receipts cannot revive a closing/retired worker, and retired slots are never reused. Unknown/worker identities have no transition authority; a detector is reduction-only. The model does not grant resource rights or modify signed administrator replay state.

Sanitized native tests and a separate Python reference model pass 79,232 event comparisons across 185 bounded reachable states, plus malformed-state, startup-failure, pending-work, missing-confirmation, pool-exhaustion and counter-boundary cases. All native checks and 57 Python tests pass. The model cross-compiles for AArch64 but is excluded from all eight guest builds; all eight images rebuild byte-for-byte unchanged and pass QEMU regressions. Evidence is saved in `artifacts/lifecycle-model-tests.log`.

This begins lifecycle design; it does not complete milestone 0.3 or implement a supervisor. [The contract](LIFECYCLE.md) distinguishes model results from actual kernel/resource evidence and documents late-resume ordering, trusted endpoint identity, mailbox synchronization, deadline/budget and quiescence gates. Next: a separate test-only runtime supervisor/worker experiment after those adapter contracts are made concrete. No in-place restart, memory/capability reclamation, credential, hardware or new guest authority is introduced.

## 2026-09-20 — shared native sanitizer objects, isolated build profiles

Seven native tests/fixture helpers now reuse one separately compiled pair of sanitized Monocypher objects. Address/undefined-behavior instrumentation and strict warnings remain enabled for both dependency compilation and test linking. Operator tooling remains unsanitized and separately compiled; freestanding guest profiles cannot consume the host objects. Fixture-only macros stay on their application/test sources, and pinned upstream crypto files are unchanged.

Three new build tests cover exact object reuse, all guest targets, mode separation, parallel scheduling, repeat-build reuse, and source/header/recipe invalidation. A fresh real four-job native build passes all native checks and 54 Python tests. All eight guest images rebuild byte-for-byte unchanged and pass their QEMU suites, including the twelve invalid launch configurations. Saved build-test evidence is `artifacts/host-build-tests.log`; actual clean-machine timing is available per commit in GitHub Actions, not assumed from fewer compile commands.

This is build efficiency, not a runtime security change. Next work moves toward explicit lifecycle/containment models with incarnation and quiescence rules; no administrator restart shortcut or new credential is introduced.

## 2026-09-20 — fail-closed launch and uncertain-response regression coverage

Added twelve actual interactive-guest launch failures: absent/short/long context, wrong header/mode, zero realm/boot/key, unknown fixture key, legacy test header, DMA-enabled firmware, and an operator-mode context carrying a public fixture key with its mode marker flipped. Each case submits twice and requires administrator NOT_READY, unchanged restricted live status, and denied reads. A banner alone is never a pass. These are explicit test-only direct launches, not new operator launcher options.

Both native administrator adapters now reject 64 wrong bootstrap word counts, seven wrong labels, every single-bit corruption of the 16-byte header, and zero public fields without policy/audit execution. Recalling initialization with a changed context cannot replace the live identity or reset replay state. This is a mocked-adapter initialization test, not safe crashed-server restart evidence.

The signed terminal tests now exercise every defined admission status, representative invalid values, 64 wrong reply counts, six wrong labels, and corruption of all 13 receipt words. Uncertain completion never appears as rejection; malformed replies never appear as valid execution receipts. A separate blocked-echo/loss schedule verifies whole-frame discard, no submission, and ordinary status recovery. All native tests and 51 Python groups pass, along with the expanded actual UART suite. Runtime sources and all eight boot image bytes are unchanged; saved evidence and documentation are updated. GitHub Actions records clean-machine checks for the published commit.

Next: make the expensive native sanitizer build reuse mode-specific dependency objects, then investigate bounded lifecycle/containment models. No real credential, restart bypass, authenticated host receipt, new device authority, or production readiness is introduced.

## 2026-09-20 — one-shot launch and signed interactive UART

Connected the host workflow to a separate eight-domain release guest. An exclusive session reservation is written before launching; failed execution and competing launches cannot normally reuse it. Public context and image copies remain in the private session directory; no private seed or extra file descriptor reaches QEMU. Bootstrap alone reads the firmware context and supplies it to administrator over private IPC. Terminal submits exact signed packets, never plaintext administration or direct policy calls.

The bounded packet parser accepts exactly 384 lowercase hex digits, handles CRLF and cancellation, and discards the entire packet on invalid input, excess length, or transport loss. It waits for echo completion before submitting and checks correlated receipt shape/state before display. Existing administrator execution/pending semantics are shared unchanged with the test profile.

Local native checks and all 50 Python groups pass, including twelve host-tool groups, both administrator adapters, exact graph mutations, profile separation, all packet lengths through 386, all 256 byte classes, and cancellation/loss at every frame position. Real QEMU tests pass the complete review/sign/one-shot launch flow, twelve policy transitions/receipts, audit-full reductions, replay and cross-launch rejection, malformed frames/signatures, and three injected UART breaks. A macOS descriptor-based image-loading failure was caught by real boot testing and replaced with private per-session image copies.

Two separately built images are included: `interactive.img` (experimental operator mode) and `interactive-fixture.img` (explicitly public-fixture-only). All positive guest authentication uses public RFC fixtures; the operator guest's refusal of those fixtures is tested. No real credential was generated/imported or successful real-operator login claimed. All six prior images rebuild byte-for-byte unchanged and pass regression scenarios. See [operator lifecycle and limits](OPERATOR.md); GitHub Actions records clean-machine checks for each published commit.

Next acceptance gates: explicit uncertainty/recovery semantics and better operator visibility without trusting unsigned display, plus broader malformed bootstrap/transport failure schedules. Production authorization, durable state/receipts, credential protection, snapshot/service-restart recovery, and kernel capability revocation remain open. No automated retries, snapshot resume, or key import are introduced.

Clean-machine follow-up: GCC rejected pointer spelling in a function definition whose header used an array parameter. The definition now uses the same explicit packet bound as its declaration; no warning checks are disabled and no runtime semantics change. CI results must be read for the follow-up commit, not assumed from the local compiler's acceptance.

## 2026-09-20 — experimental trusted-host review and signing

Implemented an explicit host workflow for local identity creation, public context preparation, command review, and signing. It derives the public half from the private seed, binds approval to the exact command/key/realm/boot, and creates one non-overwriting request file per sequence. Private local directories/files, no symlink traversal, exact lengths, owner/link/mode/ACL checks, repository exclusion, core-dump suppression, and best-effort secret wiping bound the host interface. No ambient credentials, imports, automatic retries, or guest transport are provided.

Public test fixtures exercise actual signatures/admission, all operations and integer limits, altered approval/context/key, malformed public data, file/ACL/link rejection, four competing signers, entropy failure, partial writes, and uncertain synchronization. Fixture formats and binary are separate; operator mode refuses known RFC fixtures. The new public context codec also cross-compiles for AArch64, but no guest accepts it yet. No real credential was generated/imported. See [workflow and explicit limits](OPERATOR.md).

All 45 Python tests and native checks pass locally. All six images rebuild byte-for-byte unchanged and pass their actual QEMU regression suites. Saved host evidence is `artifacts/operator-tests.log`; remote validation is recorded per published commit in GitHub Actions.

## 2026-09-20 — signed administrator/policy IPC and definitive receipts

Connected a separate administrator server to a strict conditional-policy adapter in an eight-domain release test profile. Policy accepts no legacy administration on that channel and independently sequences requests. Receipts bind the full command to its decision, audit outcome, applied flag, and post-state. Only validated receipts clear administrator pending state; invalid replies after commit block further admission without retrying. Ordinary profiles retain their prior graphs.

Native sanitized real-adapter tests pass malformed channel/shape/boot cases, post-auth policy changes, nested register clobber, every corrupted receipt word, malformed audit acknowledgements, duplicate execution, and counter exhaustion. The real guest passes 12 signed commands and verifies each receipt against current policy through the ordinary client path. Audit-failed revoke/quarantine apply; grant/restore do not. UART commands remain read-only afterward. Public fixtures only, no real credential or deployment provisioning. See [IPC contract and limits](ADMIN-IPC.md).

The receipt validator also accepts 176 valid state/generation/operation/audit/precondition combinations and rejects inverted applied flags. All 35 Python tests pass. The previous five images rebuild byte-for-byte unchanged; the new administration image and its evidence are separate.

## 2026-09-20 — trusted-launch transport and real guest signature tests

Added a separate two-domain release test profile: one firmware-context owner and the existing isolated UART driver, with no policy authority. A bounded fw_cfg reader refuses DMA/unknown features, malformed directory data, duplicate target names/selectors, missing items, and wrong sizes. An exact test-only format carries realm, fresh launch nonce, and a public fixture key. Its real guest Ed25519 verifier checks signed commands, pending/replay state, and context binding. No real key is read or created.

Fifteen actual QEMU cases pass, including two fresh launches, cross-launch/key/realm mismatches, malformed or missing boot/command data, bad signature, DMA-enabled refusal, and reset termination with no repeated boot. Native sanitized reader tests and 33 Python tests cover callback failure, directory bounds, all topology attributes, and transcript corruption. This is a tested host-launch boundary, not operator provisioning or snapshot/in-VM-restart protection. See [boot context scope](BOOT-CONTEXT.md).

## 2026-09-20 — signed-administration and replay foundation

Added exact 192-byte signed packets, real Ed25519 verification through pinned unmodified Monocypher 4.0.3, realm/boot binding, nonwrapping request sequences, one-pending-request admission, and definitive-completion handling. A separate policy-owner helper checks the signed expected generation immediately before candidate mutation, then uses existing audit commit semantics. None of this code is wired to a live admin endpoint or included in boot images yet.

Sanitized host tests pass RFC vectors, 1,536 one-bit corruptions, exact lengths/encoding, unaligned input, correctly signed malformed commands, replay/context/key mismatch, pending/wrong receipts, exhausted sequences, stale generation, and audit-failure behavior. A test explicitly demonstrates that reusing a boot identity after resetting volatile state permits replay. Fresh trusted boot identity is therefore a required integration gate, not an implemented feature. All four new files compile for freestanding AArch64; 29 Python tests include exact dependency-source provenance checks. No real credential was created/imported. See [administration contract](ADMIN.md).

All four existing image profiles rebuild byte-for-byte unchanged and pass their QEMU regression suites. The new native administration transcript is saved separately as `artifacts/admin-core-tests.log`; unchanged guest behavior is not evidence of a live authentication service.

## 2026-09-20 — observed release-kernel isolation faults

Added a separate test-only release image with six child probes, a parent fault observer, and a dedicated policy-owned canary page. Actual QEMU faults reject ungranted UART addresses, unmapped policy-page reads/writes, writing through a read-only mapping, and fetching from a non-executable page. Kernel messages are checked for identity/order, shape, address, access type, and syndrome. The canary and the caller's live restricted policy metadata are checked before launch and after each fault. All six reports are required; silence is failure. The real terminal and serial-break suite runs afterward.

Native sanitized tests cover syndrome decoding and the actual observer adapter, including duplicate/wrong faults, state tampering, message-register clobbering, and output backpressure. Twenty-eight Python tests include 21 isolation-graph mutations and malformed/missing/partial runtime evidence. Normal graphs, service protocols, and saved ordinary image bytes are unchanged. Test supervision and page-sharing authority exist only in the separately checked test image. See [isolation evidence and limits](ISOLATION.md); this is not a claim of arbitrary driver containment, physical-hardware isolation, or general server recovery.

## 2026-09-20 — separate release-kernel terminal

Added a release-kernel terminal with separate objects, ELFs, SDK headers/libraries, image, and saved evidence. Every server checks its intended profile against generated kernel flags. Banners identify the profile; the emulator harness rejects mismatches, any release boot preamble, and diagnostic interleaving. The same six-domain graph is used, with no new authority or administrative endpoint. Debug seed and terminal targets remain available.

Local sanitized tests and 22 Python tests pass, including profile-guard, build-routing, and harness-negative cases. All three newly built images and their saved copies pass actual QEMU scenarios. Both terminal variants pass three injected serial breaks and Enter/Ctrl-C recovery. Debug evidence still checks audit sequence numbers; release evidence checks observable rejection/denial/recovery without claiming access to silent internal counters.

Testing caught a Make pattern-rule overlap that could link release servers against debug libraries; explicit static target sets now separate those paths, with a regression test for every server link. The release configuration removes the shared SDK debug-printing route, but does not add authenticated input, physical-hardware support, formal verification, fault supervision, or production readiness. The upstream `CONFIG_VERIFICATION_BUILD` name is not a verification claim.

## 2026-09-20 — visible editing and real UART break recovery

Added bounded safe echo, single-cell tab/delete behavior, cancellation display, and deferred command execution until echo is flushed. Transport loss produces an explicit notice and continues rejecting the incomplete line until Enter or Ctrl-C. The host terminal adapter test covers full/partial writes, ordering, loss, and malformed replies. Exhaustive single-byte echo tests cover all 256 input values.

The actual QEMU test now injects three serial breaks through a private temporary QMP socket. It observes the prefix before injection and the driver loss report afterward, verifies that interrupted commands do not reach policy, then proves Enter/Ctrl-C recovery using fresh commands and audit sequence numbers. The test matches exact response bytes rather than treating echoed prompt text as completion. A sandbox initially blocked the local socket; the authorized local-socket test passed. No TCP listener or guest network interface was added.

Limits remain explicit: no Unicode/full-screen editing, no physical UART evidence, no coverage claim for every overrun/queue-loss schedule, and the debug kernel still shares the output device.

## 2026-09-20 — live channel-bound self-status

The terminal's `status` command now queries actual policy metadata through the existing client/storage chain. Every request has zero words; policy binds the subject to its storage channel. Added a bounded four-word snapshot contract and strict reply decoding. No additional capabilities, administrator route, mutation, or audit append was introduced. Snapshot errors expose no metadata, and a snapshot never replaces the normal access check.

Local evidence: sanitized core and adapter tests (including all 1–64-word malformed requests at each hop, unknown callers/versions, malformed replies, no mutation/audit effects, and lifecycle freshness), actual QEMU seed lifecycle/status checks, and 80 repeated UART status queries followed by a read that still becomes audit record 1. Both rebuilt profiles and their saved images pass their emulator scenarios. GitHub Actions provides clean-machine validation per published commit.

## 2026-09-20 — isolated UART and working read-only terminal

Implemented a separate six-domain boot profile with a PL011 driver, private bounded receive queue, transport-loss marker, partial writes, bounded IRQ/terminal handlers, and UART-driven command processing. The terminal has no policy-admin endpoint. `help` and `version` work; reads reach the real policy/audit chain and are denied by default. `status` reports its missing endpoint instead of fabricating live state. The seed's existing administrative test profile is preserved separately.

Local evidence: sanitized policy/terminal/serial tests, 100,000 serial-buffer transitions, mocked driver IPC/register negative cases, 35 topology mutations across both profiles, optimized-Python checker regression, and scripted real QEMU UART cases. The script checks malformed/admin commands, integer boundaries, CRLF, editing, cancellation, line overflow, control bytes, denied reads, and recovery. Both development images and transcripts are saved under `artifacts/`; clean-machine evidence is tracked in GitHub Actions for each published commit.

Limits: no input echo yet; debug-kernel output still shares the UART; no exclusive/trusted display path, authentication, administrator terminal, or physical-hardware support. Runtime input-loss injection is still a verification gate; native driver tests model it but do not establish real-device behavior. See [terminal contract](TERMINAL.md).

## 2026-09-20 — terminal foundation and explicit communication permissions

Implemented:

- Allocation-free terminal line handling and a read-only command parser, with explicit length, integer bounds, CRLF handling, cancellation, and whole-line rejection after overflow or transport loss.
- Disabled unused notification permissions on all 12 seed channel ends. The six existing protected-call paths remain.
- Replaced optimization-sensitive assertions in the system checker with always-enabled validation. The checker now validates the entire seed resource schema, program identities, channel bindings, notification rights, and scheduling configuration, rejecting additional memory mappings, IRQs, child domains, and unknown attributes.
- Added native terminal tests, 100,000 deterministic input bytes, topology mutation tests, and a Python optimized-mode regression test.

The terminal core is not yet wired to a running terminal. No administrator authentication, UART driver, or new policy endpoint is claimed in this checkpoint. See [terminal contract](TERMINAL.md).

Local validation passed: sanitized policy and terminal tests, 50,000 policy transitions, 100,000 terminal input bytes, 11 Python tests (including the topology mutation cases), and the real QEMU IPC scenario with notification sending disabled. The rebuilt image is recorded in `artifacts/`; the original seed remains available in Git history. Remote validation is recorded per commit in [GitHub Actions](https://github.com/chasebryan/tcs/actions/workflows/check.yml).

## Next bounded increment

Inspect the pinned platform/kernel timer interfaces and specify a test-only driver-independent wakeup and fixed clock source for the isolated containment experiment. The native deadline model and trusted-time contract are now tested, but no guest uses them. The observer still uses serial before publication. Specify exact device/IRQ authority, source validation, bounded handler/acknowledgement work and scheduling/flood failure semantics before changing a guest graph. Then test withheld/coalesced wakeups and stuck serial/broker paths. Continue to refuse replacement without real broker/resource-owner drain; do not turn missing confirmation into success.

The one-shot signed UART workflow above is already implemented experimentally, so it is no longer the next increment. Its real provisioning, verifier restart, durable replay and authenticated-receipt gates remain open. Keep credentials operator-controlled, uncertain requests unretried, and all existing images/authority graphs unchanged during the new experiment.
