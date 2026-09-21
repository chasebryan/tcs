#include "runtime.h"
#include "tcs/serial.h"
#include "tcs/terminal.h"
uintptr_t counter_a_vaddr, counter_b_vaddr;
static struct tcs_line ct_line;
static enum tcs_line_event ct_event;
static char ct_output[1024];
static size_t ct_length, ct_sent;
static bool ct_failed;
static unsigned ct_step;

static void ct_append(const char *s)
{
    while (*s && ct_length < sizeof ct_output) ct_output[ct_length++] = *s++;
    if (*s) ct_failed = true;
}
static void ct_number(uint64_t n)
{
    char s[21]; unsigned length = 0;
    do { s[length++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (length) { char digit[2] = {s[--length], 0}; ct_append(digit); }
}
static bool ct_flush(void)
{
    if (ct_sent == ct_length) return true;
    size_t count = ct_length - ct_sent;
    if (count > TCS_SERIAL_CHUNK) count = TCS_SERIAL_CHUNK;
    for (size_t i = 0; i < count; ++i) microkit_mr_set((unsigned)i, (uint8_t)ct_output[ct_sent + i]);
    struct tcs_result r = tcs_response(microkit_ppcall(0, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_WRITE), count)));
    if (r.status != TCS_OK || r.value > count) { ct_failed = true; return false; }
    ct_sent += r.value;
    if (ct_sent != ct_length) return false;
    ct_sent = ct_length = 0;
    return true;
}
static void ct_fail(void)
{ ct_append("TCS LIFECYCLE FAIL\r\n"); ct_failed = true; }
static struct tcs_lc_result ct_control(uint64_t op, uint64_t slot)
{
    microkit_mr_set(0, op); microkit_mr_set(1, slot);
    return lt_response(microkit_ppcall(1, microkit_msginfo_new(LT_CONTROL, 2)));
}
static struct tcs_lc_result ct_broker(uint64_t op, uint64_t slot)
{
    microkit_mr_set(0, slot);
    return lt_response(microkit_ppcall(2, microkit_msginfo_new(op, 1)));
}
static void ct_report(void)
{
    uint64_t words[LT_WORDS], stats[LT_STATS_WORDS];
    microkit_msginfo m = microkit_ppcall(1, microkit_msginfo_new(LT_STATUS, 0));
    if (!lt_message(m, LT_SNAPSHOT, LT_WORDS)) { ct_fail(); return; }
    for (unsigned i = 0; i < LT_WORDS; ++i) words[i] = microkit_mr_get(i);
    m = microkit_ppcall(2, microkit_msginfo_new(LT_BROKER_STATUS, 0));
    if (!lt_message(m, LT_STATS, LT_STATS_WORDS)) { ct_fail(); return; }
    for (unsigned i = 0; i < LT_STATS_WORDS; ++i) stats[i] = microkit_mr_get(i);
    if (words[0] || stats[0]) { ct_fail(); return; }
    ct_append("STATE "); ct_number(ct_step);
    for (unsigned i = 0; i < LT_WORDS; ++i) { ct_append(" "); ct_number(words[i]); }
    for (unsigned i = 0; i < LT_STATS_WORDS; ++i) { ct_append(" "); ct_number(stats[i]); }
    ct_append(" "); ct_number(atomic_load_explicit((_Atomic(uint64_t) *)counter_a_vaddr, memory_order_acquire));
    ct_append(" "); ct_number(atomic_load_explicit((_Atomic(uint64_t) *)counter_b_vaddr, memory_order_acquire));
    ct_append("\r\n");
}
static void ct_next(void)
{
    struct tcs_lc_result r = {TCS_LC_BAD_STATE, 0};
    switch (ct_step) {
    case 0: r = ct_control(TCS_LC_RESERVE, 0); break;
    case 1:
        r = ct_control(TCS_LC_ACTIVATE, 0);
        if (r.status == TCS_LC_OK) r = ct_broker(LT_KICK, 0);
        break;
    case 2: r = ct_control(TCS_LC_CONTAIN, 0); break;
    case 3: r = ct_broker(LT_DRAIN, 0); break;
    case 4: r = ct_control(TCS_LC_RESERVE, 1); break;
    case 5:
        r = ct_control(TCS_LC_ACTIVATE, 1);
        if (r.status == TCS_LC_OK) r = ct_broker(LT_KICK, 1);
        break;
    case 6: r = ct_broker(LT_DRAIN, 1); break;
    case 7:
        if (ct_control(TCS_LC_RESERVE, 0).status != TCS_LC_BAD_STATE ||
            ct_control(TCS_LC_RESERVE, 1).status != TCS_LC_BAD_STATE) { ct_fail(); return; }
        r = (struct tcs_lc_result){TCS_LC_OK, 0};
        break;
    default: ct_append("DONE\r\n"); return;
    }
    if (r.status != TCS_LC_OK) { ct_fail(); return; }
    ++ct_step;
    ct_report();
    if (!ct_failed && ct_step == 8) ct_append("TCS LIFECYCLE PASS (test fixture, no resource reuse)\r\n");
}
static bool ct_match(const char *s)
{
    size_t i = 0;
    while (s[i] && i < ct_line.length && s[i] == ct_line.bytes[i]) ++i;
    return i == ct_line.length && s[i] == 0;
}
static void ct_execute(void)
{
    if (ct_match("status")) ct_report();
    else if (ct_match("next")) ct_next();
    else if (ct_line.length) ct_append("ERROR fixture commands: status | next\r\n");
    ct_append("fixture> ");
}
static void ct_service(void)
{
    for (unsigned step = 0; step < 32; ++step) {
        if (!ct_flush() || ct_failed) return;
        if (ct_event != TCS_LINE_NONE) {
            enum tcs_line_event event = ct_event; ct_event = TCS_LINE_NONE;
            if (event == TCS_LINE_READY) ct_execute();
            else ct_append("ERROR discarded input\r\nfixture> ");
            continue;
        }
        microkit_msginfo m = microkit_ppcall(0, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_READ), 0));
        if (!lt_message(m, TCS_SERIAL_DATA, 2)) { ct_fail(); break; }
        uint64_t flags = microkit_mr_get(0), byte = microkit_mr_get(1);
        if (flags > 3 || byte > 255 || (!(flags & TCS_SERIAL_BYTE) && byte)) { ct_fail(); break; }
        if (flags & TCS_SERIAL_LOSS) tcs_line_discard(&ct_line);
        if (!(flags & TCS_SERIAL_BYTE)) { (void)ct_flush(); return; }
        struct tcs_line_feedback f = tcs_terminal_input(&ct_line, (uint8_t)byte);
        ct_append(f.echo); ct_event = f.event;
    }
    (void)ct_flush();
}
void init(void)
{
    ct_append("TCS LIFECYCLE READY (release-kernel, TEST-ONLY, fixture approval)\r\n");
    ct_report(); ct_append("fixture> "); ct_service();
}
void notified(microkit_channel ch) { if (ch == 0) ct_service(); }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo m)
{ (void)ch; (void)m; return lt_reply(TCS_LC_DENIED, 0); }
