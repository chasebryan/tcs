#include "tcs/ipc.h"

static struct tcs_policy live;
static bool ready;

void init(void)
{
    ready = true;
    microkit_dbg_puts("TCS policy ready\n");
}

void notified(microkit_channel ch) { (void)ch; }

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    uint64_t label = microkit_msginfo_get_label(msg);
    if (!ready || (ch != 0 && ch != 1))
        return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (microkit_msginfo_get_count(msg) != 4 ||
        label < TCS_LABEL(TCS_GRANT) || label > TCS_LABEL(TCS_RESTORE))
        return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});

    struct tcs_request r = {label & 0xff, microkit_mr_get(0),
        microkit_mr_get(1), microkit_mr_get(2), microkit_mr_get(3)};
    enum tcs_actor actor = ch == 0 ? TCS_ADMIN : TCS_STORAGE;
    struct tcs_policy candidate = live;
    struct tcs_result decision = tcs_policy_apply(&candidate, actor, r);

    /* Preserve request/decision in local storage across nested IPC. */
    microkit_mr_set(0, r.op);
    microkit_mr_set(1, actor);
    microkit_mr_set(2, r.subject);
    microkit_mr_set(3, r.object);
    microkit_mr_set(4, r.rights);
    microkit_mr_set(5, r.generation);
    microkit_mr_set(6, decision.status);
    struct tcs_result logged = tcs_response(microkit_ppcall(2,
        microkit_msginfo_new(TCS_LABEL(TCS_AUDIT_APPEND), 7)));
    struct tcs_result committed = tcs_policy_commit(&live, &candidate, r,
        decision, logged.status == TCS_OK);
    if (logged.status != TCS_OK)
        microkit_dbg_puts("TCS audit unavailable: grants/reads denied; reductions remain effective\n");
    return tcs_reply(committed);
}
