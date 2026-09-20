#include "tcs/signed_input.h"
void tcs_signed_begin(struct tcs_signed_input *s, bool previous_cr)
{ *s = (struct tcs_signed_input){0}; s->previous_cr = previous_cr; }
void tcs_signed_discard(struct tcs_signed_input *s)
{ *s = (struct tcs_signed_input){0}; s->rejected = true; }
struct tcs_line_feedback tcs_signed_feed(struct tcs_signed_input *s, uint8_t byte)
{
    struct tcs_line_feedback f = {TCS_LINE_NONE, {0}};
    if (s->completed) return f;
    if (byte == '\n' && s->previous_cr) { s->previous_cr = false; return f; }
    s->previous_cr = byte == '\r';
    if (byte == 3) {
        tcs_signed_discard(s); s->completed = true;
        f.event = TCS_LINE_CANCELLED; f.echo[0] = '^'; f.echo[1] = 'C'; f.echo[2] = '\r'; f.echo[3] = '\n';
    } else if (byte == '\r' || byte == '\n') {
        bool valid = !s->rejected && s->digits == 2*TCS_ADMIN_PACKET_BYTES;
        if (!valid) { bool cr = s->previous_cr; tcs_signed_discard(s); s->previous_cr = cr; }
        s->completed = true; f.event = valid ? TCS_LINE_READY : TCS_LINE_REJECTED;
        f.echo[0] = '\r'; f.echo[1] = '\n';
    } else if (!s->rejected) {
        unsigned nibble;
        if (byte >= '0' && byte <= '9') nibble = byte - '0';
        else if (byte >= 'a' && byte <= 'f') nibble = byte - 'a' + 10;
        else { tcs_signed_discard(s); return f; }
        if (s->digits >= 2*TCS_ADMIN_PACKET_BYTES) { tcs_signed_discard(s); return f; }
        size_t i = s->digits / 2;
        if (s->digits % 2) s->packet[i] |= (uint8_t)nibble;
        else s->packet[i] = (uint8_t)(nibble << 4);
        ++s->digits; f.echo[0] = (char)byte;
    }
    return f;
}
static uint64_t load(const uint8_t *p)
{ uint64_t value = 0; for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (8*i); return value; }
struct tcs_admin_command tcs_signed_command(const uint8_t p[TCS_ADMIN_PACKET_BYTES])
{
    return (struct tcs_admin_command){load(p+80), load(p+120),
        {load(p+88), load(p+96), load(p+104), load(p+112), 0}};
}
