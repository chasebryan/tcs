#include "tcs/ipc.h"

void init(void) { microkit_dbg_puts("TCS storage ready (read-only fixture)\n"); }
void notified(microkit_channel ch) { (void)ch; }

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    if (ch != 0)
        return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (!tcs_message(msg, TCS_STORE_READ, 3))
        return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});

    /* Channel 0 identifies client/subject 1; payload cannot override identity. */
    struct tcs_request r = {TCS_CHECK, 1, microkit_mr_get(0),
        microkit_mr_get(1), microkit_mr_get(2)};
    struct tcs_result decision = tcs_policy_call(1, r);
    if (decision.status == TCS_OK)
        decision.value = TCS_SAMPLE;
    else
        decision.value = 0;
    return tcs_reply(decision);
}
