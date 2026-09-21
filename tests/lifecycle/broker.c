#include "runtime.h"
static bool br_ready, br_failed;
static uint64_t br_inc[2], br_pending[2], br_bad, br_stale, br_begun, br_drains;

static struct tcs_lc_result br_call(uint64_t op, uint64_t slot, uint64_t inc, uint64_t seq)
{
    microkit_mr_set(0, op); microkit_mr_set(1, slot); microkit_mr_set(2, inc); microkit_mr_set(3, seq);
    struct tcs_lc_result r = lt_response(microkit_ppcall(3, microkit_msginfo_new(LT_EVENT, 4)));
    if (r.status == LT_FAILURE) br_failed = true; /* Uncertain result is never retried. */
    return r;
}
static bool br_view(uint64_t slot, uint64_t words[6])
{
    microkit_mr_set(0, slot);
    microkit_msginfo m = microkit_ppcall(3, microkit_msginfo_new(LT_VIEW, 1));
    if (!lt_message(m, LT_SLOT, 6)) { br_failed = true; return false; }
    for (unsigned i = 0; i < 6; ++i) words[i] = microkit_mr_get(i);
    if (words[0] > TCS_LC_RETIRED || words[1] != br_inc[slot] || words[4] > 1 || words[5] > 1) {
        br_failed = true; return false;
    }
    return true;
}
void init(void) { if (br_ready) br_failed = true; br_ready = true; }
void notified(microkit_channel ch) { (void)ch; }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo m)
{
    if (!br_ready || br_failed || ch > 2) return lt_reply(LT_FAILURE, 0);
    if (ch == 2) {
        if (lt_message(m, LT_BROKER_STATUS, 0)) {
            microkit_mr_set(0, br_failed); microkit_mr_set(1, br_bad); microkit_mr_set(2, br_stale);
            microkit_mr_set(3, br_begun); microkit_mr_set(4, br_drains);
            microkit_mr_set(5, (br_pending[0] != 0) | ((uint64_t)(br_pending[1] != 0) << 1));
            microkit_mr_set(6, (br_inc[0] != 0) | ((uint64_t)(br_inc[1] != 0) << 1));
            return microkit_msginfo_new(LT_STATS, LT_STATS_WORDS);
        }
        if (lt_message(m, LT_KICK, 1) || lt_message(m, LT_DRAIN, 1)) {
            uint64_t label = microkit_msginfo_get_label(m), slot = microkit_mr_get(0), view[6];
            if (slot >= 2 || !br_inc[slot]) return lt_reply(TCS_LC_INVALID, 0);
            if (!br_view(slot, view)) return lt_reply(LT_FAILURE, 0);
            if (label == LT_KICK) {
                if (view[0] != TCS_LC_ACTIVE || br_pending[slot]) return lt_reply(TCS_LC_BAD_STATE, 0);
                microkit_notify((microkit_channel)slot);
                return lt_reply(TCS_LC_OK, br_inc[slot]);
            }
            if (view[0] != TCS_LC_CLOSING || !view[4] || view[5] || view[3] != br_pending[slot])
                return lt_reply(TCS_LC_BAD_STATE, 0);
            /* This fixture holds one ticket only: no disk, DMA, external I/O or
             * work forwarded to another owner. Admission/completion always calls
             * the gate owner. The stopped worker cannot enqueue later requests. */
            br_pending[slot] = 0;
            struct tcs_lc_result r = br_call(TCS_LC_DRAINED, slot, br_inc[slot], view[2]);
            if (r.status != TCS_LC_OK) br_failed = true;
            else ++br_drains;
            return lt_reply(r.status, r.value);
        }
        return lt_reply(TCS_LC_INVALID, 0);
    }
    /* Worker identity is ch, never a payload slot. No control/drain endpoint. */
    uint64_t slot = ch;
    if (lt_message(m, LT_HELLO, 0)) {
        if (br_inc[slot]) { ++br_bad; return lt_reply(TCS_LC_BAD_STATE, 0); }
        microkit_mr_set(0, slot);
        struct tcs_lc_result r = lt_response(microkit_ppcall(3, microkit_msginfo_new(LT_READY, 1)));
        if (r.status == LT_FAILURE || (r.status == TCS_LC_OK && !r.value)) {
            br_failed = true; return lt_reply(LT_FAILURE, 0);
        }
        if (r.status == TCS_LC_OK) br_inc[slot] = r.value;
        return lt_reply(r.status, r.value);
    }
    bool begin = lt_message(m, LT_BEGIN, 1), complete = lt_message(m, LT_COMPLETE, 2);
    if (!begin && !complete) { ++br_bad; return lt_reply(TCS_LC_INVALID, 0); }
    uint64_t inc = microkit_mr_get(0), sequence = complete ? microkit_mr_get(1) : 0;
    if (!inc || inc != br_inc[slot] || (complete && (!sequence || sequence != br_pending[slot]))) {
        ++br_stale; return lt_reply(TCS_LC_STALE, 0);
    }
    struct tcs_lc_result r = br_call(begin ? TCS_LC_BEGIN : TCS_LC_COMPLETE, slot, inc, sequence);
    if (r.status == TCS_LC_OK) {
        if (!r.value || (complete && r.value != sequence)) { br_failed = true; return lt_reply(LT_FAILURE, 0); }
        br_pending[slot] = begin ? r.value : 0;
        if (begin) ++br_begun;
    }
    return lt_reply(r.status, r.value);
}
