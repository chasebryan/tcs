#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define init sv_init
#define protected sv_protected
#define notified sv_notified
#define fault sv_fault
#include "lifecycle/supervisor.c"
#undef init
#undef protected
#undef notified
#undef fault
#define init br_init
#define protected br_protected
#define notified br_notified
#include "lifecycle/broker.c"
#undef init
#undef protected
#undef notified
#define init ct_init
#define protected ct_protected
#define notified ct_notified
#include "lifecycle/controller.c"
#undef init
#undef protected
#undef notified

enum domain { CONTROLLER, BROKER, SUPERVISOR };
static enum domain current;
static unsigned stop_calls[2], resume_calls[2], wakeups[2], corrupt_reply;
static unsigned fail_stop, fail_resume;
static jmp_buf kernel_failure;
static _Atomic(uint64_t) counter_a, counter_b;
static uint8_t rx_bytes[256], rx_flags[256];
static size_t rx_count, rx_at, tx_limit, tx_count;
static char tx_bytes[8192];

static microkit_msginfo call(enum domain target, unsigned ch, microkit_msginfo msg)
{
    enum domain before = current; current = target;
    microkit_msginfo r = target == SUPERVISOR ? sv_protected(ch, msg) : br_protected(ch, msg);
    current = before;
    return r;
}
static microkit_msginfo lc_route(microkit_channel ch, microkit_msginfo msg)
{
    if (current == BROKER) {
        assert(ch == 3);
        microkit_msginfo r = call(SUPERVISOR, 1, msg);
        if (corrupt_reply) {
            unsigned kind = corrupt_reply; corrupt_reply = 0;
            if (kind == 1) r.count = 64;
            if (kind == 2) r.label = 0;
            if (kind == 3) lc_mrs[1] = 0;
        }
        return r;
    }
    assert(current == CONTROLLER); /* Supervisor must never make a protected call. */
    if (ch == 1) return call(SUPERVISOR, 0, msg);
    if (ch == 2) return call(BROKER, 2, msg);
    assert(ch == 0);
    if (tcs_message(msg, TCS_SERIAL_READ, 0)) {
        microkit_mr_set(0, rx_at < rx_count ? rx_flags[rx_at] : 0);
        microkit_mr_set(1, rx_at < rx_count ? rx_bytes[rx_at] : 0);
        if (rx_at < rx_count) ++rx_at;
        return microkit_msginfo_new(TCS_SERIAL_DATA, 2);
    }
    assert(msg.label == TCS_LABEL(TCS_SERIAL_WRITE) && msg.count <= 32);
    size_t n = msg.count < tx_limit ? (size_t)msg.count : tx_limit;
    for (size_t i = 0; i < n; ++i) { assert(tx_count < sizeof tx_bytes); tx_bytes[tx_count++] = (char)lc_mrs[i]; }
    return tcs_reply((struct tcs_result){TCS_OK, n});
}
static void lc_notify(microkit_channel ch) { assert(current == BROKER && ch < 2); ++wakeups[ch]; }
static void lc_stop(microkit_child child)
{
    assert(current == SUPERVISOR && child >= 1 && child <= 2);
    ++stop_calls[child - 1];
    if (fail_stop == child) longjmp(kernel_failure, 1);
}
static void lc_resume(microkit_child child)
{
    assert(current == SUPERVISOR && child >= 1 && child <= 2);
    assert(sv_life.slots[child - 1].state == TCS_LC_STARTING);
    ++resume_calls[child - 1];
    if (fail_resume == child) longjmp(kernel_failure, 1);
}
static void reset(void)
{
    sv_life = (struct tcs_lifecycle){0}; sv_ready = sv_failed = false;
    sv_faults = sv_ip = sv_address = sv_fsr = 0;
    br_ready = br_failed = false;
    memset(br_inc, 0, sizeof br_inc); memset(br_pending, 0, sizeof br_pending);
    br_bad = br_stale = br_begun = br_drains = 0;
    memset(lc_mrs, 0, sizeof lc_mrs); memset(stop_calls, 0, sizeof stop_calls);
    memset(resume_calls, 0, sizeof resume_calls); memset(wakeups, 0, sizeof wakeups);
    corrupt_reply = fail_stop = fail_resume = 0;
    ct_line = (struct tcs_line){0}; ct_event = TCS_LINE_NONE;
    ct_length = ct_sent = ct_step = 0; ct_failed = false;
    rx_count = rx_at = tx_count = 0; tx_limit = 32;
    atomic_store(&counter_a, 0); atomic_store(&counter_b, 0);
    counter_a_vaddr = (uintptr_t)&counter_a; counter_b_vaddr = (uintptr_t)&counter_b;
    current = SUPERVISOR; sv_init(); current = BROKER; br_init(); current = CONTROLLER;
}
static struct tcs_lc_result control(uint64_t op, uint64_t slot)
{
    lc_mrs[0] = op; lc_mrs[1] = slot;
    return lt_response(call(SUPERVISOR, 0, microkit_msginfo_new(LT_CONTROL, 2)));
}
static struct tcs_lc_result worker(unsigned slot, uint64_t label, unsigned count, uint64_t a, uint64_t b)
{
    lc_mrs[0] = a; lc_mrs[1] = b;
    return lt_response(call(BROKER, slot, microkit_msginfo_new(label, count)));
}
static struct tcs_lc_result broker_control(uint64_t label, uint64_t slot)
{
    lc_mrs[0] = slot;
    return lt_response(call(BROKER, 2, microkit_msginfo_new(label, 1)));
}
static uint64_t start(unsigned slot)
{
    assert(control(TCS_LC_RESERVE, slot).status == TCS_LC_OK);
    assert(resume_calls[slot] == 1);
    assert(worker(slot, LT_HELLO, 0, 0, 0).status == TCS_LC_OK);
    assert(sv_life.slots[slot].state == TCS_LC_READY);
    assert(control(TCS_LC_ACTIVATE, slot).status == TCS_LC_OK);
    return br_inc[slot];
}
static void transitions(void)
{
    reset(); uint64_t inc = start(0);
    assert(broker_control(LT_KICK, 0).status == TCS_LC_OK && wakeups[0] == 1);
    assert(worker(0, LT_BEGIN, 1, inc, 0).value == 1);
    assert(broker_control(LT_DRAIN, 0).status == TCS_LC_BAD_STATE);
    assert(control(TCS_LC_CONTAIN, 0).status == TCS_LC_OK);
    assert(stop_calls[0] == 2 && sv_life.slots[0].stopped && !sv_life.slots[0].drained);
    assert(worker(0, LT_COMPLETE, 2, inc, 1).status == TCS_LC_BAD_STATE);
    assert(worker(0, LT_BEGIN, 1, inc, 0).status == TCS_LC_BAD_STATE);
    assert(control(TCS_LC_RESERVE, 1).status == TCS_LC_BAD_STATE);
    assert(broker_control(LT_DRAIN, 0).status == TCS_LC_OK);
    assert(br_pending[0] == 0 && sv_life.slots[0].state == TCS_LC_RETIRED);
    assert(broker_control(LT_DRAIN, 0).status == TCS_LC_BAD_STATE);
    assert(start(1) == 2);
    assert(worker(1, LT_COMPLETE, 2, 1, 1).status == TCS_LC_STALE);
    assert(worker(1, LT_BEGIN, 1, 1, 0).status == TCS_LC_STALE);
    assert(worker(1, LT_HELLO, 2, 0, 1).status == TCS_LC_INVALID);
    assert(worker(1, LT_BEGIN, 1, 2, 0).value == 1);
    assert(br_bad == 1 && br_stale == 2 && br_begun == 2);
    puts("PASS real native supervisor/broker adapters: gate linearization, stop before drain, kernel-channel identity, stale work, no supervisor RPC");
}
static void failures(void)
{
    reset(); start(0);
    fail_stop = 1;
    if (setjmp(kernel_failure) == 0) { (void)control(TCS_LC_CONTAIN, 0); assert(0); }
    assert(sv_life.slots[0].state == TCS_LC_CLOSING && !sv_life.slots[0].stopped);
    current = CONTROLLER;
    assert(broker_control(LT_DRAIN, 0).status == TCS_LC_BAD_STATE);
    assert(control(TCS_LC_RESERVE, 1).status == TCS_LC_BAD_STATE);
    reset(); fail_resume = 1;
    if (setjmp(kernel_failure) == 0) { (void)control(TCS_LC_RESERVE, 0); assert(0); }
    assert(sv_life.slots[0].state == TCS_LC_STARTING && !sv_life.slots[0].stopped);
    current = CONTROLLER; fail_resume = 0;
    assert(control(TCS_LC_CONTAIN, 0).status == TCS_LC_OK);
    assert(worker(0, LT_HELLO, 0, 0, 0).status == TCS_LC_BAD_STATE);
    for (unsigned kind = 1; kind <= 3; ++kind) {
        reset(); start(0); corrupt_reply = kind;
        assert(worker(0, LT_BEGIN, 1, 1, 0).status == LT_FAILURE);
        assert(br_failed && sv_life.slots[0].pending == 1);
        assert(worker(0, LT_BEGIN, 1, 1, 0).status == LT_FAILURE);
        /* Supervisor remains callable even when broker refuses further work. */
        assert(control(TCS_LC_CONTAIN, 0).status == TCS_LC_OK);
        assert(sv_life.slots[0].stopped && !sv_life.slots[0].drained);
        assert(control(TCS_LC_RESERVE, 1).status == TCS_LC_BAD_STATE);
    }
    puts("PASS nonreturning kernel failures and malformed post-commit replies never certify stop, clear uncertainty, or enable replacement");
}
static void faults(void)
{
    for (unsigned mutation = 0; mutation < 9; ++mutation) {
        reset(); start(0); control(TCS_LC_CONTAIN, 0); broker_control(LT_DRAIN, 0);
        start(1); worker(1, LT_BEGIN, 1, 2, 0);
        uint64_t ip = 0x200400, address = LT_FAULT_ADDRESS, instruction = 0, fsr = UINT64_C(0x92000046);
        unsigned child = 2; microkit_msginfo m = {6, 4}, reply = {123, 456};
        if (mutation == 1) child = 1;
        if (mutation == 2) m.label = 5;
        if (mutation == 3) m.count = 3;
        if (mutation == 4) address++;
        if (mutation == 5) instruction = 1;
        if (mutation == 6) fsr ^= 1u << 6;
        if (mutation == 7) ip++;
        if (mutation == 8) fsr |= UINT64_C(1) << 40;
        lc_mrs[0] = ip; lc_mrs[1] = address; lc_mrs[2] = instruction; lc_mrs[3] = fsr;
        current = SUPERVISOR;
        assert(sv_fault(child, m, &reply) == seL4_False && reply.label == 123 && reply.count == 456);
        assert(sv_failed == (mutation != 0));
        if (!mutation) {
            assert(sv_faults == 1 && sv_address == address && sv_fsr == fsr);
            assert(sv_life.slots[1].stopped && !sv_life.slots[1].drained);
            assert(sv_fault(child, m, &reply) == seL4_False && sv_failed);
        }
    }
    reset(); current = SUPERVISOR;
    microkit_msginfo reply = {0};
    assert(sv_fault(2, (microkit_msginfo){6, 4}, &reply) == seL4_False && sv_failed);
    puts("PASS exact child fault identity/shape/address/syndrome; duplicate, unknown and unused-child faults disable supervisor");
}
static void malformed(void)
{
    for (unsigned n = 0; n <= 64; ++n) {
        reset(); struct tcs_lifecycle before = sv_life;
        if (n != 2) assert(lt_response(call(SUPERVISOR, 0, (microkit_msginfo){LT_CONTROL, n})).status != TCS_LC_OK);
        if (n != 4) assert(lt_response(call(SUPERVISOR, 1, (microkit_msginfo){LT_EVENT, n})).status != TCS_LC_OK);
        assert(memcmp(&before, &sv_life, sizeof before) == 0 && !resume_calls[0]);
    }
    reset(); start(0);
    for (uint64_t op = TCS_LC_RESERVE; op <= TCS_LC_DRAINED; ++op) {
        if (op == TCS_LC_BEGIN || op == TCS_LC_COMPLETE || op == TCS_LC_DRAINED) continue;
        lc_mrs[0] = op; lc_mrs[1] = 0; lc_mrs[2] = 1; lc_mrs[3] = 0;
        assert(lt_response(call(SUPERVISOR, 1, (microkit_msginfo){LT_EVENT, 4})).status == TCS_LC_DENIED);
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        assert(worker(ch, LT_DRAIN, 1, 0, 0).status == TCS_LC_INVALID);
        assert(worker(ch, LT_CONTROL, 2, TCS_LC_ACTIVATE, 0).status == TCS_LC_INVALID);
        assert(worker(ch, LT_BROKER_STATUS, 0, 0, 0).status == TCS_LC_INVALID);
    }
    assert(lt_response(call(SUPERVISOR, 2, (microkit_msginfo){LT_CONTROL, 2})).status == LT_FAILURE);
    assert(control(TCS_LC_RESERVE, UINT64_MAX).status == TCS_LC_INVALID);
    puts("PASS exact control/bridge shapes, unknown channels, slot bounds and worker inability to select control authority");
}
static void drive(void)
{
    current = CONTROLLER;
    for (unsigned i = 0; i < 2000 && (rx_at < rx_count || ct_length); ++i) ct_notified(0);
    assert(rx_at == rx_count && ct_length == 0);
}
static void input(const char *s)
{
    rx_count = strlen(s); rx_at = 0;
    memcpy(rx_bytes, s, rx_count); memset(rx_flags, TCS_SERIAL_BYTE, rx_count);
}
static void controller(void)
{
    reset(); ct_init(); drive();
    input("nextx\n"); drive(); assert(ct_step == 0 && !resume_calls[0]);
    input("next\003"); drive(); assert(ct_step == 0 && !resume_calls[0]);
    input("next\n"); rx_flags[4] |= TCS_SERIAL_LOSS; drive(); assert(ct_step == 0 && !resume_calls[0]);
    input("next\n"); tx_limit = 0; ct_notified(0);
    assert(ct_step == 0 && !resume_calls[0]);
    tx_limit = 1; drive(); assert(ct_step == 1 && resume_calls[0] == 1 && !ct_failed);
    puts("PASS test controller: cancellation/loss/unknown commands have no effects; partial echo completes before fixture control");
}
int main(void)
{
    transitions(); failures(); faults(); malformed(); controller();
    puts("TCS LIFECYCLE RUNTIME ADAPTER TESTS PASS (mocked kernel, actual adapters)");
    return 0;
}
