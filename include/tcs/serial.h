#ifndef TCS_SERIAL_H
#define TCS_SERIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TCS_SERIAL_CAPACITY 256u
#define TCS_SERIAL_CHUNK 32u
#define TCS_SERIAL_READ 48u
#define TCS_SERIAL_WRITE 49u
#define TCS_SERIAL_DATA UINT64_C(0x181)
#define TCS_SERIAL_BYTE 1u
#define TCS_SERIAL_LOSS 2u

/* Single-threaded driver state: no shared memory with the terminal. */
struct tcs_serial_rx {
    uint8_t bytes[TCS_SERIAL_CAPACITY];
    size_t head, count;
    bool lost;
};

void tcs_serial_loss(struct tcs_serial_rx *rx);
void tcs_serial_receive(struct tcs_serial_rx *rx, uint8_t byte);
unsigned tcs_serial_take(struct tcs_serial_rx *rx, uint8_t *byte);

#endif
