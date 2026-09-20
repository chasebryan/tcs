# Runtime memory and device isolation evidence

This is a **test-only release-kernel image**, not an administration or production profile. Run `make isolation-smoke`, or `make isolation-smoke-saved` to test the included image without a compiler/SDK. Both use diskless, networkless AArch64 QEMU and the same private local QMP socket as the terminal tests.

## What is attempted

Six child protection domains execute one explicit AArch64 load, store, or branch. They have no service channels, device mappings, IRQs, or child domains. Only the last two receive the specific mappings below. A breakpoint follows each forbidden operation: unexpected success therefore produces a different fault and fails the test.

| Child | Attempt | Virtual address | Required fault |
| --- | --- | --- | --- |
| 1 | Read using the UART's physical address as a virtual address | `0x09000018` | Data-read translation fault |
| 2 | Write using the serial server's UART virtual alias | `0x04000000` | Data-write translation fault |
| 3 | Read using policy's dedicated test-page address | `0x05000000` | Data-read translation fault |
| 4 | Write using that policy test-page address | `0x05000000` | Data-write translation fault |
| 5 | Write through an explicitly read-only mapping of the test page | `0x06000000` | Data-write permission fault |
| 6 | Fetch an instruction from a writable, non-executable page | `0x07000000` | Instruction permission fault |

Virtual addresses in one domain do not grant access to another domain's mapping, nor does knowing a device's physical address create a mapping. These tests observe specific examples of those boundaries; they are not an exhaustive memory-isolation proof.

## Observer and protected state

`system/isolation.system` starts from the terminal graph, with three explicit test-only changes:

- A terminal wrapper owns six probe TCBs as their parent/fault handler. The observer can stop/resume those probes, not the other services. It reuses the real terminal's bounded UART output, and later its normal command implementation.
- A policy wrapper initializes one dedicated page with a public canary value. This is **not** the live authorization table. Policy owns its writable mapping; the observer and child 5 have read-only aliases. Children 3 and 4 have no mapping of the page.
- Child 6 owns a separate writable/non-executable page containing a return instruction.

The parent runs at a higher priority than its probes and stops them before their initial entry points run. It starts them one at a time, only after the previous report has drained through the real serial server. Kernel-delivered fault messages are checked for child identity/order, VM-fault tag, exact word count, fault address, instruction/data distinction, read/write bit, exception class, instruction length, and translation-versus-permission syndrome. Data-fault PCs must be aligned and in the probe image range; the execute-fault PC must equal the NX target. Exact per-instruction PC equality is not claimed for data faults.

The observer captures all fault words before nested IPC overwrites the message registers. Before launch and after each accepted fault, it checks the canary and queries the existing client → storage → policy self-status path. The expected subject remains restricted with zero generation/object/rights. Wrong/duplicate faults or changed protected state produce failure, not a pass. A faulting child is never replied to or restarted; the next independent child is resumed instead. This is fault observation and containment of test probes, **not general server recovery**.

Only serial owns the UART mapping/IRQ. Reports travel through its existing IPC channel; no debug output route is enabled. The host requires all six structured fault reports and the protected-state verdict. Silence, a timeout, a standalone pass marker, a wrong fault class, or missing reports cannot succeed. Afterward it exercises the full real-UART terminal suite, including live status, denied reads, editing, malformed input, and three serial breaks.

## Keeping test authority out of normal images

The normal seed/debug-terminal/release-terminal image targets never link the observer, policy wrapper, or probes. Their ordinary descriptions still forbid children and extra mappings. The isolation checker removes only exact validated test additions, then validates the entire remaining terminal graph. Twenty-one negative mutations cover added authority, altered mappings/permissions, identities, scheduling, executable names, text, and missing/duplicate resources. The normal release harness rejects the test-image preamble unless explicitly run with `--isolation`.

The test uses separately built wrapper/probe ELFs under `build/isolation` and unmodified release service ELFs. It does not grant the terminal a policy-admin endpoint, add commands that mutate policy, or change normal service protocols. The saved ordinary images remain byte-for-byte unchanged in this increment.

## Limits and references

This is evidence for six deliberate accesses on QEMU's pinned AArch64 release configuration. It does not cover arbitrary driver compromise, all addresses or instruction sequences, DMA/IOMMU isolation, speculative side channels, physical hardware, concurrent revocation, data already disclosed, or arbitrary corruption of the policy table. The host, emulator, SDK, observer, and expected-result code remain trusted. Host decoder/observer mocks test the verifier, not the MMU.

The exception layout and syndrome interpretation follow the bundled [seL4 AArch64 VM-fault implementation](https://github.com/seL4/seL4/blob/6e7c3b733d296cfd88d5fbf635c96e447a882374/src/arch/arm/64/kernel/vspace.c) and [Microkit AArch64 fault decoder](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/monitor/src/main.c). Parent fault handling follows the [pinned Microkit manual](https://github.com/seL4/microkit/blob/8780fab8699f5aeec109b21325ff37c741736b24/docs/manual.md#fault). Their exact source archives remain in `third_party/sources/`.
