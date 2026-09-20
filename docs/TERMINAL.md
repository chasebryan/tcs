# Terminal foundation: input is not authority

Status: host-tested core and real QEMU UART runtime. The five-domain seed remains unchanged in role; the new six-domain terminal is a separate development boot profile.

## First input contract

The terminal core in `lib/terminal.c` is allocation-free and uses a 128-byte line buffer, with at most 127 input bytes plus a terminator. Parsing always uses an explicit byte length, so callers do not have to supply a terminated string. Commands are case-sensitive:

| Input | Parsed operation |
| --- | --- |
| `help` | Show the allowed commands |
| `version` | Show the build identity |
| `status` | Query the calling terminal's own status, once that endpoint exists |
| `read <generation>` | Request a fixture read under an existing nonzero decimal session generation |

The parser only produces a command value. It does not perform IPC, grant rights, read policy state, or authenticate a user. A parsed generation is untrusted input; storage and policy must still validate caller identity and the current session. Admin operations have no parser representation and are rejected.

Space and tab may separate/trim arguments. Other embedded control bytes, NUL, escape sequences, and non-ASCII bytes reject the whole line. Backspace and Delete remove a character; Ctrl-C cancels the line. CR, LF, and CRLF delimit commands, with CRLF producing one event. Overflow rejects the entire line until its delimiter or explicit cancellation: truncation must not turn a malformed line into an executable command prefix.

The driver must call `tcs_line_discard()` after any detected input loss or transport error. Otherwise deleting a byte in transit could turn one command into another. A compromised driver can fabricate input; the security boundary is the terminal's limited capability set, not the parser alone.

## Implemented runtime profile

`system/terminal.system` contains terminal, client, storage, policy, audit, and serial protection domains. Only serial receives the uncached, non-executable UART page at physical `0x09000000` and IRQ 33. The seed console is absent. Policy's administrative channel 0 and audit's query channel 1 are not connected to anything.

Terminal calls serial channel 1 through its own channel 0, and calls client through channel 1. Serial can notify terminal but cannot call any server. Existing client → storage → policy → audit calls remain. The topology checker validates this exact graph, not a general permission to add device access. Serial has a 2 ms budget per 10 ms period; other priorities remain ordered along the protected-call graph.

The serial server uses a 256-byte private receive queue and handles at most 64 UART reads per IRQ. Queue overflow or a reported UART error clears queued bytes and sets an explicit loss marker. The terminal discards the current line on that marker. This also prevents a delimiter buffered *before* the gap from authorizing a suffix *after* the gap. Transmission accepts at most 32 bytes per call, returns a partial count instead of spinning on a full FIFO, and notifies on TX readiness. The terminal has a 256-byte output buffer, drains output before accepting another input byte, and executes at most 32 service-loop steps per entry. Notifications are coalesced readiness hints, not byte counts. No shared memory or synchronous call cycle is introduced.

Serial IPC uses versioned labels `0x130` (read, zero words), `0x131` (write, 1–32 words, one byte per word), and read reply `0x181` (two words: flags and byte). Read flags are bit 0 for a byte and bit 1 for detected loss; an absent byte is zero. Write replies use the existing two-word status/value format, with the value equal to the accepted byte count. All words are validated before any write. Consumers reject malformed reply lengths, labels, flags, and counts.

Run `make terminal-run`; Ctrl-A then X exits QEMU. This first terminal has no local echo. `help` and `version` work; `read` exercises actual policy IPC and is denied because no grant authority exists in this profile. `status` honestly reports its unimplemented endpoint. Authentication and a separate administration service remain future gates.

## Limits and evidence

The profile still uses the **debug** SDK. The kernel, monitor, and existing servers can print through the kernel debug UART path; device mapping isolation is therefore **not exclusive output ownership or a trusted display path**. Production isolation requires a separately tested release profile without that debug channel. This driver targets QEMU virt only, not physical hardware, DMA, flow control, or a general terminal emulator. Fixed handler limits and a scheduling budget do not establish end-to-end real-time guarantees under hostile load or a stalled emulator backend.

Sanitized host tests cover ring wrap, overflow/loss ordering, 100,000 buffer transitions, and mocked driver IPC/register cases (including full TX, malformed messages, IRQ work bounds, and error propagation). Topology mutations test attempted admin access, device mappings, IRQ changes, notification direction, and scheduling changes. `make terminal-smoke` tests actual UART input/output, version/help, policy-denied reads, CRLF, editing, cancellation, malformed commands, control bytes, oversized input, and recovery in diskless/networkless QEMU. Native mocks do not prove physical UART behavior. Runtime transport-loss injection and a release-kernel profile remain additional verification gates.

Implementation references: [Microkit serial tutorial](https://docs.sel4.systems/projects/microkit/tutorial/part1.html), [pinned Microkit manual](https://github.com/seL4/microkit/blob/2.3.0/docs/manual.md), and [QEMU virt platform](https://www.qemu.org/docs/master/system/arm/virt). Use the pinned SDK's 2 GiB configuration, not the tutorial's older 1 GiB example.
