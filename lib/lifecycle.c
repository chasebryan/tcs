#include "tcs/lifecycle.h"

bool tcs_lc_valid(const struct tcs_lifecycle *life)
{
    if (!life) return false;
    unsigned occupied = 0;
    for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
        const struct tcs_lc_slot *s = &life->slots[i];
        if (s->state > TCS_LC_RETIRED || s->stopped > 1 || s->drained > 1)
            return false;
        if (s->state == TCS_LC_UNUSED) {
            if (s->incarnation || s->issued || s->pending || s->stopped || s->drained)
                return false;
            continue;
        }
        if (!s->incarnation || s->incarnation > life->last_incarnation ||
            (s->pending && s->pending != s->issued)) return false;
        for (unsigned j = 0; j < i; ++j)
            if (s->incarnation == life->slots[j].incarnation) return false;
        if (s->state != TCS_LC_RETIRED) ++occupied;
        if (s->state == TCS_LC_STARTING || s->state == TCS_LC_READY) {
            if (s->issued || s->pending) return false;
        }
        if (s->state < TCS_LC_CLOSING && (s->stopped || s->drained)) return false;
        if (s->drained && s->pending) return false;
        if (s->state == TCS_LC_CLOSING && s->stopped && s->drained) return false;
        if (s->state == TCS_LC_RETIRED && (!s->stopped || !s->drained || s->pending))
            return false;
    }
    return occupied <= 1;
}

bool tcs_lc_allows(const struct tcs_lifecycle *life, uint64_t slot, uint64_t incarnation)
{
    return tcs_lc_valid(life) && slot < TCS_LC_SLOTS && incarnation != 0 &&
        life->slots[slot].state == TCS_LC_ACTIVE && life->slots[slot].incarnation == incarnation;
}

static bool permitted(enum tcs_lc_actor actor, uint64_t op)
{
    switch (op) {
    case TCS_LC_RESERVE: case TCS_LC_ACTIVATE: return actor == TCS_LC_ADMIN;
    case TCS_LC_STARTED: case TCS_LC_STOPPED: return actor == TCS_LC_SUPERVISOR;
    case TCS_LC_BEGIN: case TCS_LC_COMPLETE: case TCS_LC_DRAINED: return actor == TCS_LC_BROKER;
    case TCS_LC_CONTAIN:
        return actor == TCS_LC_ADMIN || actor == TCS_LC_SUPERVISOR || actor == TCS_LC_DETECTOR;
    default: return false;
    }
}

static struct tcs_lc_result result(uint64_t status, uint64_t value)
{ return (struct tcs_lc_result){status, value}; }

struct tcs_lc_result tcs_lc_apply(struct tcs_lifecycle *life,
    enum tcs_lc_actor actor, struct tcs_lc_event e, bool audit_ok)
{
    if (!tcs_lc_valid(life) || e.slot >= TCS_LC_SLOTS ||
        e.op < TCS_LC_RESERVE || e.op > TCS_LC_DRAINED)
        return result(TCS_LC_INVALID, 0);
    if (!permitted(actor, e.op)) return result(TCS_LC_DENIED, 0);
    if ((e.op != TCS_LC_COMPLETE && e.op != TCS_LC_STOPPED && e.op != TCS_LC_DRAINED && e.sequence) ||
        (e.op == TCS_LC_COMPLETE && !e.sequence) ||
        (e.op == TCS_LC_RESERVE ? e.incarnation != 0 : e.incarnation == 0))
        return result(TCS_LC_INVALID, 0);

    struct tcs_lifecycle next = *life;
    struct tcs_lc_slot *s = &next.slots[e.slot];
    if (e.op != TCS_LC_RESERVE && e.incarnation != s->incarnation)
        return result(TCS_LC_STALE, 0);
    uint64_t value = s->incarnation;
    switch (e.op) {
    case TCS_LC_RESERVE:
        if (s->state != TCS_LC_UNUSED) return result(TCS_LC_BAD_STATE, 0);
        for (unsigned i = 0; i < TCS_LC_SLOTS; ++i)
            if (next.slots[i].state != TCS_LC_UNUSED && next.slots[i].state != TCS_LC_RETIRED)
                return result(TCS_LC_BAD_STATE, 0);
        if (next.last_incarnation == UINT64_MAX) return result(TCS_LC_EXHAUSTED, 0);
        if (!audit_ok) return result(TCS_LC_AUDIT_REQUIRED, 0);
        s->incarnation = ++next.last_incarnation;
        s->state = TCS_LC_STARTING;
        value = s->incarnation;
        break;
    case TCS_LC_STARTED:
        if (s->state != TCS_LC_STARTING) return result(TCS_LC_BAD_STATE, 0);
        s->state = TCS_LC_READY;
        break;
    case TCS_LC_ACTIVATE:
        if (s->state != TCS_LC_READY) return result(TCS_LC_BAD_STATE, 0);
        if (!audit_ok) return result(TCS_LC_AUDIT_REQUIRED, 0);
        s->state = TCS_LC_ACTIVE;
        break;
    case TCS_LC_BEGIN:
        if (s->state != TCS_LC_ACTIVE || s->pending) return result(TCS_LC_BAD_STATE, 0);
        if (s->issued == UINT64_MAX) return result(TCS_LC_EXHAUSTED, 0);
        s->pending = ++s->issued;
        value = s->pending;
        break;
    case TCS_LC_COMPLETE:
        if (s->state != TCS_LC_ACTIVE) return result(TCS_LC_BAD_STATE, 0);
        if (s->pending != e.sequence) return result(TCS_LC_STALE, 0);
        s->pending = 0;
        value = e.sequence;
        break;
    case TCS_LC_CONTAIN:
        if (s->state < TCS_LC_STARTING || s->state > TCS_LC_ACTIVE)
            return result(TCS_LC_BAD_STATE, 0);
        /* Close the gate even without audit. Keep uncertain work pending. */
        s->state = TCS_LC_CLOSING;
        break;
    case TCS_LC_STOPPED: case TCS_LC_DRAINED:
        if (s->state != TCS_LC_CLOSING) return result(TCS_LC_BAD_STATE, 0);
        if (e.sequence != s->issued) return result(TCS_LC_STALE, 0);
        if (e.op == TCS_LC_STOPPED) {
            if (s->stopped) return result(TCS_LC_BAD_STATE, 0);
            s->stopped = 1;
        } else {
            if (s->drained) return result(TCS_LC_BAD_STATE, 0);
            s->drained = 1;
            s->pending = 0;
        }
        if (s->stopped && s->drained) s->state = TCS_LC_RETIRED;
        break;
    default: return result(TCS_LC_INVALID, 0);
    }
    *life = next;
    return result(TCS_LC_OK, value);
}
