#include "tcs/deadline.h"

bool tcs_dl_valid(const struct tcs_deadline *owner)
{
    if (!owner || !tcs_rd_valid(&owner->control) || owner->clock_fault > 1 ||
        (owner->clock_fault && owner->control.inhibited != TCS_RD_ALL)) return false;
    for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
        bool unused = owner->control.life.slots[i].state == TCS_LC_UNUSED;
        if (unused != (owner->end[i] == 0)) return false;
        if (owner->end[i] && owner->last_tick >= owner->end[i] &&
            !(owner->control.inhibited & (UINT64_C(1) << i))) return false;
    }
    return true;
}

enum tcs_dl_status tcs_dl_poll(struct tcs_deadline *owner, uint64_t requests,
    struct tcs_dl_reading reading)
{
    if (!tcs_dl_valid(owner)) return TCS_DL_INVALID_STATE;
    struct tcs_deadline next = *owner;
    if (!reading.ok || reading.tick < next.last_tick) next.clock_fault = 1;
    if (next.clock_fault) {
        requests |= TCS_RD_ALL;
    } else {
        next.last_tick = reading.tick;
        for (unsigned i = 0; i < TCS_LC_SLOTS; ++i)
            if (next.end[i] && reading.tick >= next.end[i])
                requests |= UINT64_C(1) << i;
    }
    if (tcs_rd_poll(&next.control, requests) == TCS_RD_INVALID_STATE)
        return TCS_DL_INVALID_STATE;
    *owner = next;
    return next.clock_fault || next.control.input_fault ? TCS_DL_FAULT : TCS_DL_OK;
}

struct tcs_lc_result tcs_dl_apply(struct tcs_deadline *owner, uint64_t requests,
    struct tcs_dl_reading reading, enum tcs_lc_actor actor,
    struct tcs_lc_event event, bool audit_ok, uint64_t duration)
{
    if (tcs_dl_poll(owner, requests, reading) == TCS_DL_INVALID_STATE)
        return (struct tcs_lc_result){TCS_LC_INVALID, 0};
    if (event.op == TCS_LC_RESERVE ?
        (!duration || duration > UINT64_MAX - owner->last_tick) : duration != 0)
        return (struct tcs_lc_result){TCS_LC_INVALID, 0};
    struct tcs_deadline next = *owner;
    struct tcs_lc_result result = tcs_rd_apply(&next.control, 0, actor, event, audit_ok);
    if (result.status == TCS_LC_OK) {
        if (event.op == TCS_LC_RESERVE) next.end[event.slot] = next.last_tick + duration;
        *owner = next;
    }
    return result;
}

uint64_t tcs_dl_stop_mask(const struct tcs_deadline *owner)
{
    return tcs_dl_valid(owner) ? tcs_rd_stop_mask(&owner->control) : TCS_RD_ALL;
}

bool tcs_dl_allows(const struct tcs_deadline *owner, uint64_t slot, uint64_t incarnation)
{
    return tcs_dl_valid(owner) && tcs_rd_allows(&owner->control, slot, incarnation);
}
