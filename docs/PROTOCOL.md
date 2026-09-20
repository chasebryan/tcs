# TCS IPC v1

All Seed messages are bounded machine-word messages on the AArch64 substrate. A request label is `0x100 | opcode`. Unknown versions, opcodes, and incorrect word counts are rejected. Generic replies have label `0x180` and exactly two words: status, value. Denied resource operations return value zero.

## Policy endpoint

Policy mutation/access requests below have four words: subject, object, rights, generation. Self-status is the separate zero-word operation described below.

| Opcode | Name | Caller | Fields |
| --- | --- | --- | --- |
| 1 | GRANT | console/admin channel 0 | Valid subject, object 42, READ=1, generation 0 |
| 2 | CHECK | storage channel 1 | Channel-derived subject, object, rights, session generation |
| 3 | REVOKE | console/admin channel 0 | Subject; remaining words zero |
| 4 | QUARANTINE | console/admin channel 0 | Subject; remaining words zero |
| 5 | RESTORE | console/admin channel 0 | Quarantined subject; remaining words zero |

Only subjects 0..3 exist in the policy core. Runtime storage serves client as subject 1. A successful grant returns its new generation. Grant replaces that subject's previous grant. There is no wildcard object, combined right set, or bearer authority token.

Status codes: 0 OK, 1 DENIED, 2 BAD_MESSAGE, 3 STALE, 4 ISOLATED, 5 AUDIT_FULL, 6 EXHAUSTED. AUDIT_FULL also covers an invalid audit acknowledgement; it means required audit evidence is unavailable. Revocation/quarantine still take effect on this status. Failed grant or restore does not change policy.

## Storage and client

Storage READ (16) accepts three words: object, rights, generation. Subject identity comes only from storage channel 0. A permitted request returns the fixture word `0x43494e4958`; this is an in-memory test object, not a filesystem operation.

Client READ (17) forwards those words to storage. Test-only commands 18 and 19 send malformed READ and attempted GRANT requests respectively. Client has no channel to the policy server, and storage accepts no grant operation.

## Self-status

CLIENT_STATUS (20, label `0x114`) takes **zero words** from client channel 0. It forwards STORE_STATUS (21, `0x115`, zero words) to storage channel 0, which forwards SELF_STATUS (6, `0x106`, zero words) to policy channel 1. Policy channel 1 is bound to `TCS_CLIENT_SUBJECT` (1) for this query. No hop accepts a subject selector. The policy admin channel cannot use this self-status identity. No new kernel channels or capabilities were added.

A successful reply has label `0x182` and exactly four words: state, current generation, object, rights. State is 0 restricted, 1 active, or 2 quarantined. Active requires a nonzero generation, object 42, and READ=1. Restricted/quarantined require object=rights=0. A generation may reach `UINT64_MAX`; that does not allow reuse or a new grant. Invalid state/field combinations, reply labels/counts, and generic OK replies are rejected by the snapshot decoder. Errors use the generic two-word reply with a nonzero recognized status and value zero; consumers clear all snapshot fields on error.

This query copies current policy metadata without mutation or audit append. It is intentionally available even after audit exhaustion and in quarantine; it releases no fixture data and cannot mint authority. Repeating it does not consume the finite audit log. Replies are snapshots at policy evaluation time, not authorization proofs or promises that state cannot change before a subsequent read. Every resource access still performs its normal generation/rights check. Generation values are session identifiers, not secret transferable capabilities.

The fixed subject mapping is a seed constraint, not a multi-user identity mechanism. Additional clients must introduce explicit kernel-channel-to-subject bindings and tests; a payload-selected subject must not be substituted for that work.

## Audit

APPEND (32), from policy only, accepts seven words: operation, actor role, subject, object, rights, input generation, decision status. The server assigns a sequence starting at 1 and returns it after copying the record to bounded private memory. Capacity is 64; entries are not overwritten.

COUNT (33), from console only, takes zero words and returns the record count. GET (34), also console only, accepts a zero-based index; success has label `0x122` and eight words: sequence followed by the seven stored fields. Invalid indices return the generic DENIED reply.

This log records decisions, including denial, rather than durable receipts. Malformed wire messages rejected before policy evaluation are not logged. There is no log reset RPC. The boot demonstration intentionally fills the log and finishes in a state that denies further ordinary access.

## Next protocol work

Introduce request IDs, service incarnation IDs, bounded asynchronous queues, deadlines, and explicit commit acknowledgements before concurrent bulk operations. Bulk data will use separately granted memory mappings with ownership and revocation rules. Capability transfer and server replacement require a lifecycle protocol in addition to these application-session messages.
