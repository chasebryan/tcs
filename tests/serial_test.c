#include "tcs/serial.h"
#include "tcs/terminal.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct tcs_serial_rx rx = {0};
    uint8_t byte = 99;
    assert(tcs_serial_take(&rx, &byte) == 0 && byte == 0);
    for (unsigned pass = 0; pass < 4; ++pass) {
        for (unsigned i = 0; i < TCS_SERIAL_CAPACITY; ++i)
            tcs_serial_receive(&rx, (uint8_t)i);
        for (unsigned i = 0; i < TCS_SERIAL_CAPACITY; ++i) {
            assert(tcs_serial_take(&rx, &byte) == TCS_SERIAL_BYTE);
            assert(byte == (uint8_t)i);
        }
    }
    /* A delimiter queued before loss must not unpoison the command after it. */
    tcs_serial_receive(&rx, '\n');
    for (unsigned i = 1; i < TCS_SERIAL_CAPACITY; ++i)
        tcs_serial_receive(&rx, ' ');
    tcs_serial_receive(&rx, 'h');
    assert(rx.count == 1);
    assert(tcs_serial_take(&rx, &byte) == (TCS_SERIAL_BYTE | TCS_SERIAL_LOSS));
    assert(byte == 'h');
    struct tcs_line line = {0};
    tcs_line_discard(&line);
    assert(tcs_line_feed(&line, byte) == TCS_LINE_NONE);
    const char *tail = "elp\n";
    for (size_t i = 0; tail[i]; ++i) {
        enum tcs_line_event event = tcs_line_feed(&line, (uint8_t)tail[i]);
        assert(event == (tail[i] == '\n' ? TCS_LINE_REJECTED : TCS_LINE_NONE));
    }
    tcs_serial_loss(&rx);
    assert(tcs_serial_take(&rx, &byte) == TCS_SERIAL_LOSS && byte == 0);
    assert(tcs_serial_take(&rx, &byte) == 0);
    uint32_t random = 0x142ac3u;
    for (unsigned i = 0; i < 100000; ++i) {
        random = random * 1664525u + 1013904223u;
        if ((random & 7u) == 0) tcs_serial_loss(&rx);
        else if ((random & 3u) == 1) (void)tcs_serial_take(&rx, &byte);
        else tcs_serial_receive(&rx, (uint8_t)random);
        assert(rx.head < TCS_SERIAL_CAPACITY && rx.count <= TCS_SERIAL_CAPACITY);
    }
    puts("TCS SERIAL BUFFER TESTS PASS (100000 transitions)");
}
