#include "tcs/deadline.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct tcs_lc_result event(struct tcs_deadline *c, uint64_t tick,
    enum tcs_lc_actor actor, uint64_t op, uint64_t slot, uint64_t inc,
    uint64_t seq, bool audit, uint64_t duration)
{
    struct tcs_lc_result r = tcs_dl_apply(c, 0, (struct tcs_dl_reading){tick, true},
        actor, (struct tcs_lc_event){op, slot, inc, seq}, audit, duration);
    assert(tcs_dl_valid(c));
    if (r.status != TCS_LC_OK) assert(r.value == 0);
    return r;
}

static struct tcs_deadline active(void)
{
    struct tcs_deadline c = {0};
    assert(event(&c, 10, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, 5).value == 1);
    assert(event(&c, 11, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 0, 1, 0, true, 0).status == TCS_LC_OK);
    assert(event(&c, 12, TCS_LC_ADMIN, TCS_LC_ACTIVATE, 0, 1, 0, true, 0).status == TCS_LC_OK);
    assert(event(&c, 13, TCS_LC_BROKER, TCS_LC_BEGIN, 0, 1, 0, true, 0).value == 1);
    assert(c.end[0] == 15 && tcs_dl_allows(&c, 0, 1));
    return c;
}

static void expiry_and_evidence(void)
{
    for (unsigned phase = TCS_LC_STARTING; phase <= TCS_LC_ACTIVE; ++phase) {
        struct tcs_deadline c = {0};
        assert(event(&c, 0, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, 5).status == TCS_LC_OK);
        if (phase >= TCS_LC_READY)
            assert(event(&c, 1, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 0, 1, 0, true, 0).status == TCS_LC_OK);
        if (phase == TCS_LC_ACTIVE)
            assert(event(&c, 2, TCS_LC_ADMIN, TCS_LC_ACTIVATE, 0, 1, 0, true, 0).status == TCS_LC_OK);
        assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){4, true}) == TCS_DL_OK);
        assert(c.control.life.slots[0].state == phase);
        assert(event(&c, 5, TCS_LC_ADMIN, TCS_LC_ACTIVATE, 0, 1, 0, true, 0).status == TCS_LC_DENIED);
        assert(c.control.life.slots[0].state == TCS_LC_CLOSING && c.end[0] == 5);
        assert(!c.control.life.slots[0].stopped && !c.control.life.slots[0].drained);
        assert(tcs_dl_stop_mask(&c) == 1);
        assert(event(&c, 6, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 0, 1, 0, true, 0).status == TCS_LC_BAD_STATE);
    }
    for (unsigned first = 0; first < 2; ++first) {
        struct tcs_deadline c = active();
        assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){15, true}) == TCS_DL_OK);
        assert(c.control.life.slots[0].pending == 1 && !tcs_dl_allows(&c, 0, 1));
        assert(event(&c, 15, TCS_LC_BROKER, TCS_LC_COMPLETE, 0, 1, 1, true, 0).status == TCS_LC_BAD_STATE);
        assert(event(&c, 15, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, 2, 1, true, 0).status == TCS_LC_STALE);
        assert(event(&c, 15, TCS_LC_WORKER, TCS_LC_DRAINED, 0, 1, 1, true, 0).status == TCS_LC_DENIED);
        for (unsigned pass = 0; pass < 2; ++pass) {
            assert(event(&c, 15, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0, true, 5).status == TCS_LC_BAD_STATE);
            bool stop = pass == first;
            assert(event(&c, 15, stop ? TCS_LC_SUPERVISOR : TCS_LC_BROKER,
                stop ? TCS_LC_STOPPED : TCS_LC_DRAINED, 0, 1, 1, false, 0).status == TCS_LC_OK);
            if (!pass) assert(c.control.life.slots[0].state == TCS_LC_CLOSING);
        }
        assert(c.control.life.slots[0].state == TCS_LC_RETIRED);
        assert(event(&c, 15, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, 5).status == TCS_LC_DENIED);
        assert(event(&c, 15, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0, true, 5).value == 2);
        assert(c.end[0] == 15 && c.end[1] == 20);
        assert(tcs_dl_poll(&c, 1, (struct tcs_dl_reading){19, true}) == TCS_DL_OK);
        assert(c.control.life.slots[1].state == TCS_LC_STARTING);
        assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){UINT64_MAX, true}) == TCS_DL_OK);
        assert(c.control.life.slots[1].state == TCS_LC_CLOSING && c.control.inhibited == 3);
    }
}

static void clock_faults_and_rejected_events(void)
{
    for (unsigned fail = 0; fail < 2; ++fail) {
        struct tcs_deadline c = active();
        struct tcs_dl_reading reading = {fail ? UINT64_MAX : 12, !fail};
        assert(tcs_dl_poll(&c, 0, reading) == TCS_DL_FAULT);
        assert(c.last_tick == 13 && c.clock_fault == 1 && c.control.inhibited == 3);
        assert(c.control.life.slots[0].pending == 1 && tcs_dl_stop_mask(&c) == 1);
        struct tcs_deadline saved = c;
        assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){100, true}) == TCS_DL_FAULT);
        assert(memcmp(&c, &saved, sizeof c) == 0); /* No recovery by a later good tick. */
        assert(event(&c, 100, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, 1, 1, false, 0).status == TCS_LC_OK);
        assert(c.control.life.slots[0].pending == 1 && !c.control.life.slots[0].drained);
        assert(event(&c, 100, TCS_LC_BROKER, TCS_LC_DRAINED, 0, 1, 1, false, 0).status == TCS_LC_OK);
        assert(event(&c, 100, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0, true, 5).status == TCS_LC_DENIED);
    }
    for (unsigned bit = 0; bit < 64; ++bit) {
        struct tcs_deadline c = active();
        struct tcs_lc_result r = tcs_dl_apply(&c, UINT64_C(1) << bit,
            (struct tcs_dl_reading){15, true}, TCS_LC_WORKER,
            (struct tcs_lc_event){UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX}, false, 0);
        assert(r.status == TCS_LC_INVALID && r.value == 0 && tcs_dl_valid(&c));
        assert(c.control.inhibited == (bit ? 3 : 1) && c.end[0] == 15);
        assert(c.control.life.slots[0].pending == 1 && tcs_dl_stop_mask(&c) == 1);
        assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){15, true}) == (bit > 1 ? TCS_DL_FAULT : TCS_DL_OK));
    }
    struct tcs_deadline c = {0};
    assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){0, false}) == TCS_DL_FAULT);
    assert(c.control.inhibited == 3 && !c.end[0] && !tcs_dl_stop_mask(&c));
    assert(event(&c, 0, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, 5).status == TCS_LC_DENIED);
}

static void admission_and_boundaries(void)
{
    struct tcs_deadline c = {0};
    for (unsigned actor = 0; actor <= TCS_LC_WORKER; ++actor) {
        if (actor == TCS_LC_ADMIN) continue;
        assert(event(&c, 0, (enum tcs_lc_actor)actor, TCS_LC_RESERVE, 0, 0, 0, true, 5).status == TCS_LC_DENIED);
        assert(!c.end[0]);
    }
    assert(event(&c, 0, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, false, 5).status == TCS_LC_AUDIT_REQUIRED);
    assert(event(&c, 0, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, 0).status == TCS_LC_INVALID);
    assert(event(&c, 1, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, UINT64_MAX).status == TCS_LC_INVALID);
    assert(!c.end[0] && !c.control.life.last_incarnation);
    assert(event(&c, 1, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, UINT64_MAX - 1).status == TCS_LC_OK);
    assert(c.end[0] == UINT64_MAX);
    assert(event(&c, 1, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, 1).status == TCS_LC_BAD_STATE);
    assert(c.end[0] == UINT64_MAX);
    assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){UINT64_MAX - 1, true}) == TCS_DL_OK);
    assert(c.control.life.slots[0].state == TCS_LC_STARTING);
    assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){UINT64_MAX, true}) == TCS_DL_OK);
    assert(c.control.life.slots[0].state == TCS_LC_CLOSING);
    assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){0, true}) == TCS_DL_FAULT);
    c = active();
    assert(event(&c, 15, TCS_LC_ADMIN, TCS_LC_ACTIVATE, 0, 1, 0, true, 5).status == TCS_LC_INVALID);
    assert(c.control.inhibited == 1 && c.end[0] == 15); /* Bad duration cannot undo expiry. */
    c = active();
    c.control.life.last_incarnation = c.control.life.slots[0].incarnation = UINT64_MAX;
    c.control.life.slots[0].issued = c.control.life.slots[0].pending = UINT64_MAX;
    assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){15, true}) == TCS_DL_OK);
    assert(c.control.life.slots[0].pending == UINT64_MAX && tcs_dl_stop_mask(&c) == 1);
}

static void floods_and_observation_boundary(void)
{
    struct tcs_deadline c = active(), saved = c;
    for (unsigned i = 0; i < 100000; ++i)
        assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){13, true}) == TCS_DL_OK);
    assert(memcmp(&c, &saved, sizeof c) == 0); /* Count of notifications is NOT elapsed time. */
    /* A stale-but-equal read is undetectable here. This is a limit, not success
     * against a frozen clock. Freshness/progress require an external contract. */
    assert(tcs_dl_allows(&c, 0, 1));
    assert(tcs_dl_poll(&c, 0, (struct tcs_dl_reading){30, true}) == TCS_DL_OK);
    assert(!tcs_dl_allows(&c, 0, 1) && c.control.life.slots[0].pending == 1);
    saved = c;
    for (unsigned i = 0; i < 100000; ++i)
        assert(tcs_dl_poll(&c, i & 1, (struct tcs_dl_reading){30, true}) == TCS_DL_OK);
    assert(memcmp(&c, &saved, sizeof c) == 0);
    c = (struct tcs_deadline){0};
    assert(tcs_dl_poll(&c, 1, (struct tcs_dl_reading){0, true}) == TCS_DL_OK);
    assert(event(&c, 0, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true, 1).status == TCS_LC_DENIED);
    assert(!c.end[0]);
}

static void invalid_private_state(void)
{
    assert(!tcs_dl_valid(NULL) && !tcs_dl_allows(NULL, 0, 1));
    assert(tcs_dl_stop_mask(NULL) == 3);
    assert(tcs_dl_poll(NULL, 0, (struct tcs_dl_reading){0, true}) == TCS_DL_INVALID_STATE);
    assert(tcs_dl_apply(NULL, 0, (struct tcs_dl_reading){0, true}, TCS_LC_ADMIN,
        (struct tcs_lc_event){TCS_LC_RESERVE, 0, 0, 0}, true, 1).status == TCS_LC_INVALID);
    for (unsigned i = 0; i < 8; ++i) {
        struct tcs_deadline c = active();
        if (i == 0) c.end[0] = 0;
        if (i == 1) c.end[1] = 16;
        if (i == 2) c.last_tick = 15; /* Overdue without inhibit. */
        if (i == 3) c.clock_fault = 2;
        if (i == 4) c.clock_fault = 1; /* Clock fault without inhibit. */
        if (i == 5) c.control.life.slots[0].pending = 2;
        if (i == 6) c.control.inhibited = 1;
        if (i == 7) c.control.input_fault = 1;
        struct tcs_deadline saved = c;
        assert(!tcs_dl_valid(&c) && !tcs_dl_allows(&c, 0, 1) && tcs_dl_stop_mask(&c) == 3);
        assert(tcs_dl_poll(&c, 3, (struct tcs_dl_reading){100, true}) == TCS_DL_INVALID_STATE);
        assert(tcs_dl_apply(&c, 3, (struct tcs_dl_reading){100, true}, TCS_LC_ADMIN,
            (struct tcs_lc_event){TCS_LC_RESERVE, 1, 0, 0}, true, 1).status == TCS_LC_INVALID);
        assert(memcmp(&c, &saved, sizeof c) == 0);
    }
}

int main(void)
{
    expiry_and_evidence(); clock_faults_and_rejected_events(); admission_and_boundaries();
    floods_and_observation_boundary(); invalid_private_state();
    puts("PASS deadline model: one-use lifetime, equality expiry, sticky clock failure, checked arithmetic, retained stop/drain obligations");
    puts("PASS deadline model: 200000 repeated observations cannot renew or fabricate elapsed time; no hardware timing guarantee");
    return 0;
}
