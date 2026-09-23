#include "runtime.h"
uintptr_t request_vaddr, receipt_vaddr;
static struct tcs_reduction sp_control;
static bool sp_ready, sp_failed;

static void sp_fail(void)
{
    sp_failed = true;
    microkit_pd_stop(1); microkit_pd_stop(2);
}
static bool sp_sync(bool notification)
{
    if (!sp_ready || sp_failed) return false;
    uint64_t sample = tcs_rd_sample((const struct tcs_reduction_mailbox *)request_vaddr);
    if (tcs_rd_poll(&sp_control, sample) == TCS_RD_INVALID_STATE) { sp_fail(); return false; }
    uint64_t stops = tcs_rd_stop_mask(&sp_control);
    for (unsigned i = 0; i < 2; ++i) {
        if (!(stops & (UINT64_C(1) << i))) continue;
        struct tcs_lc_slot *s = &sp_control.life.slots[i];
        uint64_t inc = s->incarnation, seq = s->issued;
        /* Closure precedes stop; no outgoing RPC or deferred resume exists.
         * SDK errors do not return. Never confirm before the kernel returns. */
        microkit_pd_stop(i + 1);
        struct tcs_lc_result r = tcs_rd_apply(&sp_control, 0, TCS_LC_SUPERVISOR,
            (struct tcs_lc_event){TCS_LC_STOPPED, i, inc, seq}, false);
        if (r.status != TCS_LC_OK) { sp_fail(); return false; }
    }
    /* Diagnostic proof of this callback path only, never a wire authority.
     * Ordinary polling cannot set it, nor can bootstrap stops. */
    if (notification && stops) ct_store(receipt_vaddr, ct_load(receipt_vaddr) | stops);
    return true;
}
void init(void)
{
    if (sp_ready) { sp_fail(); return; }
    microkit_pd_stop(1); microkit_pd_stop(2);
    sp_ready = true;
    (void)sp_sync(false);
}
void notified(microkit_channel ch) { if (ch == 0) (void)sp_sync(true); }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo m)
{
    /* Capture before any kernel operation can clobber message registers. */
    uint64_t label = microkit_msginfo_get_label(m), count = microkit_msginfo_get_count(m);
    uint64_t a = count > 0 ? microkit_mr_get(0) : 0;
    uint64_t b = count > 1 ? microkit_mr_get(1) : 0;
    if (!sp_sync(false)) return ct_reply(CT_FAILURE, 0);
    if (ch == 0 && label == CT_STATUS && count == 0) {
        microkit_mr_set(0, sp_failed); microkit_mr_set(1, sp_control.inhibited);
        microkit_mr_set(2, sp_control.input_fault); microkit_mr_set(3, sp_control.life.last_incarnation);
        ct_slot_words(4, &sp_control.life.slots[0]); ct_slot_words(10, &sp_control.life.slots[1]);
        return microkit_msginfo_new(CT_SNAPSHOT, CT_WORDS);
    }
    if (ch == 0 && label == CT_CONTROL && count == 2) {
        if (b >= 2 || (a != TCS_LC_RESERVE && a != TCS_LC_ACTIVATE)) return ct_reply(TCS_LC_INVALID, 0);
        /* Explicit fixture approval/audit, not a credential or real audit. */
        struct tcs_lc_result r = tcs_rd_apply(&sp_control, 0, TCS_LC_ADMIN,
            (struct tcs_lc_event){a, b, a == TCS_LC_RESERVE ? 0 : sp_control.life.slots[b].incarnation, 0}, true);
        if (r.status == TCS_LC_OK && a == TCS_LC_RESERVE) microkit_pd_resume((microkit_child)b + 1);
        return ct_reply(r.status, r.value);
    }
    if (ch == 1 && a < 2 && (label == CT_READY || label == CT_VIEW) && count == 1) {
        if (label == CT_VIEW) { ct_slot_words(0, &sp_control.life.slots[a]); return microkit_msginfo_new(CT_SLOT, 6); }
        struct tcs_lc_result r = tcs_rd_apply(&sp_control, 0, TCS_LC_SUPERVISOR,
            (struct tcs_lc_event){TCS_LC_STARTED, a, sp_control.life.slots[a].incarnation, 0}, false);
        return ct_reply(r.status, r.value);
    }
    if (ch == 1 && label == CT_BEGIN && count == 2 && a < 2) {
        struct tcs_lc_result r = tcs_rd_apply(&sp_control, 0, TCS_LC_BROKER,
            (struct tcs_lc_event){TCS_LC_BEGIN, a, b, 0}, false);
        return ct_reply(r.status, r.value);
    }
    /* This experiment exposes NO drain or remote stop-confirmation operation. */
    return ct_reply(TCS_LC_DENIED, 0);
}
seL4_Bool fault(microkit_child child, microkit_msginfo m, microkit_msginfo *reply)
{ (void)child; (void)m; (void)reply; sp_fail(); return seL4_False; }
