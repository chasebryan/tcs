#include "tcs/ipc.h"
#include "tcs/serial.h"

/* QEMU virt PL011 only. Set by the checked system description. */
uintptr_t uart_base_vaddr;
static struct tcs_serial_rx rx;
#define REG(offset) (*(volatile uint32_t *)(uart_base_vaddr + (offset)))
#define DR 0x00u
#define ECR 0x04u
#define FR 0x18u
#define IBRD 0x24u
#define FBRD 0x28u
#define LCRH 0x2cu
#define CR 0x30u
#define IFLS 0x34u
#define IMSC 0x38u
#define MIS 0x40u
#define ICR 0x44u
#define DMACR 0x48u
#define RX_EMPTY (1u << 4)
#define TX_FULL (1u << 5)
#define RX_IRQ (1u << 4)
#define TX_IRQ (1u << 5)
#define RT_IRQ (1u << 6)
#define ERROR_IRQ (15u << 7)
#define RECEIVE_IRQS (RX_IRQ | RT_IRQ | ERROR_IRQ)
#define UART_IRQ_CHANNEL 0u
#define TERMINAL_CHANNEL 1u

void init(void)
{
    REG(IMSC) = 0;
    REG(CR) = 0;
    REG(DMACR) = 0;
    REG(ICR) = 0x7ff;
    REG(ECR) = 0;
    REG(IBRD) = 13; /* 24 MHz / (16 * 115200), rounded fractional divisor. */
    REG(FBRD) = 1;
    REG(LCRH) = (3u << 5) | (1u << 4); /* 8N1, FIFO enabled. */
    REG(IFLS) = 0; /* Lowest RX/TX thresholds. */
    REG(CR) = (1u << 9) | (1u << 8) | 1u;
    REG(IMSC) = RECEIVE_IRQS;
    microkit_irq_ack(UART_IRQ_CHANNEL);
}

void notified(microkit_channel ch)
{
    if (ch != UART_IRQ_CHANNEL)
        return;
    uint32_t pending = REG(MIS);
    if (pending & ERROR_IRQ) {
        tcs_serial_loss(&rx);
        REG(ECR) = 0;
    }
    /* Bound work even if an external sender continuously fills the FIFO. */
    for (unsigned i = 0; i < 64 && !(REG(FR) & RX_EMPTY); ++i) {
        uint32_t data = REG(DR);
        if (data & 0xf00u) {
            tcs_serial_loss(&rx);
            REG(ECR) = 0;
        } else {
            tcs_serial_receive(&rx, (uint8_t)data);
        }
    }
    /* Do not clear RX level while unread bytes remain. */
    REG(ICR) = RT_IRQ | ERROR_IRQ | (pending & TX_IRQ);
    /* An unrelated receive IRQ must not cancel a pending TX-space wakeup. */
    if (pending & TX_IRQ)
        REG(IMSC) = RECEIVE_IRQS;
    if (rx.count || rx.lost || (pending & TX_IRQ))
        microkit_notify(TERMINAL_CHANNEL);
    microkit_irq_ack(UART_IRQ_CHANNEL);
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    if (ch != TERMINAL_CHANNEL)
        return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (tcs_message(msg, TCS_SERIAL_READ, 0)) {
        uint8_t byte;
        unsigned flags = tcs_serial_take(&rx, &byte);
        if (rx.count)
            microkit_notify(TERMINAL_CHANNEL);
        microkit_mr_set(0, flags);
        microkit_mr_set(1, byte);
        return microkit_msginfo_new(TCS_SERIAL_DATA, 2);
    }
    uint64_t count = microkit_msginfo_get_count(msg);
    if (microkit_msginfo_get_label(msg) != TCS_LABEL(TCS_SERIAL_WRITE) ||
        count == 0 || count > TCS_SERIAL_CHUNK)
        return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    for (uint64_t i = 0; i < count; ++i)
        if (microkit_mr_get(i) > UINT8_MAX)
            return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    uint64_t sent = 0;
    while (sent < count && !(REG(FR) & TX_FULL)) {
        REG(DR) = (uint32_t)microkit_mr_get(sent);
        ++sent;
    }
    if (sent < count)
        REG(IMSC) = RECEIVE_IRQS | TX_IRQ;
    if (sent)
        microkit_notify(TERMINAL_CHANNEL);
    return tcs_reply((struct tcs_result){TCS_OK, sent});
}
