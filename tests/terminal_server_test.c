#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TCS_TEST_IPC_ROUTER
#include "../servers/terminal.c"

static char captured[8192];
static size_t captured_length, input_head, input_count, tx_limit = 3;
static uint8_t input[256];
static bool input_loss, bad_reply;
static unsigned reads, status_calls, object_calls;

static microkit_msginfo test_ppcall(microkit_channel ch, microkit_msginfo msg)
{
    if (ch == SERIAL_CHANNEL && tcs_message(msg, TCS_SERIAL_READ, 0)) {
        ++reads;
        if (bad_reply) return microkit_msginfo_new(0, 0);
        unsigned flags = input_loss ? TCS_SERIAL_LOSS : 0;
        input_loss = false;
        uint8_t byte = 0;
        if (input_head < input_count) {
            flags |= TCS_SERIAL_BYTE;
            byte = input[input_head++];
        }
        microkit_mr_set(0, flags); microkit_mr_set(1, byte);
        return microkit_msginfo_new(TCS_SERIAL_DATA, 2);
    }
    if (ch == SERIAL_CHANNEL) {
        size_t count = microkit_msginfo_get_count(msg);
        assert(microkit_msginfo_get_label(msg) == TCS_LABEL(TCS_SERIAL_WRITE));
        assert(count && count <= TCS_SERIAL_CHUNK);
        if (count > tx_limit) count = tx_limit;
        assert(captured_length + count < sizeof captured);
        for (size_t i = 0; i < count; ++i)
            captured[captured_length++] = (char)microkit_mr_get(i);
        captured[captured_length] = 0;
        return tcs_reply((struct tcs_result){TCS_OK, count});
    }
    assert(ch == CLIENT_CHANNEL);
    assert(captured_length && captured[captured_length - 1] == '\n');
    if (tcs_message(msg, TCS_CLIENT_STATUS, 0)) {
        ++status_calls;
        return tcs_snapshot_reply((struct tcs_snapshot){TCS_OK, TCS_RESTRICTED, 0, 0, 0});
    }
    assert(tcs_message(msg, TCS_CLIENT_READ, 3));
    ++object_calls;
    return tcs_reply((struct tcs_result){TCS_DENIED, 0});
}

static void feed(const char *text)
{
    assert(input_head == input_count);
    input_count = strlen(text); input_head = 0;
    assert(input_count <= sizeof input);
    memcpy(input, text, input_count);
}

static void drain(void)
{
    for (unsigned i = 0; i < 200; ++i) notified(SERIAL_CHANNEL);
    assert(!failed && output_length == 0 && pending_event == TCS_LINE_NONE);
}

int main(void)
{
    tx_limit = 0; feed("version\n"); init();
    assert(reads == 0 && input_head == 0); /* Output backpressure stops consumption. */
    tx_limit = 3; drain();
    assert(strcmp(captured, "TCS TERMINAL READY (host-test, read-only)\r\ntcs> version\r\n"
                           "TCS 0.2-dev / host-test / read-only terminal\r\ntcs> ") == 0);
    feed("status\n"); drain();
    assert(status_calls == 1 && object_calls == 0);
    feed("read 1"); drain();
    input_loss = true; drain();
    assert(strstr(captured, "read 1\r\nINPUT LOST; discard until Enter or Ctrl-C\r\n"));
    feed("\n"); drain();
    assert(object_calls == 0 && strstr(captured, "ERROR discarded input line"));
    feed("read 1\n"); drain();
    assert(object_calls == 1);
    bad_reply = true; feed("read 1\n"); notified(SERIAL_CHANNEL);
    assert(failed && input_head == 0 && object_calls == 1);
    puts("TCS TERMINAL SERVER TESTS PASS (partial/full TX, echo ordering, loss, malformed driver reply)");
}
