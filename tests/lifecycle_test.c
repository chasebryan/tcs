#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tcs/lifecycle.h"

static struct tcs_lc_result step(struct tcs_lifecycle *p, enum tcs_lc_actor actor,
    uint64_t op, uint64_t slot, uint64_t incarnation, uint64_t sequence, bool audit)
{
    struct tcs_lifecycle before = *p;
    struct tcs_lc_result r = tcs_lc_apply(p, actor,
        (struct tcs_lc_event){op, slot, incarnation, sequence}, audit);
    if (r.status != TCS_LC_OK) {
        assert(r.value == 0);
        assert(memcmp(&before, p, sizeof before) == 0);
    } else assert(tcs_lc_valid(p));
    return r;
}

static uint64_t active(struct tcs_lifecycle *p, uint64_t slot)
{
    struct tcs_lc_result r = step(p, TCS_LC_ADMIN, TCS_LC_RESERVE, slot, 0, 0, true);
    assert(r.status == TCS_LC_OK);
    uint64_t inc = r.value;
    assert(!tcs_lc_allows(p, slot, inc));
    assert(step(p, TCS_LC_SUPERVISOR, TCS_LC_STARTED, slot, inc, 0, false).status == TCS_LC_OK);
    assert(!tcs_lc_allows(p, slot, inc));
    assert(step(p, TCS_LC_ADMIN, TCS_LC_ACTIVATE, slot, inc, 0, false).status == TCS_LC_AUDIT_REQUIRED);
    assert(step(p, TCS_LC_ADMIN, TCS_LC_ACTIVATE, slot, inc, 0, true).status == TCS_LC_OK);
    assert(tcs_lc_allows(p, slot, inc));
    return inc;
}

static void replacement(void)
{
    for (unsigned order = 0; order < 2; ++order) {
        struct tcs_lifecycle p = {0};
        assert(step(&p, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, false).status == TCS_LC_AUDIT_REQUIRED);
        uint64_t inc = active(&p, 0);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_BEGIN, 0, inc, 0, false).value == 1);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_COMPLETE, 0, inc, 1, false).status == TCS_LC_OK);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_BEGIN, 0, inc, 0, false).value == 2);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_COMPLETE, 0, inc, 1, false).status == TCS_LC_STALE);
        assert(step(&p, TCS_LC_DETECTOR, TCS_LC_CONTAIN, 0, inc, 0, false).status == TCS_LC_OK);
        assert(!tcs_lc_allows(&p, 0, inc) && p.slots[0].pending == 2);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_COMPLETE, 0, inc, 2, true).status == TCS_LC_BAD_STATE);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_DRAINED, 0, inc, 1, false).status == TCS_LC_STALE);
        assert(step(&p, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0, true).status == TCS_LC_BAD_STATE);
        uint64_t first = order ? TCS_LC_DRAINED : TCS_LC_STOPPED;
        uint64_t second = order ? TCS_LC_STOPPED : TCS_LC_DRAINED;
        enum tcs_lc_actor a = order ? TCS_LC_BROKER : TCS_LC_SUPERVISOR;
        enum tcs_lc_actor b = order ? TCS_LC_SUPERVISOR : TCS_LC_BROKER;
        assert(step(&p, a, first, 0, inc, 2, false).status == TCS_LC_OK);
        assert(step(&p, a, first, 0, inc, 2, false).status == TCS_LC_BAD_STATE);
        assert(step(&p, TCS_LC_DETECTOR, TCS_LC_CONTAIN, 0, inc, 0, false).status == TCS_LC_BAD_STATE);
        assert(step(&p, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0, true).status == TCS_LC_BAD_STATE);
        assert(step(&p, b, second, 0, inc, 2, false).status == TCS_LC_OK);
        assert(p.slots[0].state == TCS_LC_RETIRED && p.slots[0].pending == 0);
        uint64_t next = active(&p, 1);
        assert(next > inc && !tcs_lc_allows(&p, 0, inc) && !tcs_lc_allows(&p, 1, inc));
        assert(step(&p, TCS_LC_BROKER, TCS_LC_BEGIN, 1, next, 0, true).value == 1);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_COMPLETE, 1, inc, 1, true).status == TCS_LC_STALE);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_COMPLETE, 0, inc, 2, true).status == TCS_LC_BAD_STATE);
        assert(step(&p, TCS_LC_ADMIN, TCS_LC_CONTAIN, 1, next, 0, false).status == TCS_LC_OK);
        assert(step(&p, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 1, next, 1, false).status == TCS_LC_OK);
        assert(step(&p, TCS_LC_BROKER, TCS_LC_DRAINED, 1, next, 1, false).status == TCS_LC_OK);
        for (unsigned slot = 0; slot < TCS_LC_SLOTS; ++slot)
            assert(step(&p, TCS_LC_ADMIN, TCS_LC_RESERVE, slot, 0, 0, true).status == TCS_LC_BAD_STATE);
    }
    puts("PASS lifecycle replacement: separate audited activation, two confirmation orders, pending work, stale results, finite one-use pool");
}

static void start_failure(void)
{
    struct tcs_lifecycle p = {0};
    assert(step(&p, TCS_LC_ADMIN, TCS_LC_RESERVE, 0, 0, 0, true).status == TCS_LC_OK);
    assert(step(&p, TCS_LC_SUPERVISOR, TCS_LC_CONTAIN, 0, 1, 0, false).status == TCS_LC_OK);
    assert(step(&p, TCS_LC_SUPERVISOR, TCS_LC_STARTED, 0, 1, 0, true).status == TCS_LC_BAD_STATE);
    assert(step(&p, TCS_LC_ADMIN, TCS_LC_ACTIVATE, 0, 1, 0, true).status == TCS_LC_BAD_STATE);
    assert(step(&p, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, 1, 1, true).status == TCS_LC_STALE);
    assert(step(&p, TCS_LC_BROKER, TCS_LC_DRAINED, 0, 1, 0, false).status == TCS_LC_OK);
    /* Missing/failed stop is not success: no replacement may be selected. */
    for (unsigned n = 0; n < 100; ++n)
        assert(step(&p, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0, true).status == TCS_LC_BAD_STATE);
    assert(step(&p, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, 1, 0, false).status == TCS_LC_OK);
    assert(active(&p, 1) == 2);
    puts("PASS containment during start: delayed start rejected, failed/missing stop never treated as completion");
}

static void exhaustion(void)
{
    struct tcs_lifecycle p = {0};
    /* Synthetic structurally valid boundary state, not reachable in two fresh slots. */
    p.last_incarnation = UINT64_MAX - 1;
    uint64_t inc = active(&p, 0);
    assert(inc == UINT64_MAX);
    p.slots[0].issued = UINT64_MAX - 1;
    assert(step(&p, TCS_LC_BROKER, TCS_LC_BEGIN, 0, inc, 0, true).value == UINT64_MAX);
    assert(step(&p, TCS_LC_BROKER, TCS_LC_COMPLETE, 0, inc, UINT64_MAX, true).status == TCS_LC_OK);
    assert(step(&p, TCS_LC_BROKER, TCS_LC_BEGIN, 0, inc, 0, true).status == TCS_LC_EXHAUSTED);
    assert(step(&p, TCS_LC_ADMIN, TCS_LC_CONTAIN, 0, inc, 0, false).status == TCS_LC_OK);
    assert(step(&p, TCS_LC_SUPERVISOR, TCS_LC_STOPPED, 0, inc, UINT64_MAX, false).status == TCS_LC_OK);
    assert(step(&p, TCS_LC_BROKER, TCS_LC_DRAINED, 0, inc, UINT64_MAX, false).status == TCS_LC_OK);
    assert(step(&p, TCS_LC_ADMIN, TCS_LC_RESERVE, 1, 0, 0, true).status == TCS_LC_EXHAUSTED);
    puts("PASS nonwrapping incarnation/request exhaustion; authority reduction remains possible");
}

static void invalid(void)
{
    struct tcs_lifecycle p = {0};
    uint64_t inc = active(&p, 0);
    for (uint64_t op = TCS_LC_RESERVE; op <= TCS_LC_DRAINED; ++op) {
        assert(step(&p, TCS_LC_WORKER, op, 0, inc, 0, true).status == TCS_LC_DENIED);
        assert(step(&p, TCS_LC_UNKNOWN, op, 0, inc, 0, true).status == TCS_LC_DENIED);
        assert(step(&p, (enum tcs_lc_actor)-1, op, 0, inc, 0, true).status == TCS_LC_DENIED);
    }
    assert(step(&p, TCS_LC_ADMIN, 0, 0, inc, 0, true).status == TCS_LC_INVALID);
    assert(step(&p, TCS_LC_ADMIN, UINT64_MAX, 0, inc, 0, true).status == TCS_LC_INVALID);
    assert(step(&p, TCS_LC_ADMIN, TCS_LC_CONTAIN, UINT64_MAX, inc, 0, true).status == TCS_LC_INVALID);
    assert(step(&p, TCS_LC_ADMIN, TCS_LC_CONTAIN, 0, inc, 1, true).status == TCS_LC_INVALID);
    assert(!tcs_lc_valid(NULL) && !tcs_lc_allows(NULL, 0, 1));
    assert(!tcs_lc_allows(&p, UINT64_MAX, 1) && !tcs_lc_allows(&p, 0, 0));
    assert(tcs_lc_apply(NULL, TCS_LC_ADMIN, (struct tcs_lc_event){0}, true).status == TCS_LC_INVALID);
    for (unsigned field = 0; field < 12; ++field) {
        struct tcs_lifecycle bad = p;
        switch (field) {
        case 0: bad.slots[0].state = UINT64_MAX; break;
        case 1: bad.slots[0].incarnation = 0; break;
        case 2: bad.last_incarnation = 0; break;
        case 3: bad.slots[0].stopped = 2; break;
        case 4: bad.slots[0].drained = 2; break;
        case 5: bad.slots[0].pending = 2; break;
        case 6: bad.slots[0].stopped = 1; break;
        case 7: bad.slots[1].issued = 1; break;
        case 8: bad.slots[1] = bad.slots[0]; break;
        case 9: bad.slots[1] = bad.slots[0]; bad.slots[1].incarnation = ++bad.last_incarnation; break;
        case 10: bad.slots[0].state = TCS_LC_RETIRED; break;
        case 11: bad.slots[0].state = TCS_LC_CLOSING; bad.slots[0].stopped = bad.slots[0].drained = 1; break;
        }
        assert(!tcs_lc_valid(&bad) && !tcs_lc_allows(&bad, 0, inc));
        assert(step(&bad, TCS_LC_ADMIN, TCS_LC_CONTAIN, 0, inc, 0, true).status == TCS_LC_INVALID);
    }
    puts("PASS unknown/worker identities, malformed events and inconsistent state fail closed without mutation");
}

int main(void)
{
    replacement(); start_failure(); exhaustion(); invalid();
    puts("TCS LIFECYCLE MODEL TESTS PASS (no guest supervisor or kernel effects)");
    return 0;
}
