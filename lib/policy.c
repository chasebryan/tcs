#include "tcs/policy.h"

static struct tcs_result result(uint64_t status, uint64_t value)
{
    return (struct tcs_result){status, value};
}

struct tcs_result tcs_policy_apply(struct tcs_policy *policy,
    enum tcs_actor actor, struct tcs_request r)
{
    if (r.subject >= TCS_SUBJECTS || r.op < TCS_GRANT || r.op > TCS_RESTORE)
        return result(TCS_BAD_MESSAGE, 0);
    if ((r.op == TCS_CHECK && actor != TCS_STORAGE) ||
        (r.op != TCS_CHECK && actor != TCS_ADMIN))
        return result(TCS_DENIED, 0);

    struct tcs_subject *s = &policy->subjects[r.subject];
    if (r.op == TCS_REVOKE || r.op == TCS_QUARANTINE || r.op == TCS_RESTORE) {
        if (r.object != 0 || r.rights != 0 || r.generation != 0)
            return result(TCS_BAD_MESSAGE, 0);
        if (r.op == TCS_RESTORE && s->state != TCS_QUARANTINED)
            return result(TCS_DENIED, 0);
        s->rights = 0;
        s->object = 0;
        if (s->generation != UINT64_MAX)
            s->generation++;
        if (r.op == TCS_QUARANTINE)
            s->state = TCS_QUARANTINED;
        else if (r.op == TCS_RESTORE || s->state != TCS_QUARANTINED)
            s->state = TCS_RESTRICTED;
        return result(TCS_OK, s->generation);
    }

    if (s->state == TCS_QUARANTINED)
        return result(TCS_ISOLATED, 0);
    /* Seed has exactly one read-only object. No wildcard rights. */
    if (r.object != TCS_OBJECT || r.rights != TCS_READ)
        return result(TCS_DENIED, 0);
    if (r.op == TCS_GRANT) {
        if (r.generation != 0)
            return result(TCS_BAD_MESSAGE, 0);
        if (s->generation == UINT64_MAX)
            return result(TCS_EXHAUSTED, 0);
        s->generation++;
        s->rights = r.rights;
        s->object = r.object;
        s->state = TCS_ACTIVE;
        return result(TCS_OK, s->generation);
    }
    if (s->state != TCS_ACTIVE || s->rights != TCS_READ)
        return result(TCS_DENIED, 0);
    if (r.generation == 0 || r.generation != s->generation)
        return result(TCS_STALE, 0);
    if (s->object != r.object)
        return result(TCS_DENIED, 0);
    return result(TCS_OK, s->generation);
}

struct tcs_result tcs_policy_commit(struct tcs_policy *live,
    const struct tcs_policy *candidate, struct tcs_request r,
    struct tcs_result decision, bool audit_ok)
{
    bool reduction = r.op == TCS_REVOKE || r.op == TCS_QUARANTINE;
    if (decision.status == TCS_OK && (audit_ok || reduction))
        *live = *candidate;
    /* Audit failure must neither enable access nor prevent isolation. */
    if (!audit_ok)
        return result(TCS_AUDIT_FULL, 0);
    return decision;
}
