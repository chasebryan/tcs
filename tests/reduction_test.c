#include "tcs/reduction.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

static struct tcs_lc_result apply(struct tcs_reduction *c, enum tcs_lc_actor actor,
    uint64_t op, uint64_t slot, uint64_t inc, uint64_t seq)
{ return tcs_rd_apply(c, 0, actor, (struct tcs_lc_event){op, slot, inc, seq}, true); }

static struct tcs_reduction active(void)
{
    struct tcs_reduction c = {0};
    assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0).status == TCS_LC_OK);
    assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 0, 1, 0).status == TCS_LC_OK);
    assert(apply(&c, TCS_LC_ADMIN, TCS_LC_ACTIVATE, 0, 1, 0).status == TCS_LC_OK);
    assert(apply(&c, TCS_LC_BROKER, TCS_LC_BEGIN, 0, 1, 0).value == 1);
    return c;
}

static void early_requests(void)
{
    struct tcs_reduction c = {0};
    assert(tcs_rd_poll(&c, 1) == TCS_RD_OK);
    assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0).status == TCS_LC_DENIED);
    assert(c.life.last_incarnation == 0 && tcs_rd_stop_mask(&c) == 0);
    assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0).status == TCS_LC_OK);
    assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 1, 1, 0).status == TCS_LC_OK);
    assert(apply(&c, TCS_LC_ADMIN, TCS_LC_ACTIVATE, 1, 1, 0).status == TCS_LC_OK);
    assert(tcs_rd_allows(&c, 1, 1));
    assert(tcs_rd_poll(&c, 2) == TCS_RD_OK);
    assert(c.inhibited == 3 && tcs_rd_stop_mask(&c) == 2);
    assert(!tcs_rd_allows(&c, 1, 1));

    for (unsigned phase = TCS_LC_STARTING; phase <= TCS_LC_ACTIVE; ++phase) {
        c = (struct tcs_reduction){0};
        assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0).status == TCS_LC_OK);
        if (phase >= TCS_LC_READY)
            assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 0, 1, 0).status == TCS_LC_OK);
        if (phase == TCS_LC_ACTIVE)
            assert(apply(&c, TCS_LC_ADMIN, TCS_LC_ACTIVATE, 0, 1, 0).status == TCS_LC_OK);
        struct tcs_lc_result r = tcs_rd_apply(&c, 1, TCS_LC_ADMIN,
            (struct tcs_lc_event){TCS_LC_ACTIVATE, 0, 1, 0}, true);
        assert(r.status == TCS_LC_DENIED && r.value == 0);
        assert(c.life.slots[0].state == TCS_LC_CLOSING);
        assert(!c.life.slots[0].stopped && !c.life.slots[0].drained);
        assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 0, 1, 0).status == TCS_LC_BAD_STATE);
    }
}

static void pending_and_confirmations(void)
{
    for (unsigned first = 0; first < 2; ++first) {
        struct tcs_reduction c = active();
        assert(tcs_rd_poll(&c, 1) == TCS_RD_OK);
        assert(c.life.slots[0].pending == 1 && tcs_rd_stop_mask(&c) == 1);
        assert(apply(&c, TCS_LC_BROKER, TCS_LC_COMPLETE, 0, 1, 1).status == TCS_LC_BAD_STATE);
        assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0).status == TCS_LC_BAD_STATE);
        assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, 2, 1).status == TCS_LC_STALE);
        assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, 1, 0).status == TCS_LC_STALE);
        for (unsigned actor = TCS_LC_UNKNOWN; actor <= TCS_LC_WORKER; ++actor) {
            if (actor != TCS_LC_SUPERVISOR)
                assert(apply(&c, (enum tcs_lc_actor)actor, TCS_LC_STOPPED, 0, 1, 1).status == TCS_LC_DENIED);
            if (actor != TCS_LC_BROKER)
                assert(apply(&c, (enum tcs_lc_actor)actor, TCS_LC_DRAINED, 0, 1, 1).status == TCS_LC_DENIED);
        }
        for (unsigned pass = 0; pass < 2; ++pass) {
            bool stop = (pass == first);
            assert(apply(&c, stop ? TCS_LC_SUPERVISOR : TCS_LC_BROKER,
                stop ? TCS_LC_STOPPED : TCS_LC_DRAINED, 0, 1, 1).status == TCS_LC_OK);
            if (pass == 0) {
                assert(c.life.slots[0].state == TCS_LC_CLOSING);
                assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0).status == TCS_LC_BAD_STATE);
            }
        }
        assert(c.life.slots[0].state == TCS_LC_RETIRED && tcs_rd_stop_mask(&c) == 0);
        assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0).status == TCS_LC_DENIED);
        assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0).value == 2);
        assert(tcs_rd_poll(&c, 1) == TCS_RD_OK); /* Old bit does not target replacement. */
        assert(c.life.slots[1].state == TCS_LC_STARTING);
    }
}

static void malformed_and_rejected_events(void)
{
    for (unsigned bit = 2; bit < 64; ++bit) {
        struct tcs_reduction c = active();
        assert(tcs_rd_poll(&c, UINT64_C(1) << bit) == TCS_RD_MALFORMED);
        assert(c.inhibited == TCS_RD_ALL && c.input_fault == 1);
        assert(c.life.slots[0].state == TCS_LC_CLOSING && c.life.slots[0].pending == 1);
        assert(tcs_rd_stop_mask(&c) == 1);
        assert(tcs_rd_poll(&c, 0) == TCS_RD_OK && c.input_fault == 1);
        assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0).status == TCS_LC_DENIED);
        assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, 1, 1).status == TCS_LC_OK);
        assert(c.life.slots[0].state == TCS_LC_CLOSING && c.life.slots[0].pending == 1);
        assert(apply(&c, TCS_LC_BROKER, TCS_LC_DRAINED, 0, 1, 1).status == TCS_LC_OK);
        assert(c.life.slots[0].state == TCS_LC_RETIRED);
    }
    struct tcs_reduction c = active();
    struct tcs_lc_result r = tcs_rd_apply(&c, 1, TCS_LC_WORKER,
        (struct tcs_lc_event){UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX}, false);
    assert(r.status == TCS_LC_INVALID && r.value == 0);
    assert(c.inhibited == 1 && c.life.slots[0].state == TCS_LC_CLOSING);
    struct tcs_reduction saved = c;
    for (unsigned i = 0; i < 1000; ++i) assert(tcs_rd_poll(&c, i & 1) == TCS_RD_OK);
    assert(memcmp(&c, &saved, sizeof c) == 0);
    c = active();
    c.life.last_incarnation = c.life.slots[0].incarnation = UINT64_MAX;
    c.life.slots[0].issued = c.life.slots[0].pending = UINT64_MAX;
    assert(tcs_rd_poll(&c, 1) == TCS_RD_OK); /* Reductions do not consume counters. */
    assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, UINT64_MAX, UINT64_MAX).status == TCS_LC_OK);
    assert(apply(&c, TCS_LC_BROKER, TCS_LC_DRAINED, 0, UINT64_MAX, UINT64_MAX).status == TCS_LC_OK);
    assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0).status == TCS_LC_EXHAUSTED);
}

static void invalid_private_state(void)
{
    assert(!tcs_rd_valid(NULL) && tcs_rd_stop_mask(NULL) == TCS_RD_ALL);
    assert(!tcs_rd_allows(NULL, 0, 1));
    assert(tcs_rd_poll(NULL, 0) == TCS_RD_INVALID_STATE);
    for (unsigned i = 0; i < 5; ++i) {
        struct tcs_reduction c = active();
        if (i == 0) c.inhibited = 4;
        if (i == 1) c.input_fault = 2;
        if (i == 2) c.input_fault = 1;
        if (i == 3) c.inhibited = 1; /* Active despite already-observed inhibit. */
        if (i == 4) c.life.slots[0].pending = 2;
        struct tcs_reduction saved = c;
        assert(!tcs_rd_valid(&c) && !tcs_rd_allows(&c, 0, 1));
        assert(tcs_rd_stop_mask(&c) == TCS_RD_ALL);
        assert(tcs_rd_poll(&c, 3) == TCS_RD_INVALID_STATE);
        assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0).status == TCS_LC_INVALID);
        assert(memcmp(&c, &saved, sizeof c) == 0);
    }
}

static void mailbox_and_observation_boundary(void)
{
    struct tcs_reduction_mailbox m = {0};
    assert(!tcs_rd_publish(NULL, 1) && tcs_rd_sample(NULL) == UINT64_MAX);
    assert(!tcs_rd_publish(&m, UINT64_MAX) && tcs_rd_sample(&m) == 0);
    assert(tcs_rd_publish(&m, 1) && tcs_rd_publish(&m, 2) && tcs_rd_publish(&m, 0));
    assert(tcs_rd_sample(&m) == 3); /* Coalesced wakeup needs only one sample. */
    struct tcs_reduction c = active();
    assert(tcs_rd_poll(&c, tcs_rd_sample(&m)) == TCS_RD_OK);
    /* Hostile clearing cannot clear PRIVATE bits already observed. */
    atomic_store_explicit(&m.requests, 0, memory_order_release);
    assert(tcs_rd_poll(&c, tcs_rd_sample(&m)) == TCS_RD_OK && c.inhibited == 3);
    atomic_store_explicit(&m.requests, 4, memory_order_release);
    assert(tcs_rd_poll(&c, tcs_rd_sample(&m)) == TCS_RD_MALFORMED);

    struct tcs_reduction_mailbox later = {0};
    c = (struct tcs_reduction){0};
    assert(apply(&c, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0).status == TCS_LC_OK);
    assert(apply(&c, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 0, 1, 0).status == TCS_LC_OK);
    uint64_t old_sample = tcs_rd_sample(&later);
    assert(tcs_rd_publish(&later, 1));
    /* Publication AFTER a sample does not retroactively change that sample. */
    assert(tcs_rd_apply(&c, old_sample, TCS_LC_ADMIN,
        (struct tcs_lc_event){TCS_LC_ACTIVATE, 0, 1, 0}, true).status == TCS_LC_OK);
    assert(tcs_rd_allows(&c, 0, 1));
    assert(tcs_rd_poll(&c, tcs_rd_sample(&later)) == TCS_RD_OK);
    assert(!tcs_rd_allows(&c, 0, 1) && tcs_rd_stop_mask(&c) == 1);
}

static struct tcs_reduction_mailbox concurrent;
static atomic_bool start;
static void *writer(void *argument)
{
    uint64_t bit = *(const uint64_t *)argument;
    while (!atomic_load_explicit(&start, memory_order_acquire)) sched_yield();
    for (unsigned i = 0; i < 50000; ++i) assert(tcs_rd_publish(&concurrent, bit));
    return NULL;
}
static void concurrent_publishers(void)
{
    pthread_t writers[2]; const uint64_t bits[2] = {1, 2};
    assert(pthread_create(&writers[0], NULL, writer, (void *)&bits[0]) == 0);
    assert(pthread_create(&writers[1], NULL, writer, (void *)&bits[1]) == 0);
    atomic_store_explicit(&start, true, memory_order_release);
    struct tcs_reduction c = active(); uint64_t previous = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        uint64_t sample = tcs_rd_sample(&concurrent);
        assert((sample & previous) == previous && sample <= TCS_RD_ALL);
        assert(tcs_rd_poll(&c, sample) == TCS_RD_OK && tcs_rd_valid(&c));
        previous = sample;
    }
    assert(pthread_join(writers[0], NULL) == 0 && pthread_join(writers[1], NULL) == 0);
    assert(tcs_rd_poll(&c, tcs_rd_sample(&concurrent)) == TCS_RD_OK);
    assert(c.inhibited == 3 && c.life.slots[0].pending == 1 && tcs_rd_stop_mask(&c) == 1);
}

int main(void)
{
    early_requests(); pending_and_confirmations(); malformed_and_rejected_events();
    invalid_private_state(); mailbox_and_observation_boundary(); concurrent_publishers();
    puts("PASS reduction latch: pre-selection inhibit, observation-before-admission, sticky fault, no fabricated stop/drain");
    puts("PASS reduction mailbox: 100000 atomic publications, 100000 samples; publication is not containment acknowledgement");
    return 0;
}
