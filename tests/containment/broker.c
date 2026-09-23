#include "runtime.h"
uintptr_t broker_counter_vaddr;
static uint64_t br_inc[2], br_pending[2];
static bool br_ready, br_failed;
void init(void) { if (br_ready) br_failed = true; br_ready = true; }
void notified(microkit_channel ch) { (void)ch; }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo m)
{
    if (!br_ready || br_failed || ch > 2) return ct_reply(CT_FAILURE, 0);
    uint64_t label = microkit_msginfo_get_label(m), count = microkit_msginfo_get_count(m);
    if (ch == 2 && count == 0 && (label == CT_KICK || label == CT_HANG)) {
        microkit_mr_set(0, 0);
        m = microkit_ppcall(3, microkit_msginfo_new(CT_VIEW, 1));
        if (!ct_message(m, CT_SLOT, 6)) { br_failed = true; return ct_reply(CT_FAILURE, 0); }
        uint64_t phase = microkit_mr_get(0), inc = microkit_mr_get(1);
        uint64_t issued = microkit_mr_get(2), pending = microkit_mr_get(3);
        if (phase != TCS_LC_ACTIVE || !inc || inc != br_inc[0] || microkit_mr_get(4) || microkit_mr_get(5))
            return ct_reply(TCS_LC_BAD_STATE, 0);
        if (label == CT_KICK) {
            if (issued || pending || br_pending[0]) return ct_reply(TCS_LC_BAD_STATE, 0);
            microkit_notify(0); return ct_reply(TCS_LC_OK, inc);
        }
        if (issued != 1 || pending != 1 || br_pending[0] != 1) return ct_reply(TCS_LC_BAD_STATE, 0);
        ct_spin(broker_counter_vaddr); /* Deliberately NEVER replies to caller. */
    }
    if (ch < 2 && label == CT_HELLO && count == 0 && !br_inc[ch]) {
        microkit_mr_set(0, ch);
        struct tcs_lc_result r = ct_response(microkit_ppcall(3, microkit_msginfo_new(CT_READY, 1)));
        if (r.status == CT_FAILURE || (r.status == TCS_LC_OK && !r.value)) br_failed = true;
        if (br_failed) return ct_reply(CT_FAILURE, 0);
        if (r.status == TCS_LC_OK) br_inc[ch] = r.value;
        return ct_reply(r.status, r.value);
    }
    if (ch < 2 && label == CT_WORK && count == 1) {
        uint64_t inc = microkit_mr_get(0);
        if (!inc || inc != br_inc[ch] || br_pending[ch]) return ct_reply(TCS_LC_STALE, 0);
        microkit_mr_set(0, ch); microkit_mr_set(1, inc);
        struct tcs_lc_result r = ct_response(microkit_ppcall(3, microkit_msginfo_new(CT_BEGIN, 2)));
        if (r.status == CT_FAILURE || (r.status == TCS_LC_OK && r.value != 1)) br_failed = true;
        if (br_failed) return ct_reply(CT_FAILURE, 0);
        if (r.status == TCS_LC_OK) br_pending[ch] = r.value;
        return ct_reply(r.status, r.value);
    }
    return ct_reply(TCS_LC_DENIED, 0);
}
