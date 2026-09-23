#include "runtime.h"
#include "tcs/serial.h"
#include "tcs/terminal.h"
uintptr_t observer_request_vaddr, observer_receipt_vaddr;
uintptr_t observer_worker_a_vaddr, observer_worker_b_vaddr, observer_broker_vaddr, observer_caller_vaddr;
static struct tcs_line ob_line;
static enum tcs_line_event ob_event;
static char ob_output[1024];
static size_t ob_length, ob_sent;
static bool ob_failed;
static unsigned ob_step, ob_probes;

static void ob_append(const char *s)
{
    while (*s && ob_length < sizeof ob_output) ob_output[ob_length++] = *s++;
    if (*s) ob_failed = true;
}
static void ob_number(uint64_t n)
{
    char s[21]; unsigned length = 0;
    do { s[length++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (length) { char digit[2] = {s[--length], 0}; ob_append(digit); }
}
static bool ob_flush(void)
{
    if (ob_sent == ob_length) return true;
    size_t count = ob_length - ob_sent;
    if (count > TCS_SERIAL_CHUNK) count = TCS_SERIAL_CHUNK;
    for (size_t i = 0; i < count; ++i) microkit_mr_set((unsigned)i, (uint8_t)ob_output[ob_sent + i]);
    struct tcs_result r = tcs_response(microkit_ppcall(0, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_WRITE), count)));
    if (r.status != TCS_OK || r.value > count) { ob_failed = true; return false; }
    ob_sent += r.value;
    if (ob_sent != ob_length) return false;
    ob_sent = ob_length = 0; return true;
}
static void ob_fail(void) { ob_append("TCS CONTAINMENT FAIL\r\n"); ob_failed = true; }
static bool ob_waiting(void)
{
    if (ob_step >= 4) {
        uint64_t receipt = ct_load(observer_receipt_vaddr);
        if (receipt > 1) { ob_fail(); return true; }
        if (!receipt) { ob_append("WAIT notification\r\n"); return true; }
    }
    return false;
}
static struct tcs_lc_result ob_control(uint64_t op, uint64_t slot)
{
    microkit_mr_set(0, op); microkit_mr_set(1, slot);
    return ct_response(microkit_ppcall(1, microkit_msginfo_new(CT_CONTROL, 2)));
}
static void ob_report(void)
{
    /* No supervisor RPC after publication until the notification-only receipt
     * confirms stop. Otherwise a status call could accidentally rescue the test. */
    if (ob_waiting()) return;
    microkit_msginfo m = microkit_ppcall(1, microkit_msginfo_new(CT_STATUS, 0));
    if (!ct_message(m, CT_SNAPSHOT, CT_WORDS)) { ob_fail(); return; }
    uint64_t words[CT_WORDS];
    for (unsigned i = 0; i < CT_WORDS; ++i) words[i] = microkit_mr_get(i);
    if (words[0] || words[2]) { ob_fail(); return; }
    ob_append("STATE "); ob_number(ob_step);
    for (unsigned i = 0; i < CT_WORDS; ++i) { ob_append(" "); ob_number(words[i]); }
    const uintptr_t addresses[] = {observer_worker_a_vaddr, observer_worker_b_vaddr,
        observer_broker_vaddr, observer_caller_vaddr, observer_receipt_vaddr};
    for (unsigned i = 0; i < 5; ++i) { ob_append(" "); ob_number(ct_load(addresses[i])); }
    ob_append(" "); ob_number(ob_probes); ob_append("\r\n");
}
static void ob_next(void)
{
    if (ob_waiting()) return;
    struct tcs_lc_result r = {TCS_LC_OK, 0};
    switch (ob_step) {
    case 0: r = ob_control(TCS_LC_RESERVE, 0); break;
    case 1:
        r = ob_control(TCS_LC_ACTIVATE, 0);
        if (r.status == TCS_LC_OK) microkit_notify(2);
        break;
    case 2: microkit_notify(2); break; /* Caller, NOT this observer, calls the broker. */
    case 3:
        (void)atomic_fetch_or_explicit(&((struct tcs_reduction_mailbox *)observer_request_vaddr)->requests,
            UINT64_C(1), memory_order_release);
        microkit_notify(1); /* Nonblocking independent request; no broker/serial roundtrip. */
        break;
    case 4:
        if (ob_control(TCS_LC_RESERVE, 0).status != TCS_LC_DENIED ||
            ob_control(TCS_LC_ACTIVATE, 0).status != TCS_LC_DENIED ||
            ob_control(TCS_LC_RESERVE, 1).status != TCS_LC_BAD_STATE) { ob_fail(); return; }
        ob_probes = 7; break;
    default: ob_append("DONE\r\n"); return;
    }
    if (r.status != TCS_LC_OK) { ob_fail(); return; }
    ++ob_step; ob_report();
    if (!ob_failed && ob_step == 5) ob_append("TCS CONTAINMENT PASS (test fixture, missing drain blocks replacement)\r\n");
}
static bool ob_match(const char *s)
{
    size_t i = 0;
    while (s[i] && i < ob_line.length && s[i] == ob_line.bytes[i]) ++i;
    return i == ob_line.length && s[i] == 0;
}
static void ob_execute(void)
{
    if (ob_match("status")) ob_report();
    else if (ob_match("next")) ob_next();
    else if (ob_line.length) ob_append("ERROR fixture commands: status | next\r\n");
    ob_append("fixture> ");
}
static void ob_service(void)
{
    for (unsigned step = 0; step < 32; ++step) {
        if (!ob_flush() || ob_failed) return;
        if (ob_event != TCS_LINE_NONE) {
            enum tcs_line_event event = ob_event; ob_event = TCS_LINE_NONE;
            if (event == TCS_LINE_READY) ob_execute();
            else ob_append("ERROR discarded input\r\nfixture> ");
            continue;
        }
        microkit_msginfo m = microkit_ppcall(0, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_READ), 0));
        if (!ct_message(m, TCS_SERIAL_DATA, 2)) { ob_fail(); break; }
        uint64_t flags = microkit_mr_get(0), byte = microkit_mr_get(1);
        if (flags > 3 || byte > 255 || (!(flags & TCS_SERIAL_BYTE) && byte)) { ob_fail(); break; }
        if (flags & TCS_SERIAL_LOSS) tcs_line_discard(&ob_line);
        if (!(flags & TCS_SERIAL_BYTE)) { (void)ob_flush(); return; }
        struct tcs_line_feedback f = tcs_terminal_input(&ob_line, (uint8_t)byte);
        ob_append(f.echo); ob_event = f.event;
    }
    (void)ob_flush();
}
void init(void)
{
    ob_append("TCS CONTAINMENT READY (release-kernel, TEST-ONLY, fixture approval)\r\n");
    ob_report(); ob_append("fixture> "); ob_service();
}
void notified(microkit_channel ch) { if (ch == 0) ob_service(); }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo m)
{ (void)ch; (void)m; return ct_reply(TCS_LC_DENIED, 0); }
