#ifndef TCS_SIGNED_INPUT_H
#define TCS_SIGNED_INPUT_H
#include "tcs/terminal.h"
#include "tcs/admin.h"
struct tcs_signed_input {
    uint8_t packet[TCS_ADMIN_PACKET_BYTES];
    size_t digits;
    bool rejected, previous_cr, completed;
};
/* Exact lowercase hex, no editing/whitespace. Enter completes; Ctrl-C cancels.
 * Transport loss must discard the entire frame. Completion is one-shot until
 * begin. Packet must be privately captured before any later begin/feed. */
void tcs_signed_begin(struct tcs_signed_input *input, bool previous_cr);
void tcs_signed_discard(struct tcs_signed_input *input);
struct tcs_line_feedback tcs_signed_feed(struct tcs_signed_input *input, uint8_t byte);
/* Untrusted public field extraction only, NOT authentication. */
struct tcs_admin_command tcs_signed_command(const uint8_t packet[TCS_ADMIN_PACKET_BYTES]);
#endif
