#ifndef TCS_IPC_H
#define TCS_IPC_H

#include <microkit.h>
#include "tcs/build_profile.h"
#include "tcs/policy.h"

/* Version 1 occupies the upper byte. Requests have exact word counts. */
#define TCS_LABEL(op) (UINT64_C(0x100) | (uint64_t)(op))
#define TCS_REPLY UINT64_C(0x180)
#define TCS_SNAPSHOT_REPLY UINT64_C(0x182)
#define TCS_SELF_STATUS 6u
#define TCS_STORE_READ 16u
#define TCS_CLIENT_READ 17u
#define TCS_CLIENT_BAD_LENGTH 18u
#define TCS_CLIENT_TRY_GRANT 19u
#define TCS_CLIENT_STATUS 20u
#define TCS_STORE_STATUS 21u
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

static inline microkit_msginfo tcs_snapshot_reply(struct tcs_snapshot s)
{
    if (s.status != TCS_OK)
        return tcs_reply((struct tcs_result){s.status, 0});
    if (!tcs_snapshot_valid(s))
        return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    microkit_mr_set(0, s.state);
    microkit_mr_set(1, s.generation);
    microkit_mr_set(2, s.object);
    microkit_mr_set(3, s.rights);
    return microkit_msginfo_new(TCS_SNAPSHOT_REPLY, 4);
}

static inline struct tcs_snapshot tcs_snapshot_response(microkit_msginfo msg)
{
    struct tcs_snapshot invalid = {TCS_BAD_MESSAGE, 0, 0, 0, 0};
    if (microkit_msginfo_get_label(msg) == TCS_REPLY) {
        struct tcs_result r = tcs_response(msg);
        if (r.status >= TCS_DENIED && r.status <= TCS_EXHAUSTED && r.value == 0)
            return (struct tcs_snapshot){r.status, 0, 0, 0, 0};
        return invalid; /* A generic OK is not a snapshot. */
    }
    if (microkit_msginfo_get_label(msg) != TCS_SNAPSHOT_REPLY ||
        microkit_msginfo_get_count(msg) != 4)
        return invalid;
    struct tcs_snapshot s = {TCS_OK, microkit_mr_get(0), microkit_mr_get(1),
        microkit_mr_get(2), microkit_mr_get(3)};
    return tcs_snapshot_valid(s) ? s : invalid;
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
