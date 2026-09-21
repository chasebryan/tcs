#ifndef TCS_LIFECYCLE_RUNTIME_TEST_H
#define TCS_LIFECYCLE_RUNTIME_TEST_H
#include "tcs/ipc.h"
#include "tcs/lifecycle.h"
#include <stdatomic.h>
#if !defined(TCS_LIFECYCLE_TEST_PROFILE) || (!defined(TCS_RELEASE_PROFILE) && !defined(TCS_HOST_TEST))
#error "Lifecycle experiment requires its explicit release-kernel test profile"
#endif
/* Test-only ABI. No operator credentials, policy server or real audit. */
#define LT_CONTROL 0x710u
#define LT_STATUS 0x711u
#define LT_READY 0x712u
#define LT_EVENT 0x713u
#define LT_VIEW 0x714u
#define LT_HELLO 0x720u
#define LT_BEGIN 0x721u
#define LT_COMPLETE 0x722u
#define LT_KICK 0x723u
#define LT_DRAIN 0x724u
#define LT_BROKER_STATUS 0x725u
#define LT_RESULT 0x780u
#define LT_SNAPSHOT 0x781u
#define LT_SLOT 0x782u
#define LT_STATS 0x783u
#define LT_FAILURE 99u
#define LT_WORDS 18u
#define LT_STATS_WORDS 7u
#define LT_FAULT_ADDRESS UINT64_C(0x0dead000)
_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "test counter requires lock-free 64-bit atomics");

static inline bool lt_message(microkit_msginfo m, uint64_t label, unsigned count)
{ return microkit_msginfo_get_label(m) == label && microkit_msginfo_get_count(m) == count; }
static inline microkit_msginfo lt_reply(uint64_t status, uint64_t value)
{
    microkit_mr_set(0, status); microkit_mr_set(1, value);
    return microkit_msginfo_new(LT_RESULT, 2);
}
static inline struct tcs_lc_result lt_response(microkit_msginfo m)
{
    if (!lt_message(m, LT_RESULT, 2)) return (struct tcs_lc_result){LT_FAILURE, 0};
    uint64_t status = microkit_mr_get(0), value = microkit_mr_get(1);
    if (status > TCS_LC_EXHAUSTED || (status != TCS_LC_OK && value))
        return (struct tcs_lc_result){LT_FAILURE, 0};
    return (struct tcs_lc_result){status, value};
}
static inline void lt_slot_words(unsigned first, const struct tcs_lc_slot *s)
{
    microkit_mr_set(first, s->state); microkit_mr_set(first + 1, s->incarnation);
    microkit_mr_set(first + 2, s->issued); microkit_mr_set(first + 3, s->pending);
    microkit_mr_set(first + 4, s->stopped); microkit_mr_set(first + 5, s->drained);
}
#endif
