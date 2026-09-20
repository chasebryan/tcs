#include "tcs/ipc.h"

struct record { uint64_t fields[7]; };
static struct record records[TCS_AUDIT_CAPACITY];
static uint64_t count;

void init(void) { microkit_dbg_puts("TCS audit ready (volatile, bounded)\n"); }
void notified(microkit_channel ch) { (void)ch; }

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    if (ch == 1 && tcs_message(msg, TCS_AUDIT_COUNT, 0))
        return tcs_reply((struct tcs_result){TCS_OK, count});
    if (ch == 1 && tcs_message(msg, TCS_AUDIT_GET, 1)) {
        uint64_t index = microkit_mr_get(0);
        if (index >= count)
            return tcs_reply((struct tcs_result){TCS_DENIED, 0});
        microkit_mr_set(0, index + 1);
        for (unsigned i = 0; i < 7; ++i)
            microkit_mr_set(i + 1, records[index].fields[i]);
        return microkit_msginfo_new(TCS_LABEL(TCS_AUDIT_GET), 8);
    }
    if (ch != 0)
        return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (!tcs_message(msg, TCS_AUDIT_APPEND, 7))
        return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    if (count == TCS_AUDIT_CAPACITY)
        return tcs_reply((struct tcs_result){TCS_AUDIT_FULL, 0});
    for (unsigned i = 0; i < 7; ++i)
        records[count].fields[i] = microkit_mr_get(i);
    ++count;
    /* Debug output is a test transcript, not authenticated persistent evidence. */
    microkit_dbg_puts("TCS audit decision ");
    microkit_dbg_put32((seL4_Uint32)count);
    microkit_dbg_puts("\n");
    return tcs_reply((struct tcs_result){TCS_OK, count});
}
