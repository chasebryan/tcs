#include "runtime.h"
static struct tcs_lifecycle sv_life;
static bool sv_ready, sv_failed;
static uint64_t sv_faults, sv_ip, sv_address, sv_fsr;

static void sv_fail(void)
{
    sv_failed = true;
    for (unsigned i = 1; i <= TCS_LC_SLOTS; ++i) microkit_pd_stop(i);
}
static struct tcs_lc_result sv_apply(enum tcs_lc_actor actor, uint64_t op,
    uint64_t slot, uint64_t inc, uint64_t seq, bool fixture_audit)
{ return tcs_lc_apply(&sv_life, actor, (struct tcs_lc_event){op, slot, inc, seq}, fixture_audit); }
static struct tcs_lc_result sv_contain(uint64_t slot, enum tcs_lc_actor actor)
{
    uint64_t inc = sv_life.slots[slot].incarnation, seq = sv_life.slots[slot].issued;
    struct tcs_lc_result r = sv_apply(actor, TCS_LC_CONTAIN, slot, inc, 0, false);
    if (r.status != TCS_LC_OK) return r;
    /* No queued start/resume work: effects are synchronous and single-owner.
     * The SDK helper does not return on kernel error. Never record success first. */
    microkit_pd_stop((microkit_child)slot + 1);
    r = sv_apply(TCS_LC_SUPERVISOR, TCS_LC_STOPPED, slot, inc, seq, false);
    if (r.status != TCS_LC_OK) sv_fail();
    return r;
}
void init(void)
{
    if (sv_ready) { sv_fail(); return; }
    /* Parent priority exceeds both children; they have not executed yet. */
    microkit_pd_stop(1); microkit_pd_stop(2);
    sv_ready = true;
}
void notified(microkit_channel ch) { (void)ch; }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo m)
{
    if (!sv_ready || sv_failed || ch > 1) return lt_reply(LT_FAILURE, 0);
    if (ch == 0 && lt_message(m, LT_STATUS, 0)) {
        microkit_mr_set(0, sv_failed); microkit_mr_set(1, sv_faults);
        microkit_mr_set(2, sv_life.last_incarnation);
        lt_slot_words(3, &sv_life.slots[0]); lt_slot_words(9, &sv_life.slots[1]);
        microkit_mr_set(15, sv_ip); microkit_mr_set(16, sv_address); microkit_mr_set(17, sv_fsr);
        return microkit_msginfo_new(LT_SNAPSHOT, LT_WORDS);
    }
    if (ch == 0 && lt_message(m, LT_CONTROL, 2)) {
        uint64_t op = microkit_mr_get(0), slot = microkit_mr_get(1);
        if (slot >= TCS_LC_SLOTS || (op != TCS_LC_RESERVE && op != TCS_LC_ACTIVATE && op != TCS_LC_CONTAIN))
            return lt_reply(TCS_LC_INVALID, 0);
        struct tcs_lc_result r;
        if (op == TCS_LC_CONTAIN) r = sv_contain(slot, TCS_LC_ADMIN);
        else {
            /* Fixture approval only; not an audit server or operator session. */
            r = sv_apply(TCS_LC_ADMIN, op, slot, op == TCS_LC_RESERVE ? 0 : sv_life.slots[slot].incarnation, 0, true);
            if (r.status == TCS_LC_OK && op == TCS_LC_RESERVE)
                microkit_pd_resume((microkit_child)slot + 1);
        }
        return lt_reply(r.status, r.value);
    }
    if (ch == 1 && (lt_message(m, LT_READY, 1) || lt_message(m, LT_VIEW, 1))) {
        uint64_t slot = microkit_mr_get(0);
        if (slot >= TCS_LC_SLOTS) return lt_reply(TCS_LC_INVALID, 0);
        if (microkit_msginfo_get_label(m) == LT_VIEW) {
            lt_slot_words(0, &sv_life.slots[slot]);
            return microkit_msginfo_new(LT_SLOT, 6);
        }
        /* Broker bound this startup handshake to a kernel-assigned worker channel.
         * It demonstrates entry into the fixture, not general worker health. */
        struct tcs_lc_result r = sv_apply(TCS_LC_SUPERVISOR, TCS_LC_STARTED, slot,
            sv_life.slots[slot].incarnation, 0, false);
        return lt_reply(r.status, r.value);
    }
    if (ch == 1 && lt_message(m, LT_EVENT, 4)) {
        uint64_t op = microkit_mr_get(0), slot = microkit_mr_get(1);
        uint64_t inc = microkit_mr_get(2), seq = microkit_mr_get(3);
        if (op != TCS_LC_BEGIN && op != TCS_LC_COMPLETE && op != TCS_LC_DRAINED)
            return lt_reply(TCS_LC_DENIED, 0);
        if (slot >= TCS_LC_SLOTS) return lt_reply(TCS_LC_INVALID, 0);
        /* Runtime is stricter than the abstract model: no drain before stop. */
        if (op == TCS_LC_DRAINED && !sv_life.slots[slot].stopped)
            return lt_reply(TCS_LC_BAD_STATE, 0);
        struct tcs_lc_result r = sv_apply(TCS_LC_BROKER, op, slot, inc, seq, false);
        return lt_reply(r.status, r.value);
    }
    return lt_reply(TCS_LC_INVALID, 0);
}
seL4_Bool fault(microkit_child child, microkit_msginfo m, microkit_msginfo *reply)
{
    (void)reply;
    uint64_t ip = 0, address = 0, instruction = 0, fsr = 0;
    if (microkit_msginfo_get_count(m) == seL4_VMFault_Length) {
        ip = microkit_mr_get(seL4_VMFault_IP); address = microkit_mr_get(seL4_VMFault_Addr);
        instruction = microkit_mr_get(seL4_VMFault_PrefetchFault); fsr = microkit_mr_get(seL4_VMFault_FSR);
    }
    bool valid = sv_ready && !sv_failed && !sv_faults && child == 2 &&
        microkit_msginfo_get_label(m) == seL4_Fault_VMFault && microkit_msginfo_get_count(m) == 4 &&
        ip >= 0x200000 && ip < 0x300000 && !(ip & 3) && address == LT_FAULT_ADDRESS && instruction == 0 &&
        !(fsr >> 32) && ((fsr >> 26) & 63) == 0x24 && (fsr & (1u << 25)) &&
        !(fsr & (15u << 7)) && (fsr & (1u << 6)) && (fsr & 63) >= 4 && (fsr & 63) <= 7 &&
        sv_life.slots[1].state == TCS_LC_ACTIVE && sv_life.slots[1].pending == 1;
    if (!valid) sv_fail();
    else {
        sv_ip = ip; sv_address = address; sv_fsr = fsr; sv_faults = 1;
        if (sv_contain(1, TCS_LC_SUPERVISOR).status != TCS_LC_OK) sv_fail();
    }
    return seL4_False; /* Never reply/resume a faulted worker. */
}
