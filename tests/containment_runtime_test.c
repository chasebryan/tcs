#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define init sp_init
#define protected sp_protected
#define notified sp_notified
#define fault sp_fault
#include "containment/supervisor.c"
#undef init
#undef protected
#undef notified
#undef fault
#define init br_init
#define protected br_protected
#define notified br_notified
#include "containment/broker.c"
#undef init
#undef protected
#undef notified
#define init cl_init
#define protected cl_protected
#define notified cl_notified
#include "containment/caller.c"
#undef init
#undef protected
#undef notified
#define init wk_init
#define notified wk_notified
#include "containment/worker.c"
#undef init
#undef notified
#define init ob_init
#define protected ob_protected
#define notified ob_notified
#include "containment/observer.c"
#undef init
#undef protected
#undef notified

enum domain { SUPERVISOR, BROKER, CALLER, WORKER, OBSERVER };
static enum domain current;
static struct tcs_reduction_mailbox mailbox;
static _Atomic(uint64_t) receipt, worker_a, worker_b, broker_progress, caller_progress;
static unsigned stops[2], resumes[2], kicks, caller_wakes, supervisor_calls;
static unsigned fail_stop, fail_resume, corrupt_bridge, spin_count;
static bool deliver_notification, running[2];
static jmp_buf trap;
static uint8_t rx_bytes[256], rx_flags[256];
static size_t rx_count, rx_at, tx_count, tx_limit;
static char tx_bytes[8192];

static microkit_msginfo call(enum domain target, unsigned ch, microkit_msginfo m)
{
    enum domain previous = current; current = target;
    if (target == SUPERVISOR) ++supervisor_calls;
    microkit_msginfo reply = target == SUPERVISOR ? sp_protected(ch, m) : br_protected(ch, m);
    current = previous; return reply;
}
static microkit_msginfo lc_route(microkit_channel ch, microkit_msginfo m)
{
    if (current == BROKER) {
        assert(ch == 3);
        microkit_msginfo r = call(SUPERVISOR, 1, m);
        if (corrupt_bridge) {
            unsigned kind = corrupt_bridge; corrupt_bridge = 0;
            if (kind == 1) r.count = 63;
            if (kind == 2) r.label = 0;
            if (kind == 3) lc_mrs[1] = 0;
        }
        return r;
    }
    if (current == CALLER) { assert(ch == 1); return call(BROKER, 2, m); }
    if (current == WORKER) { assert(ch == 0); return call(BROKER, 0, m); }
    assert(current == OBSERVER); /* Supervisor has NO outgoing protected calls. */
    if (ch == 1) return call(SUPERVISOR, 0, m);
    assert(ch == 0); /* Observer has NO broker call, even while waiting. */
    if (tcs_message(m, TCS_SERIAL_READ, 0)) {
        lc_mrs[0] = rx_at < rx_count ? rx_flags[rx_at] : 0;
        lc_mrs[1] = rx_at < rx_count ? rx_bytes[rx_at] : 0;
        if (rx_at < rx_count) ++rx_at;
        return microkit_msginfo_new(TCS_SERIAL_DATA, 2);
    }
    assert(m.label == TCS_LABEL(TCS_SERIAL_WRITE) && m.count <= 32);
    size_t n = m.count < tx_limit ? (size_t)m.count : tx_limit;
    for (size_t i = 0; i < n; ++i) { assert(tx_count < sizeof tx_bytes); tx_bytes[tx_count++] = (char)lc_mrs[i]; }
    return tcs_reply((struct tcs_result){TCS_OK, n});
}
static void lc_notify(microkit_channel ch)
{
    if (current == BROKER) { assert(ch == 0); ++kicks; return; }
    assert(current == OBSERVER);
    if (ch == 2) { ++caller_wakes; return; }
    assert(ch == 1);
    if (deliver_notification) { current = SUPERVISOR; sp_notified(0); current = OBSERVER; }
}
static void clobber(void) { for (unsigned i = 0; i < 64; ++i) lc_mrs[i] = UINT64_MAX; }
static void lc_stop(microkit_child child)
{
    assert(current == SUPERVISOR && child >= 1 && child <= 2);
    ++stops[child - 1];
    assert(sp_failed || !sp_ready || sp_control.life.slots[child - 1].state == TCS_LC_CLOSING);
    if (fail_stop == child) longjmp(trap, 1);
    running[child - 1] = false; clobber();
}
static void lc_resume(microkit_child child)
{
    assert(current == SUPERVISOR && child >= 1 && child <= 2);
    assert(sp_control.life.slots[child - 1].state == TCS_LC_STARTING);
    ++resumes[child - 1];
    if (fail_resume == child) longjmp(trap, 2);
    running[child - 1] = true; clobber();
}
_Noreturn void ct_host_spin(uintptr_t address)
{
    assert((current == BROKER && address == (uintptr_t)&broker_progress) ||
        (current == WORKER && address == (uintptr_t)&worker_a));
    ++spin_count; ct_store(address, 1); longjmp(trap, 3);
}
static void reset(void)
{
    sp_control = (struct tcs_reduction){0}; sp_ready = sp_failed = false;
    br_ready = br_failed = false; memset(br_inc, 0, sizeof br_inc); memset(br_pending, 0, sizeof br_pending);
    cl_phase = 0; wk_inc = 0; wk_started = false;
    ob_line = (struct tcs_line){0}; ob_event = TCS_LINE_NONE;
    ob_length = ob_sent = ob_step = ob_probes = 0; ob_failed = false;
    memset(stops, 0, sizeof stops); memset(resumes, 0, sizeof resumes); memset(running, 0, sizeof running);
    kicks = caller_wakes = supervisor_calls = fail_stop = fail_resume = corrupt_bridge = spin_count = 0;
    deliver_notification = true; rx_count = rx_at = tx_count = 0; tx_limit = 32;
    atomic_store(&mailbox.requests, 0); atomic_store(&receipt, 0);
    atomic_store(&worker_a, 0); atomic_store(&worker_b, 0); atomic_store(&broker_progress, 0); atomic_store(&caller_progress, 0);
    request_vaddr = observer_request_vaddr = (uintptr_t)&mailbox;
    receipt_vaddr = observer_receipt_vaddr = (uintptr_t)&receipt;
    worker_counter_vaddr = observer_worker_a_vaddr = (uintptr_t)&worker_a;
    observer_worker_b_vaddr = (uintptr_t)&worker_b;
    broker_counter_vaddr = observer_broker_vaddr = (uintptr_t)&broker_progress;
    caller_phase_vaddr = observer_caller_vaddr = (uintptr_t)&caller_progress;
    current = SUPERVISOR; sp_init(); current = BROKER; br_init();
    current = OBSERVER;
}
static struct tcs_lc_result control(uint64_t op, uint64_t slot)
{ current = OBSERVER; return ob_control(op, slot); }
static void active(void)
{
    reset(); assert(control(TCS_LC_RESERVE, 0).value == 1);
    current = WORKER; wk_init(); assert(wk_inc == 1);
    assert(control(TCS_LC_ACTIVATE, 0).status == TCS_LC_OK);
    current = CALLER; cl_notified(0); assert(cl_phase == 1 && kicks == 1);
    if (setjmp(trap) == 0) { current = WORKER; wk_notified(0); assert(false); }
    assert(sp_control.life.slots[0].pending == 1 && br_pending[0] == 1 && running[0]);
    current = OBSERVER;
}
static void notify_supervisor(void) { current = SUPERVISOR; sp_notified(0); current = OBSERVER; }

static void blocked_broker(void)
{
    active();
    if (setjmp(trap) == 0) { current = CALLER; cl_notified(0); assert(false); }
    assert(cl_phase == 2 && atomic_load(&caller_progress) == 2 && spin_count == 2);
    current = OBSERVER; ob_step = 3; ob_next();
    assert(atomic_load(&receipt) == 1 && !running[0] && stops[0] == 2 && stops[1] == 1);
    assert(sp_control.inhibited == 1 && sp_control.life.slots[0].pending == 1);
    assert(sp_control.life.slots[0].stopped == 1 && sp_control.life.slots[0].drained == 0);
    ob_next(); assert(ob_step == 5 && ob_probes == 7 && !ob_failed);
    assert(!resumes[1] && cl_phase == 2 && br_pending[0] == 1);
    for (unsigned i = 0; i < 1000; ++i) notify_supervisor();
    assert(stops[0] == 2 && sp_control.life.slots[0].pending == 1);
    atomic_store(&mailbox.requests, 0); notify_supervisor();
    assert(sp_control.inhibited == 1 && !running[0]);
}
static void no_poll_rescue(void)
{
    active(); deliver_notification = false; ob_step = 3;
    unsigned before = supervisor_calls;
    ob_next(); assert(ob_step == 4 && supervisor_calls == before && running[0]);
    for (unsigned i = 0; i < 10; ++i) { ob_report(); ob_next(); }
    assert(supervisor_calls == before && running[0] && !atomic_load(&receipt) && ob_step == 4);
    notify_supervisor(); ob_report();
    assert(supervisor_calls == before + 1 && !running[0] && atomic_load(&receipt) == 1);
    /* Ordinary RPC polling closes the gate too, but cannot fake this receipt. */
    active(); atomic_store(&mailbox.requests, 1);
    lc_mrs[0] = TCS_LC_RESERVE; lc_mrs[1] = 1;
    struct tcs_lc_result r = ct_response(call(SUPERVISOR, 0, microkit_msginfo_new(CT_CONTROL, 2)));
    assert(r.status == TCS_LC_BAD_STATE && !running[0] && atomic_load(&receipt) == 0);
    notify_supervisor(); assert(atomic_load(&receipt) == 0); /* No stop occurred in that callback. */
    active(); ob_step = 4; atomic_store(&receipt, 2); before = supervisor_calls;
    ob_report(); assert(ob_failed && supervisor_calls == before);
}
static void kernel_failures_and_early_requests(void)
{
    active(); atomic_store(&mailbox.requests, 1); fail_stop = 1;
    if (setjmp(trap) == 0) { notify_supervisor(); assert(false); }
    assert(running[0] && !atomic_load(&receipt) && !sp_control.life.slots[0].stopped);
    assert(sp_control.life.slots[0].state == TCS_LC_CLOSING && sp_control.life.slots[0].pending == 1);
    reset(); fail_resume = 1;
    if (setjmp(trap) == 0) { (void)control(TCS_LC_RESERVE, 0); assert(false); }
    assert(sp_control.life.slots[0].state == TCS_LC_STARTING && !running[0] && !atomic_load(&receipt));
    reset(); atomic_store(&mailbox.requests, 1); notify_supervisor();
    assert(control(TCS_LC_RESERVE, 0).status == TCS_LC_DENIED && !resumes[0] && !atomic_load(&receipt));
    assert(control(TCS_LC_RESERVE, 1).value == 1 && resumes[1] == 1);
    active(); atomic_store(&mailbox.requests, UINT64_MAX); notify_supervisor();
    assert(sp_control.input_fault == 1 && sp_control.inhibited == 3 && !running[0]);
    assert(control(TCS_LC_RESERVE, 1).status == TCS_LC_DENIED);
    active(); current = SUPERVISOR;
    assert(sp_fault(99, microkit_msginfo_new(0, 0), NULL) == seL4_False);
    assert(sp_failed && !running[0] && !atomic_load(&receipt));
    reset(); assert(control(TCS_LC_RESERVE, 0).status == TCS_LC_OK);
    atomic_store(&mailbox.requests, 1); notify_supervisor(); /* Stop before HELLO. */
    current = WORKER; wk_init();
    assert(!wk_inc && !running[0] && sp_control.life.slots[0].state == TCS_LC_CLOSING);
}
static void malformed_bridge_and_authority(void)
{
    for (unsigned corruption = 1; corruption <= 3; ++corruption) {
        reset(); assert(control(TCS_LC_RESERVE, 0).status == TCS_LC_OK);
        current = WORKER; wk_init(); assert(control(TCS_LC_ACTIVATE, 0).status == TCS_LC_OK);
        corrupt_bridge = corruption; current = WORKER; wk_notified(0);
        assert(br_failed && sp_control.life.slots[0].pending == 1);
        atomic_store(&mailbox.requests, 1); notify_supervisor();
        assert(!running[0] && sp_control.life.slots[0].pending == 1 && atomic_load(&receipt) == 1);
    }
    active();
    for (unsigned ch = 0; ch < 4; ++ch) {
        if (ch == 1) continue;
        lc_mrs[0] = 0; lc_mrs[1] = 1;
        assert(ct_response(call(SUPERVISOR, ch, microkit_msginfo_new(CT_BEGIN, 2))).status != TCS_LC_OK);
        lc_mrs[0] = 0;
        assert(ct_response(call(SUPERVISOR, ch, microkit_msginfo_new(CT_READY, 1))).status != TCS_LC_OK);
    }
    for (unsigned ch = 0; ch < 4; ++ch) {
        for (unsigned count = 0; count <= 64; ++count) {
            lc_mrs[0] = TCS_LC_DRAINED; lc_mrs[1] = 0;
            struct tcs_lc_result r = ct_response(call(SUPERVISOR, ch, microkit_msginfo_new(CT_CONTROL, count)));
            assert(r.status != TCS_LC_OK && !sp_control.life.slots[0].drained);
            lc_mrs[0] = 0; lc_mrs[1] = 1;
            /* The single valid caller shape is checked separately with a trap. */
            if (ch != 2 || count != 0) {
                r = ct_response(call(BROKER, ch, microkit_msginfo_new(CT_HANG, count)));
                assert(r.status != TCS_LC_OK);
            }
        }
    }
}
static void feed(const char *bytes, unsigned loss_at)
{
    rx_count = strlen(bytes); rx_at = 0; assert(rx_count < sizeof rx_bytes);
    for (size_t i = 0; i < rx_count; ++i) { rx_bytes[i] = (uint8_t)bytes[i]; rx_flags[i] = 1 | (i == loss_at ? 2 : 0); }
    current = OBSERVER;
    for (unsigned i = 0; i < 100; ++i) ob_notified(0);
}
static void observer_input_and_partial_echo(void)
{
    reset(); feed("nextx\nnext\003", 99); assert(ob_step == 0 && !resumes[0]);
    reset(); feed("next\n", 2); assert(ob_step == 0 && !resumes[0]);
    reset(); tx_limit = 0; feed("next\n", 99); assert(ob_step == 0 && !resumes[0]);
    tx_limit = 1; current = OBSERVER;
    for (unsigned i = 0; i < 200; ++i) ob_notified(0);
    assert(ob_step == 1 && resumes[0] == 1 && !ob_failed);
    assert(memcmp(tx_bytes, "next\r\nSTATE 1", 13) == 0);
}
int main(void)
{
    blocked_broker(); no_poll_rescue(); kernel_failures_and_early_requests();
    malformed_bridge_and_authority(); observer_input_and_partial_echo();
    puts("PASS containment adapters: hung-call trap, no observer broker RPC, notification-only receipt, no status-poll rescue");
    puts("PASS gate/stop ordering, nonreturning kernel failure, clobbered registers, missing drain, malformed replies, authority and echo/loss");
    return 0;
}
