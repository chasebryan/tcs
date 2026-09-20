#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tcs/signed_input.h"
static bool empty(const struct tcs_signed_input *s)
{ for (unsigned i = 0; i < 192; ++i) if (s->packet[i]) return false; return true; }
static void fill(struct tcs_signed_input *s, size_t n)
{ for (size_t i = 0; i < n; ++i) assert(tcs_signed_feed(s, 'a').event == TCS_LINE_NONE); }
int main(void)
{
    struct tcs_signed_input s;
    for (size_t n = 0; n <= 386; ++n) {
        tcs_signed_begin(&s, false); fill(&s, n);
        assert(tcs_signed_feed(&s, '\n').event == (n == 384 ? TCS_LINE_READY : TCS_LINE_REJECTED));
        if (n != 384) assert(empty(&s));
        else for (unsigned i = 0; i < 192; ++i) assert(s.packet[i] == 0xaa);
        assert(tcs_signed_feed(&s, '\n').event == TCS_LINE_NONE);
    }
    for (unsigned byte = 0; byte < 256; ++byte) {
        tcs_signed_begin(&s, false); fill(&s, 32);
        struct tcs_line_feedback f = tcs_signed_feed(&s, (uint8_t)byte);
        bool hex = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
        if (hex) assert(s.digits == 33 && f.echo[0] == (char)byte);
        else if (byte == 3) assert(f.event == TCS_LINE_CANCELLED && empty(&s));
        else if (byte == '\n' || byte == '\r') assert(f.event == TCS_LINE_REJECTED && empty(&s));
        else assert(s.rejected && s.digits == 0 && empty(&s) && !f.echo[0]);
    }
    for (size_t offset = 0; offset <= 384; ++offset) {
        tcs_signed_begin(&s, false); fill(&s, offset); tcs_signed_discard(&s); fill(&s, 384 - offset);
        assert(tcs_signed_feed(&s, '\n').event == TCS_LINE_REJECTED && empty(&s));
        tcs_signed_begin(&s, false); fill(&s, offset);
        assert(tcs_signed_feed(&s, 3).event == TCS_LINE_CANCELLED && empty(&s));
        fill(&s, 384); assert(tcs_signed_feed(&s, '\n').event == TCS_LINE_NONE);
    }
    tcs_signed_begin(&s, true); assert(tcs_signed_feed(&s, '\n').event == TCS_LINE_NONE && s.digits == 0);
    fill(&s, 384); assert(tcs_signed_feed(&s, '\r').event == TCS_LINE_READY && s.previous_cr);
    uint8_t packet[192] = {0}; packet[80] = 1; packet[88] = TCS_GRANT; packet[96] = 2; packet[104] = 42; packet[112] = 1;
    packet[127] = 0x80;
    struct tcs_admin_command c = tcs_signed_command(packet);
    assert(c.sequence == 1 && c.expected_generation == (UINT64_C(1) << 63) && c.request.subject == 2 &&
        c.request.op == TCS_GRANT && c.request.object == 42 && c.request.rights == 1 && !c.request.generation);
    puts("PASS exact signed frames: all lengths through 386, 256 byte classes, cancellation/loss at every position, CRLF and one-shot completion");
}
