/* Separate conditional-mutation adapter; ordinary images use policy.c. */
#include "tcs/admin_ipc.h"

static struct tcs_policy policy_live;
static bool policy_ready;
static uint64_t execution_sequence = 1;
static bool execution_exhausted;
static bool record_decision(struct tcs_request request, enum tcs_actor actor, struct tcs_result decision)
{
    microkit_mr_set(0, request.op); microkit_mr_set(1, actor);
    microkit_mr_set(2, request.subject); microkit_mr_set(3, request.object);
    microkit_mr_set(4, request.rights); microkit_mr_set(5, request.generation);
    microkit_mr_set(6, decision.status);
    struct tcs_result logged = tcs_response(microkit_ppcall(2,
        microkit_msginfo_new(TCS_LABEL(TCS_AUDIT_APPEND), 7)));
    return logged.status == TCS_OK && logged.value >= 1 && logged.value <= TCS_AUDIT_CAPACITY;
}
void init(void) { policy_ready = true; }
void notified(microkit_channel ch) { (void)ch; }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    if (!policy_ready || (ch != 0 && ch != 1)) return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (ch == 1) {
        if (microkit_msginfo_get_label(msg) == TCS_LABEL(TCS_SELF_STATUS)) {
            if (microkit_msginfo_get_count(msg)) return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
            return tcs_snapshot_reply(tcs_policy_self_status(&policy_live, TCS_STORAGE));
        }
        uint64_t label = microkit_msginfo_get_label(msg);
        if (microkit_msginfo_get_count(msg) != 4 || label < TCS_LABEL(TCS_GRANT) || label > TCS_LABEL(TCS_RESTORE))
            return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
        struct tcs_request request = {label & 0xff, microkit_mr_get(0), microkit_mr_get(1),
            microkit_mr_get(2), microkit_mr_get(3)};
        struct tcs_policy candidate = policy_live;
        struct tcs_result decision = tcs_policy_apply(&candidate, TCS_STORAGE, request);
        bool audit_ok = record_decision(request, TCS_STORAGE, decision);
        return tcs_reply(tcs_policy_commit(&policy_live, &candidate, request, decision, audit_ok));
    }
    if (microkit_msginfo_get_label(msg) != TCS_ADMIN_EXECUTE || microkit_msginfo_get_count(msg) != 6)
        return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    struct tcs_admin_command c = tcs_execution_from_words();
    if (!tcs_admin_execution_valid(c)) return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    if (execution_exhausted || c.sequence != execution_sequence)
        return tcs_reply((struct tcs_result){TCS_STALE, 0});
    /* Consume even a stale/audit-denied execution. No uncertain retries. */
    if (execution_sequence == UINT64_MAX) execution_exhausted = true;
    else ++execution_sequence;
    struct tcs_policy candidate = policy_live;
    struct tcs_result decision = tcs_policy_admin_compare_apply(&candidate, TCS_ADMIN, c);
    bool audit_ok = record_decision(c.request, TCS_ADMIN, decision);
    (void)tcs_policy_commit(&policy_live, &candidate, c.request, decision, audit_ok);
    const struct tcs_subject *s = &policy_live.subjects[c.request.subject];
    bool reduction = c.request.op == TCS_REVOKE || c.request.op == TCS_QUARANTINE;
    struct tcs_admin_receipt receipt = {c, decision.status, audit_ok,
        decision.status == TCS_OK && (audit_ok || reduction),
        {TCS_OK, s->state, s->generation, s->object, s->rights}};
    return tcs_admin_receipt_reply(receipt);
}
