#ifndef TCS_POLICY_H
#define TCS_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define TCS_SUBJECTS 4u
#define TCS_OBJECT 42u
#define TCS_READ 1u
#define TCS_WRITE 2u
#define TCS_SAMPLE UINT64_C(0x43494e4958)

enum tcs_actor { TCS_UNKNOWN, TCS_ADMIN, TCS_STORAGE };
enum tcs_state { TCS_RESTRICTED, TCS_ACTIVE, TCS_QUARANTINED };
enum tcs_op { TCS_GRANT = 1, TCS_CHECK, TCS_REVOKE,
                TCS_QUARANTINE, TCS_RESTORE };
enum tcs_status { TCS_OK, TCS_DENIED, TCS_BAD_MESSAGE,
                    TCS_STALE, TCS_ISOLATED, TCS_AUDIT_FULL,
                    TCS_EXHAUSTED };

struct tcs_subject {
    uint64_t generation;
    uint64_t object;
    uint64_t rights;
    enum tcs_state state;
};

struct tcs_policy { struct tcs_subject subjects[TCS_SUBJECTS]; };
struct tcs_request {
    uint64_t op, subject, object, rights, generation;
};
struct tcs_result { uint64_t status, value; };

/* Actor identity is supplied by the IPC adapter, never by message payload. */
struct tcs_result tcs_policy_apply(struct tcs_policy *policy,
    enum tcs_actor actor, struct tcs_request request);

/* Commit only after an audit acknowledgement, except authority reductions. */
struct tcs_result tcs_policy_commit(struct tcs_policy *live,
    const struct tcs_policy *candidate, struct tcs_request request,
    struct tcs_result decision, bool audit_ok);

#endif
