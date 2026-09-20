#include "tcs/serial.h"

void tcs_serial_loss(struct tcs_serial_rx *rx)
{
    /* Remove bytes preceding the gap: their delimiter cannot clear the loss. */
    rx->head = 0;
    rx->count = 0;
    rx->lost = true;
}

void tcs_serial_receive(struct tcs_serial_rx *rx, uint8_t byte)
{
    if (rx->count == TCS_SERIAL_CAPACITY)
        tcs_serial_loss(rx);
    rx->bytes[(rx->head + rx->count) % TCS_SERIAL_CAPACITY] = byte;
    ++rx->count;
}

unsigned tcs_serial_take(struct tcs_serial_rx *rx, uint8_t *byte)
{
    unsigned flags = rx->lost ? TCS_SERIAL_LOSS : 0;
    rx->lost = false;
    *byte = 0;
    if (rx->count != 0) {
        *byte = rx->bytes[rx->head];
        rx->head = (rx->head + 1) % TCS_SERIAL_CAPACITY;
        --rx->count;
        flags |= TCS_SERIAL_BYTE;
    }
    return flags;
}
