#include "tcs/ipc.h"

void init(void) { microkit_dbg_puts("TCS client ready\n"); }
void notified(microkit_channel ch) { (void)ch; }

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    if (ch != 0)
        return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (tcs_message(msg, TCS_CLIENT_BAD_LENGTH, 0))
        return microkit_ppcall(1, microkit_msginfo_new(TCS_LABEL(TCS_STORE_READ), 0));
    if (tcs_message(msg, TCS_CLIENT_TRY_GRANT, 0))
        return microkit_ppcall(1, microkit_msginfo_new(TCS_LABEL(TCS_GRANT), 0));
    if (!tcs_message(msg, TCS_CLIENT_READ, 3))
        return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    /* Identical three-word payload, copied by the kernel on the next call. */
    return microkit_ppcall(1, microkit_msginfo_new(TCS_LABEL(TCS_STORE_READ), 3));
}
