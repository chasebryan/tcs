#ifndef TCS_IPC_H
#define TCS_IPC_H

#include <microkit.h>
#include "tcs/policy.h"

/* Version 1 occupies the upper byte. Requests have exact word counts. */
#define TCS_LABEL(op) (UINT64_C(0x100) | (uint64_t)(op))
#define TCS_REPLY UINT64_C(0x180)
#define TCS_STORE_READ 16u
#define TCS_CLIENT_READ 17u
#define TCS_CLIENT_BAD_LENGTH 18u
#define TCS_CLIENT_TRY_GRANT 19u
#define TCS_AUDIT_APPEND 32u
#define TCS_AUDIT_COUNT 33u
#define TCS_AUDIT_GET 34u
#define TCS_AUDIT_CAPACITY 64u

static inline bool tcs_message(microkit_msginfo msg, uint64_t op, uint64_t count)
{
    return microkit_msginfo_get_label(msg) == TCS_LABEL(op) &&
           microkit_msginfo_get_count(msg) == count;
}

static inline microkit_msginfo tcs_reply(struct tcs_result r)
{
    microkit_mr_set(0, r.status);
    microkit_mr_set(1, r.value);
    return microkit_msginfo_new(TCS_REPLY, 2);
}

static inline struct tcs_result tcs_response(microkit_msginfo msg)
{
    if (microkit_msginfo_get_label(msg) != TCS_REPLY ||
        microkit_msginfo_get_count(msg) != 2)
        return (struct tcs_result){TCS_BAD_MESSAGE, 0};
    return (struct tcs_result){microkit_mr_get(0), microkit_mr_get(1)};
}

static inline struct tcs_result tcs_policy_call(microkit_channel ch,
    struct tcs_request r)
{
    microkit_mr_set(0, r.subject);
    microkit_mr_set(1, r.object);
    microkit_mr_set(2, r.rights);
    microkit_mr_set(3, r.generation);
    return tcs_response(microkit_ppcall(ch, microkit_msginfo_new(TCS_LABEL(r.op), 4)));
}

#endif
