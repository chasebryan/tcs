#include "tcs/reduction.h"

_Static_assert(TCS_LC_SLOTS == 2, "reduction latch requires two one-use slots");
_Static_assert(ATOMIC_LONG_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2,
    "reduction mailbox requires native lock-free 64-bit atomics");

bool tcs_rd_valid(const struct tcs_reduction *control)
{
    if (!control || !tcs_lc_valid(&control->life) ||
        (control->inhibited & ~TCS_RD_ALL) || control->input_fault > 1 ||
        (control->input_fault && control->inhibited != TCS_RD_ALL)) return false;
    for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
        uint64_t phase = control->life.slots[i].state;
        if ((control->inhibited & (UINT64_C(1) << i)) &&
            phase >= TCS_LC_STARTING && phase <= TCS_LC_ACTIVE) return false;
    }
    return true;
}

enum tcs_rd_poll_status tcs_rd_poll(struct tcs_reduction *control, uint64_t sample)
{
    if (!tcs_rd_valid(control)) return TCS_RD_INVALID_STATE;
    struct tcs_reduction next = *control;
    bool malformed = (sample & ~TCS_RD_ALL) != 0;
    next.inhibited |= malformed ? TCS_RD_ALL : sample;
    if (malformed) next.input_fault = 1;
    for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
        struct tcs_lc_slot *slot = &next.life.slots[i];
        if ((next.inhibited & (UINT64_C(1) << i)) &&
            slot->state >= TCS_LC_STARTING && slot->state <= TCS_LC_ACTIVE) {
            struct tcs_lc_result r = tcs_lc_apply(&next.life, TCS_LC_DETECTOR,
                (struct tcs_lc_event){TCS_LC_CONTAIN, i, slot->incarnation, 0}, false);
            if (r.status != TCS_LC_OK) return TCS_RD_INVALID_STATE;
        }
    }
    *control = next;
    return malformed ? TCS_RD_MALFORMED : TCS_RD_OK;
}

struct tcs_lc_result tcs_rd_apply(struct tcs_reduction *control, uint64_t sample,
    enum tcs_lc_actor actor, struct tcs_lc_event event, bool audit_ok)
{
    if (tcs_rd_poll(control, sample) == TCS_RD_INVALID_STATE)
        return (struct tcs_lc_result){TCS_LC_INVALID, 0};
    if (event.slot < TCS_LC_SLOTS &&
        (control->inhibited & (UINT64_C(1) << event.slot)) &&
        (event.op == TCS_LC_RESERVE || event.op == TCS_LC_ACTIVATE))
        return (struct tcs_lc_result){TCS_LC_DENIED, 0};
    return tcs_lc_apply(&control->life, actor, event, audit_ok);
}

uint64_t tcs_rd_stop_mask(const struct tcs_reduction *control)
{
    if (!tcs_rd_valid(control)) return TCS_RD_ALL;
    uint64_t mask = 0;
    for (unsigned i = 0; i < TCS_LC_SLOTS; ++i)
        if (control->life.slots[i].state == TCS_LC_CLOSING && !control->life.slots[i].stopped)
            mask |= UINT64_C(1) << i;
    return mask;
}

bool tcs_rd_allows(const struct tcs_reduction *control, uint64_t slot, uint64_t incarnation)
{
    return tcs_rd_valid(control) && slot < TCS_LC_SLOTS &&
        !(control->inhibited & (UINT64_C(1) << slot)) &&
        tcs_lc_allows(&control->life, slot, incarnation);
}

bool tcs_rd_publish(struct tcs_reduction_mailbox *mailbox, uint64_t requests)
{
    if (!mailbox || (requests & ~TCS_RD_ALL)) return false;
    (void)atomic_fetch_or_explicit(&mailbox->requests, requests, memory_order_release);
    return true;
}

uint64_t tcs_rd_sample(const struct tcs_reduction_mailbox *mailbox)
{
    return mailbox ? atomic_load_explicit(&mailbox->requests, memory_order_acquire) : UINT64_MAX;
}
