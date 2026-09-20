# Terminal foundation: input is not authority

Status: host-tested core; UART driver and interactive runtime integration are not yet implemented. The five-domain boot image still runs its automated seed scenario and then idles.

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

## Next runtime increment

Add a separate development boot profile with an isolated PL011 serial server and an ordinary terminal protection domain. Only the serial server receives the UART device mapping and IRQ. The ordinary terminal must not receive the seed console's policy-administration endpoint.

Use bounded serial buffers and bounded handlers. Notifications mean "data may be available", not a one-to-one count of received bytes. IPC consumers must drain explicitly and validate reply lengths. Do not create a synchronous call cycle between the terminal and its driver.

The initial profile will be read-only/default-deny. Authentication and a separate administration service remain future gates; connecting terminal input directly to the privileged seed test console is not an acceptable shortcut.

Implementation references: [Microkit serial tutorial](https://docs.sel4.systems/projects/microkit/tutorial/part1.html), [pinned Microkit manual](https://github.com/seL4/microkit/blob/2.3.0/docs/manual.md), and [QEMU virt platform](https://www.qemu.org/docs/master/system/arm/virt). Use the pinned SDK's 2 GiB configuration, not the tutorial's older 1 GiB example.
