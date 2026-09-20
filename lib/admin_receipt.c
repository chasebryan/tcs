#include "tcs/admin_receipt.h"

bool tcs_admin_execution_valid(struct tcs_admin_command c)
{
    struct tcs_request r = c.request;
    if (!c.sequence || r.subject >= TCS_SUBJECTS || r.generation) return false;
    if (r.op == TCS_GRANT) return r.object == TCS_OBJECT && r.rights == TCS_READ;
    return (r.op == TCS_REVOKE || r.op == TCS_QUARANTINE || r.op == TCS_RESTORE) &&
        r.object == 0 && r.rights == 0;
}
bool tcs_admin_receipt_valid(struct tcs_admin_command c, struct tcs_admin_receipt r)
{
    struct tcs_admin_command e = r.command;
    if (!tcs_admin_execution_valid(c) || !tcs_admin_execution_valid(e) ||
        c.sequence != e.sequence || c.expected_generation != e.expected_generation ||
        c.request.op != e.request.op || c.request.subject != e.request.subject ||
        c.request.object != e.request.object || c.request.rights != e.request.rights ||
        r.audit_ok > 1 || r.applied > 1 || !tcs_snapshot_valid(r.post)) return false;
    bool reduction = c.request.op == TCS_REVOKE || c.request.op == TCS_QUARANTINE;
    bool applied = r.decision == TCS_OK && (r.audit_ok || reduction);
    if (r.applied != applied) return false;
    if (r.decision == TCS_STALE)
        return r.post.generation != c.expected_generation;
    if (r.decision != TCS_OK) {
        if (r.post.generation != c.expected_generation) return false;
        return (r.decision == TCS_ISOLATED && c.request.op == TCS_GRANT && r.post.state == TCS_QUARANTINED) ||
            (r.decision == TCS_DENIED && c.request.op == TCS_RESTORE && r.post.state != TCS_QUARANTINED) ||
            (r.decision == TCS_EXHAUSTED && c.request.op == TCS_GRANT &&
             r.post.generation == UINT64_MAX && r.post.state != TCS_QUARANTINED);
    }
    if (c.request.op == TCS_GRANT && c.expected_generation == UINT64_MAX) return false;
    uint64_t generation = c.expected_generation;
    if (applied && generation != UINT64_MAX) ++generation;
    if (r.post.generation != generation) return false;
    if (!applied) {
        /* An audit-denied restore started quarantined; a successful grant
         * decision cannot have started quarantined. Neither changes state. */
        return c.request.op == TCS_RESTORE ? r.post.state == TCS_QUARANTINED :
            c.request.op == TCS_GRANT && r.post.state != TCS_QUARANTINED;
    }
    if (c.request.op == TCS_GRANT) return r.post.state == TCS_ACTIVE;
    if (c.request.op == TCS_QUARANTINE) return r.post.state == TCS_QUARANTINED;
    if (c.request.op == TCS_RESTORE) return r.post.state == TCS_RESTRICTED;
    return r.post.state != TCS_ACTIVE; /* Revoke does not restore a quarantined subject. */
}
