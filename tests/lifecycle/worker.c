#include "runtime.h"
#if !defined(LT_WORKER) || LT_WORKER < 1 || LT_WORKER > 2
#error "Select a numbered lifecycle test worker"
#endif
uintptr_t counter_vaddr;
static uint64_t worker_inc;
static bool worker_started;
void init(void)
{
    struct tcs_lc_result r = lt_response(microkit_ppcall(0, microkit_msginfo_new(LT_HELLO, 0)));
    if (r.status == TCS_LC_OK) worker_inc = r.value;
}
void notified(microkit_channel ch)
{
    if (ch != 0 || !worker_inc || worker_started) return;
    worker_started = true;
#if LT_WORKER == 2
    /* Real worker-channel negative calls. Broker records rejection independently. */
    microkit_mr_set(0, 1); microkit_mr_set(1, 1);
    (void)microkit_ppcall(0, microkit_msginfo_new(LT_COMPLETE, 2));
    microkit_mr_set(0, 1);
    (void)microkit_ppcall(0, microkit_msginfo_new(LT_BEGIN, 1));
    microkit_mr_set(0, 0); microkit_mr_set(1, 1);
    (void)microkit_ppcall(0, microkit_msginfo_new(LT_HELLO, 2));
#endif
    microkit_mr_set(0, worker_inc);
    struct tcs_lc_result r = lt_response(microkit_ppcall(0, microkit_msginfo_new(LT_BEGIN, 1)));
    if (r.status != TCS_LC_OK || r.value != 1) return;
    _Atomic(uint64_t) *counter = (_Atomic(uint64_t) *)counter_vaddr;
#if LT_WORKER == 1
    /* Deliberately never returns or cooperates with stop; has a bounded CPU budget. */
    for (uint64_t n = 1;;) {
        atomic_store_explicit(counter, n, memory_order_release);
        if (n != UINT64_MAX) ++n;
    }
#else
    atomic_store_explicit(counter, 128, memory_order_release);
    __asm__ volatile("str xzr, [%0]\n\tbrk #0" :: "r"(LT_FAULT_ADDRESS) : "memory");
#endif
}
