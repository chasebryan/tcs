#include "tcs/admin.h"

struct tcs_result tcs_policy_admin_compare_apply(struct tcs_policy *candidate,
    enum tcs_actor actor, struct tcs_admin_command c)
{
    if (actor != TCS_ADMIN) return (struct tcs_result){TCS_DENIED, 0};
    if (!candidate || !c.sequence || c.request.subject >= TCS_SUBJECTS ||
        c.request.op < TCS_GRANT || c.request.op > TCS_RESTORE || c.request.op == TCS_CHECK)
        return (struct tcs_result){TCS_BAD_MESSAGE, 0};
    if (candidate->subjects[c.request.subject].generation != c.expected_generation)
        return (struct tcs_result){TCS_STALE, 0};
    return tcs_policy_apply(candidate, actor, c.request);
}
