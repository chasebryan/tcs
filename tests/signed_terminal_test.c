#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TCS_TEST_IPC_ROUTER
#define TCS_SIGNED_INPUT 1
#define TCS_LAUNCH_MODE 1
#include "../servers/terminal.c"
static char captured[32768];
static uint8_t input[2048];
static size_t captured_length, input_head, input_count, tx_limit = 3;
static unsigned admin_calls, status_calls, response_mode;
static bool input_loss;
static microkit_msginfo test_ppcall(microkit_channel ch, microkit_msginfo msg)
{
    if (ch == SERIAL_CHANNEL && tcs_message(msg, TCS_SERIAL_READ, 0)) {
        unsigned flags = input_loss ? TCS_SERIAL_LOSS : 0; input_loss = false;
        uint8_t byte = 0;
        if (input_head < input_count) { flags |= TCS_SERIAL_BYTE; byte = input[input_head++]; }
        microkit_mr_set(0, flags); microkit_mr_set(1, byte);
        return microkit_msginfo_new(TCS_SERIAL_DATA, 2);
    }
    if (ch == SERIAL_CHANNEL) {
        size_t count = microkit_msginfo_get_count(msg);
        assert(microkit_msginfo_get_label(msg) == TCS_LABEL(TCS_SERIAL_WRITE) && count <= TCS_SERIAL_CHUNK);
        if (count > tx_limit) count = tx_limit;
        assert(captured_length + count < sizeof captured);
        for (size_t i = 0; i < count; ++i) captured[captured_length++] = (char)microkit_mr_get(i);
        captured[captured_length] = 0; return tcs_reply((struct tcs_result){TCS_OK, count});
    }
    assert(captured_length && captured[captured_length-1] == '\n'); /* Echo completed before IPC. */
    if (ch == CLIENT_CHANNEL) {
        assert(tcs_message(msg, TCS_CLIENT_STATUS, 0)); ++status_calls;
        return tcs_snapshot_reply((struct tcs_snapshot){TCS_OK,TCS_RESTRICTED,0,0,0});
    }
    assert(ch == 2 && msg.label == TCS_ADMIN_SUBMIT && msg.count == 24); ++admin_calls;
    uint8_t packet[192]; tcs_bytes_from_words(packet, 192);
    struct tcs_admin_command c = tcs_signed_command(packet);
    for (unsigned i = 0; i < 64; ++i) test_mrs[i] = UINT64_MAX;
    if (response_mode == 1) return tcs_admin_admission_reply(TCS_ADMIN_BAD_SIGNATURE);
    if (response_mode == 2) return microkit_msginfo_new(TCS_ADMIN_RECEIPT, 12);
    if (response_mode == 3) return tcs_admin_admission_reply(TCS_ADMIN_BAD_COMPLETION);
    struct tcs_admin_receipt r = {c, TCS_OK, 1, 1,
        {TCS_OK, TCS_QUARANTINED, c.expected_generation == UINT64_MAX ? UINT64_MAX : c.expected_generation+1,0,0}};
    return tcs_admin_receipt_reply(r);
}
static void feed(const char *text)
{
    assert(input_head == input_count); input_count = strlen(text); input_head = 0;
    assert(input_count < sizeof input); memcpy(input, text, input_count);
}
static void drain(void)
{
    for (unsigned i = 0; i < 2000; ++i) notified(SERIAL_CHANNEL);
    assert(!failed && input_head == input_count && output_length == 0 && pending_event == TCS_LINE_NONE);
}
int main(void)
{
    char frame[388]; uint8_t packet[192] = {0};
    packet[80] = 9; packet[88] = TCS_QUARANTINE; packet[96] = 1; packet[120] = 4;
    const char *hex = "0123456789abcdef";
    for (unsigned i = 0; i < 192; ++i) { frame[2*i] = hex[packet[i] >> 4]; frame[2*i+1] = hex[packet[i] & 15]; }
    frame[384] = '\r'; frame[385] = '\n'; frame[386] = 0;
    tx_limit = 0; feed("submit\r\n"); init(); assert(input_head == 0);
    tx_limit = 3; drain(); assert(packet_mode && signed_input.digits == 0);
    feed(frame); drain();
    assert(admin_calls == 1 && !packet_mode && strstr(captured, "ADMIN seq=9 decision=0 audit=1 applied=1 state=2 generation=5\r\ntcs> "));
    for (unsigned mode = 1; mode <= 3; ++mode) {
        response_mode = mode; feed("submit\n"); drain(); feed(frame); drain();
    }
    assert(admin_calls == 4 && strstr(captured,"ADMIN REJECTED status=4") && strstr(captured,"ADMIN UNAVAILABLE"));
    assert(strstr(captured,"ADMIN UNCERTAIN; do not retry or advance sequence") && !strstr(captured,"ADMIN REJECTED status=8"));
    feed("submit\n"); drain(); feed("abc"); drain(); input_loss = true; drain();
    feed(frame); drain(); assert(admin_calls == 4 && !packet_mode && strstr(captured,"ERROR discarded signed packet"));
    feed("submit\n"); drain(); feed("abcdef\3"); drain(); assert(admin_calls == 4 && !packet_mode);
    feed("grant 1\nrevoke 1\nrestore 1\nquarantine 1\nstatus\n"); drain();
    assert(admin_calls == 4 && status_calls == 1 && strstr(captured,"CANCELLED"));
    response_mode = 0;
    for (unsigned i = 80; i < 88; ++i) packet[i] = 255;
    for (unsigned i = 120; i < 128; ++i) packet[i] = 255;
    for (unsigned i = 0; i < 192; ++i) { frame[2*i] = hex[packet[i] >> 4]; frame[2*i+1] = hex[packet[i] & 15]; }
    feed("submit\n"); drain(); feed(frame); drain();
    assert(admin_calls == 5 && strstr(captured,"generation=18446744073709551615"));
    puts("PASS signed terminal adapter: partial TX/echo ordering, exact packet IPC, correlated/malformed replies, loss/cancel, CRLF, maximum receipt fields, plaintext denial");
}
