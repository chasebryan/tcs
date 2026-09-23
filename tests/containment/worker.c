#include "runtime.h"
uintptr_t worker_counter_vaddr;
static uint64_t wk_inc;
static bool wk_started;
void init(void)
{
    struct tcs_lc_result r = ct_response(microkit_ppcall(0, microkit_msginfo_new(CT_HELLO, 0)));
    if (r.status == TCS_LC_OK && r.value) wk_inc = r.value;
    else ct_store(worker_counter_vaddr, UINT64_MAX);
}
void notified(microkit_channel ch)
{
    if (ch != 0 || !wk_inc || wk_started) return;
    wk_started = true; microkit_mr_set(0, wk_inc);
    struct tcs_lc_result r = ct_response(microkit_ppcall(0, microkit_msginfo_new(CT_WORK, 1)));
    if (r.status != TCS_LC_OK || r.value != 1) { ct_store(worker_counter_vaddr, UINT64_MAX); return; }
    ct_spin(worker_counter_vaddr);
}
