#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TCS_TEST_IPC_ROUTER
#define TCS_TEST_FAULT_ROUTER
#include "isolation/observer.c"

static uint64_t canary;
static unsigned stopped, resumes, status_queries;
static size_t captured_length, tx_limit;
static char captured[8192];
static bool bad_status, bad_driver;

static void test_pd_stop(microkit_child child)
{
    assert(child >= 1 && child <= ISO_CASES);
    assert(!(stopped & (1u << (child - 1))));
    stopped |= 1u << (child - 1);
}
static void test_pd_resume(microkit_child child)
{
    assert(stopped == 63 && child == resumes + 1 && child <= ISO_CASES);
    assert(output_length == 0); /* Never launch the next probe behind blocked TX. */
    ++resumes;
}
static microkit_msginfo test_ppcall(microkit_channel ch, microkit_msginfo msg)
{
    if (ch == CLIENT_CHANNEL) {
        assert(tcs_message(msg, TCS_CLIENT_STATUS, 0)); ++status_queries;
        return tcs_snapshot_reply((struct tcs_snapshot){TCS_OK, TCS_RESTRICTED,
            bad_status ? 1 : 0, 0, 0}); /* Overwrites all four fault MRs. */
    }
    assert(ch == SERIAL_CHANNEL);
    if (tcs_message(msg, TCS_SERIAL_READ, 0)) {
        microkit_mr_set(0, 0); microkit_mr_set(1, 0);
        return microkit_msginfo_new(TCS_SERIAL_DATA, 2);
    }
    if (bad_driver) return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    size_t count = microkit_msginfo_get_count(msg);
    assert(microkit_msginfo_get_label(msg) == TCS_LABEL(TCS_SERIAL_WRITE));
    assert(count && count <= TCS_SERIAL_CHUNK);
    if (count > tx_limit) count = tx_limit;
    assert(captured_length + count < sizeof captured);
    for (size_t i = 0; i < count; ++i) captured[captured_length++] = (char)microkit_mr_get(i);
    captured[captured_length] = 0;
    return tcs_reply((struct tcs_result){TCS_OK, count});
}
static void reset(void)
{
    line = (struct tcs_line){0}; pending_event = TCS_LINE_NONE;
    output_length = output_sent = 0; failed = false;
    completed = 0; stage = START;
    stopped = resumes = status_queries = 0; captured_length = 0; captured[0] = 0;
    bad_status = bad_driver = false; tx_limit = 3;
    canary = ISO_CANARY; isolation_canary_vaddr = (uintptr_t)&canary;
}
static void drain(void)
{
    for (unsigned i = 0; i < 200; ++i) notified(SERIAL_CHANNEL);
    assert(!failed && output_length == 0);
}
static struct iso_fault sample(unsigned child)
{
    const struct iso_case *c = &iso_cases[child - 1];
    return (struct iso_fault){6, 4, c->execute ? c->address : 0x200004,
        c->address, c->execute, ((c->execute ? UINT64_C(0x20) : UINT64_C(0x24)) << 26) |
        (1u << 25) | ((uint64_t)c->write << 6) | (c->permission ? 15u : 6u)};
}
static void inject(unsigned child, struct iso_fault f)
{
    microkit_mr_set(0, f.ip); microkit_mr_set(1, f.address);
    microkit_mr_set(2, f.instruction); microkit_mr_set(3, f.fsr);
    microkit_msginfo reply = microkit_msginfo_new(0, 0);
    assert(fault(child, microkit_msginfo_new(f.label, f.count), &reply) == seL4_False);
}
int main(void)
{
    reset(); tx_limit = 0; init();
    assert(stopped == 63 && resumes == 0 && stage == START);
    tx_limit = 3; drain(); assert(resumes == 1 && stage == WAIT);
    tx_limit = 0; inject(1, sample(1));
    assert(resumes == 1 && completed == 1 && stage == NEXT);
    tx_limit = 3; drain(); assert(resumes == 2 && stage == WAIT);
    for (unsigned child = 2; child <= ISO_CASES; ++child) { inject(child, sample(child)); drain(); }
    assert(completed == 6 && resumes == 6 && stage == TERMINAL && status_queries == 7);
    assert(strstr(captured, "ip=2097156 address=150994968 instruction=0 fsr=2449473542"));
    assert(strstr(captured, "TCS ISOLATION PASS cases=6 canary=intact policy=restricted"));
    assert(strstr(captured, "TCS TERMINAL READY (host-test, read-only)"));
    for (unsigned variation = 0; variation < 8; ++variation) {
        reset(); init(); drain(); struct iso_fault f = sample(1); unsigned child = 1;
        switch (variation) {
        case 0: child = 0; break;
        case 1: child = 7; break;
        case 2: f.label = 0; break;
        case 3: f.count = 3; break;
        case 4: ++f.address; break;
        case 5: canary = 0; break;
        case 6: bad_status = true; break;
        case 7: f.fsr ^= 64; break;
        }
        inject(child, f); drain();
        assert(stage == FAILED && completed == 0 && resumes == 1);
        assert(strstr(captured, "TCS ISOLATION FAIL") && !strstr(captured, "TCS ISOLATION PASS"));
    }
    reset(); init(); drain(); inject(1, sample(1)); drain(); inject(1, sample(1)); drain();
    assert(stage == FAILED && completed == 1 && resumes == 2);
    reset(); canary = 0; init(); drain(); assert(stage == FAILED && resumes == 0);
    reset(); bad_driver = true; init(); assert(failed && resumes == 0);
    reset(); init(); drain();
    assert(tcs_response(protected(0, microkit_msginfo_new(6, 4))).status == TCS_DENIED);
    assert(completed == 0 && stage == WAIT); /* IPC payload cannot impersonate a fault. */
    puts("TCS ISOLATION OBSERVER TESTS PASS (ordering, backpressure, MR capture, wrong/duplicate faults, state tampering)");
}
