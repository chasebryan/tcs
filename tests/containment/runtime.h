#ifndef TCS_CONTAINMENT_RUNTIME_H
#define TCS_CONTAINMENT_RUNTIME_H
#include "tcs/ipc.h"
#include "tcs/reduction.h"
#if !defined(TCS_CONTAINMENT_TEST_PROFILE) || (!defined(TCS_RELEASE_PROFILE) && !defined(TCS_HOST_TEST))
#error "Containment experiment requires its explicit release-kernel test profile"
#endif
/* Private fixture ABI, not authenticated administration. */
enum { CT_CONTROL = 0x810, CT_STATUS, CT_READY, CT_BEGIN, CT_VIEW,
    CT_HELLO = 0x820, CT_WORK, CT_KICK, CT_HANG,
    CT_RESULT = 0x880, CT_SNAPSHOT, CT_SLOT };
#define CT_FAILURE UINT64_C(99)
#define CT_WORDS 16u
static inline bool ct_message(microkit_msginfo m, uint64_t label, unsigned count)
{ return microkit_msginfo_get_label(m) == label && microkit_msginfo_get_count(m) == count; }
static inline microkit_msginfo ct_reply(uint64_t status, uint64_t value)
{
    microkit_mr_set(0, status); microkit_mr_set(1, value);
    return microkit_msginfo_new(CT_RESULT, 2);
}
static inline struct tcs_lc_result ct_response(microkit_msginfo m)
{
    if (!ct_message(m, CT_RESULT, 2)) return (struct tcs_lc_result){CT_FAILURE, 0};
    uint64_t status = microkit_mr_get(0), value = microkit_mr_get(1);
    if (status > TCS_LC_EXHAUSTED || (status != TCS_LC_OK && value))
        return (struct tcs_lc_result){CT_FAILURE, 0};
    return (struct tcs_lc_result){status, value};
}
static inline void ct_slot_words(unsigned first, const struct tcs_lc_slot *s)
{
    microkit_mr_set(first, s->state); microkit_mr_set(first + 1, s->incarnation);
    microkit_mr_set(first + 2, s->issued); microkit_mr_set(first + 3, s->pending);
    microkit_mr_set(first + 4, s->stopped); microkit_mr_set(first + 5, s->drained);
}
static inline uint64_t ct_load(uintptr_t address)
{ return atomic_load_explicit((_Atomic(uint64_t) *)address, memory_order_acquire); }
static inline void ct_store(uintptr_t address, uint64_t value)
{ atomic_store_explicit((_Atomic(uint64_t) *)address, value, memory_order_release); }
#ifdef TCS_HOST_TEST
_Noreturn void ct_host_spin(uintptr_t address);
#endif
static inline _Noreturn void ct_spin(uintptr_t address)
{
#ifdef TCS_HOST_TEST
    ct_host_spin(address); /* Native trap only; actual nonreturning loop is boot-tested. */
#else
    for (uint64_t n = 1;;) {
        ct_store(address, n);
        if (n != UINT64_MAX) ++n;
    }
#endif
}
#endif
