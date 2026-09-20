#include "tcs/ipc.h"
#include "tcs/serial.h"
#include "tcs/terminal.h"

#define SERIAL_CHANNEL 0u
#define CLIENT_CHANNEL 1u
static struct tcs_line line;
static char output[256];
static size_t output_length, output_sent;
static bool failed;

static void append(const char *text)
{
    while (*text && output_length < sizeof output)
        output[output_length++] = *text++;
    if (*text)
        failed = true; /* Internal output bound violation: never execute more input. */
}

static void decimal(uint64_t value)
{
    char digits[21];
    size_t length = 0;
    do { digits[length++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (length) {
        char digit[2] = {digits[--length], 0};
        append(digit);
    }
}

static bool flush(void)
{
    if (output_sent == output_length)
        return true;
    size_t count = output_length - output_sent;
    if (count > TCS_SERIAL_CHUNK)
        count = TCS_SERIAL_CHUNK;
    for (size_t i = 0; i < count; ++i)
        microkit_mr_set(i, (uint8_t)output[output_sent + i]);
    struct tcs_result reply = tcs_response(microkit_ppcall(SERIAL_CHANNEL,
        microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_WRITE), count)));
    if (reply.status != TCS_OK || reply.value > count) {
        failed = true;
        return false;
    }
    output_sent += reply.value;
    if (output_sent == output_length) {
        output_sent = output_length = 0;
        return true;
    }
    return false;
}

static void execute(void)
{
    struct tcs_command cmd = tcs_command_parse(line.bytes, line.length);
    switch (cmd.kind) {
    case TCS_CMD_EMPTY: break;
    case TCS_CMD_HELP:
        append("help | version | status | read <generation>\r\nNo administration commands.\r\n");
        break;
    case TCS_CMD_VERSION:
        append("TCS 0.2-dev / read-only terminal\r\n");
        break;
    case TCS_CMD_STATUS: {
        struct tcs_snapshot s = tcs_snapshot_response(microkit_ppcall(CLIENT_CHANNEL,
            microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_STATUS), 0)));
        if (s.status == TCS_OK) {
            append("SELF state=");
            append(s.state == TCS_ACTIVE ? "active" :
                   s.state == TCS_QUARANTINED ? "quarantined" : "restricted");
            append(" generation="); decimal(s.generation);
            append(" object="); decimal(s.object);
            append(" rights="); decimal(s.rights);
        } else {
            append("STATUS UNAVAILABLE status="); decimal(s.status);
        }
        append("\r\n");
        break;
    }
    case TCS_CMD_READ: {
        microkit_mr_set(0, TCS_OBJECT);
        microkit_mr_set(1, TCS_READ);
        microkit_mr_set(2, cmd.generation);
        struct tcs_result result = tcs_response(microkit_ppcall(CLIENT_CHANNEL,
            microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_READ), 3)));
        if (result.status == TCS_OK) {
            append("READ OK value="); decimal(result.value);
        } else {
            append("READ DENIED status="); decimal(result.status);
        }
        append("\r\n");
        break;
    }
    default: append("ERROR unknown or malformed command\r\n"); break;
    }
    append("tcs> ");
}

static void service(void)
{
    /* Never wait for UART readiness or consume an unbounded input stream. */
    for (unsigned step = 0; step < 32 && !failed; ++step) {
        if (!flush())
            return; /* The driver notifies after progress or TX readiness. */
        microkit_msginfo msg = microkit_ppcall(SERIAL_CHANNEL,
            microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_READ), 0));
        if (microkit_msginfo_get_label(msg) != TCS_SERIAL_DATA ||
            microkit_msginfo_get_count(msg) != 2) { failed = true; break; }
        uint64_t flags = microkit_mr_get(0), byte = microkit_mr_get(1);
        if (flags > (TCS_SERIAL_BYTE | TCS_SERIAL_LOSS) || byte > UINT8_MAX ||
            (!(flags & TCS_SERIAL_BYTE) && byte != 0)) { failed = true; break; }
        if (flags & TCS_SERIAL_LOSS)
            tcs_line_discard(&line);
        if (!(flags & TCS_SERIAL_BYTE))
            return;
        enum tcs_line_event event = tcs_line_feed(&line, (uint8_t)byte);
        if (event == TCS_LINE_READY)
            execute();
        else if (event == TCS_LINE_REJECTED)
            append("ERROR discarded input line\r\ntcs> ");
        else if (event == TCS_LINE_CANCELLED)
            append("CANCELLED\r\ntcs> ");
    }
    if (!failed)
        (void)flush(); /* Ensures a final command's output gets a wakeup. */
}

void init(void)
{
    append("TCS TERMINAL READY (read-only, no echo)\r\ntcs> ");
    service();
}

void notified(microkit_channel ch)
{
    if (ch == SERIAL_CHANNEL)
        service();
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    (void)ch; (void)msg;
    return tcs_reply((struct tcs_result){TCS_DENIED, 0});
}
