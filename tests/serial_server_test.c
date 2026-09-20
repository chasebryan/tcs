#include <assert.h>
#include <stdio.h>
#include "../servers/serial.c"

static uint32_t registers[1024];

int main(void)
{
    uart_base_vaddr = (uintptr_t)registers;
    init();
    assert(test_acks == 1 && REG(IMSC) == RECEIVE_IRQS && REG(DMACR) == 0);
    microkit_msginfo response = protected(9, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_READ), 0));
    assert(tcs_response(response).status == TCS_DENIED);
    response = protected(1, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_WRITE), 33));
    assert(tcs_response(response).status == TCS_BAD_MESSAGE);
    response = protected(1, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_READ), 1));
    assert(tcs_response(response).status == TCS_BAD_MESSAGE);
    REG(DR) = 123;
    microkit_mr_set(0, 'a'); microkit_mr_set(1, 256);
    response = protected(1, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_WRITE), 2));
    assert(tcs_response(response).status == TCS_BAD_MESSAGE && REG(DR) == 123);
    REG(FR) = TX_FULL | RX_EMPTY;
    microkit_mr_set(0, 'a');
    response = protected(1, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_WRITE), 1));
    struct tcs_result result = tcs_response(response);
    assert(result.status == TCS_OK && result.value == 0 && (REG(IMSC) & TX_IRQ));
    REG(MIS) = RX_IRQ;
    notified(0);
    assert(REG(IMSC) & TX_IRQ);
    REG(MIS) = TX_IRQ;
    notified(0);
    assert(test_notifications == 1 && REG(IMSC) == RECEIVE_IRQS);
    REG(FR) = RX_EMPTY;
    for (unsigned i = 0; i < TCS_SERIAL_CHUNK; ++i) microkit_mr_set(i, 'A' + i);
    response = protected(1, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_WRITE), TCS_SERIAL_CHUNK));
    result = tcs_response(response);
    assert(result.status == TCS_OK && result.value == TCS_SERIAL_CHUNK);
    assert(REG(DR) == 'A' + TCS_SERIAL_CHUNK - 1);
    /* A permanently readable fake device must not make the handler loop forever. */
    REG(FR) = 0; REG(DR) = 'x'; REG(MIS) = RX_IRQ;
    notified(0);
    assert(rx.count == 64 && !rx.lost);
    response = protected(1, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_READ), 0));
    assert(microkit_msginfo_get_label(response) == TCS_SERIAL_DATA);
    assert(microkit_msginfo_get_count(response) == 2);
    assert(test_mrs[0] == TCS_SERIAL_BYTE && test_mrs[1] == 'x');
    REG(FR) = RX_EMPTY; REG(MIS) = ERROR_IRQ;
    notified(0);
    assert(rx.count == 0 && rx.lost);
    response = protected(1, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_READ), 0));
    assert(test_mrs[0] == TCS_SERIAL_LOSS && test_mrs[1] == 0);
    puts("TCS SERIAL SERVER TESTS PASS (mock registers/IPC; QEMU tests remain required)");
}
