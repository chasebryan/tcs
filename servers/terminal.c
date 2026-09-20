#include "tcs/ipc.h"
#include "tcs/serial.h"
#include "tcs/terminal.h"
#ifdef TCS_SIGNED_INPUT
#include "tcs/admin_ipc.h"
#include "tcs/launch_profile.h"
#include "tcs/signed_input.h"
static struct tcs_signed_input signed_input;
static bool packet_mode;
#endif

#define SERIAL_CHANNEL 0u
#define CLIENT_CHANNEL 1u
static struct tcs_line line;
static enum tcs_line_event pending_event;
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
#ifdef TCS_SIGNED_INPUT
    static const char submit[] = "submit";
    bool match = line.length == sizeof submit - 1;
    for (size_t i = 0; match && i < sizeof submit - 1; ++i) match = line.bytes[i] == submit[i];
    if (match) {
        tcs_signed_begin(&signed_input, line.previous_cr); packet_mode = true;
        append("Enter exactly 384 lowercase hex digits; Enter submits, Ctrl-C cancels.\r\npacket> ");
        return;
    }
#endif
    struct tcs_command cmd = tcs_command_parse(line.bytes, line.length);
    switch (cmd.kind) {
    case TCS_CMD_EMPTY: break;
    case TCS_CMD_HELP:
#ifdef TCS_SIGNED_INPUT
        append("help | version | status | read <generation> | submit\r\nSigned packets only; no plaintext administration.\r\n");
#else
        append("help | version | status | read <generation>\r\nNo administration commands.\r\n");
#endif
        break;
    case TCS_CMD_VERSION:
#ifdef TCS_SIGNED_INPUT
        append("TCS 0.2-dev / " TCS_PROFILE_NAME " / " TCS_LAUNCH_NAME " signed terminal\r\n");
#else
        append("TCS 0.2-dev / " TCS_PROFILE_NAME " / read-only terminal\r\n");
#endif
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

#ifdef TCS_SIGNED_INPUT
static void finish_packet(enum tcs_line_event event)
{
    if (event == TCS_LINE_READY) {
        struct tcs_admin_command command = tcs_signed_command(signed_input.packet);
        tcs_words_from_bytes(signed_input.packet, sizeof signed_input.packet);
        microkit_msginfo reply = microkit_ppcall(2, microkit_msginfo_new(TCS_ADMIN_SUBMIT, 24));
        struct tcs_admin_receipt receipt;
        if (tcs_admin_receipt_decode(reply, &receipt) && tcs_admin_receipt_valid(command, receipt)) {
            append("ADMIN seq="); decimal(receipt.command.sequence); append(" decision="); decimal(receipt.decision);
            append(" audit="); decimal(receipt.audit_ok); append(" applied="); decimal(receipt.applied);
            append(" state="); decimal(receipt.post.state); append(" generation="); decimal(receipt.post.generation); append("\r\n");
        } else if (microkit_msginfo_get_label(reply) == TCS_ADMIN_ADMISSION && microkit_msginfo_get_count(reply) == 1 &&
                   microkit_mr_get(0) > TCS_ADMIN_ACCEPTED && microkit_mr_get(0) <= TCS_ADMIN_BAD_COMPLETION) {
            if (microkit_mr_get(0) == TCS_ADMIN_BAD_COMPLETION)
                append("ADMIN UNCERTAIN; do not retry or advance sequence\r\n");
            else {
                append("ADMIN REJECTED status="); decimal(microkit_mr_get(0)); append("\r\n");
            }
        } else append("ADMIN UNAVAILABLE; do not retry an uncertain request\r\n");
    } else if (event == TCS_LINE_CANCELLED) append("CANCELLED\r\n");
    else append("ERROR discarded signed packet\r\n");
    line = (struct tcs_line){0}; line.previous_cr = signed_input.previous_cr;
    tcs_signed_discard(&signed_input); packet_mode = false; append("tcs> ");
}
#endif

static void service(void)
{
    /* Never wait for UART readiness or consume an unbounded input stream. */
    for (unsigned step = 0; step < 32 && !failed; ++step) {
        if (!flush())
            return; /* The driver notifies after progress or TX readiness. */
        if (pending_event != TCS_LINE_NONE) {
            enum tcs_line_event event = pending_event;
            pending_event = TCS_LINE_NONE;
            /* Finish echo before nested IPC can emit independent debug output. */
#ifdef TCS_SIGNED_INPUT
            if (packet_mode) { finish_packet(event); continue; }
#endif
            if (event == TCS_LINE_READY)
                execute();
            else if (event == TCS_LINE_REJECTED)
                append("ERROR discarded input line\r\ntcs> ");
            else if (event == TCS_LINE_CANCELLED)
                append("CANCELLED\r\ntcs> ");
            continue;
        }
        microkit_msginfo msg = microkit_ppcall(SERIAL_CHANNEL,
            microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_READ), 0));
        if (microkit_msginfo_get_label(msg) != TCS_SERIAL_DATA ||
            microkit_msginfo_get_count(msg) != 2) { failed = true; break; }
        uint64_t flags = microkit_mr_get(0), byte = microkit_mr_get(1);
        if (flags > (TCS_SERIAL_BYTE | TCS_SERIAL_LOSS) || byte > UINT8_MAX ||
            (!(flags & TCS_SERIAL_BYTE) && byte != 0)) { failed = true; break; }
        if (flags & TCS_SERIAL_LOSS) {
#ifdef TCS_SIGNED_INPUT
            if (packet_mode) tcs_signed_discard(&signed_input);
#endif
            tcs_line_discard(&line);
            append("\r\nINPUT LOST; discard until Enter or Ctrl-C\r\n");
        }
        if (!(flags & TCS_SERIAL_BYTE)) {
            (void)flush();
            return;
        }
        struct tcs_line_feedback feedback;
#ifdef TCS_SIGNED_INPUT
        if (packet_mode) feedback = tcs_signed_feed(&signed_input, (uint8_t)byte);
        else
#endif
        feedback = tcs_terminal_input(&line, (uint8_t)byte);
        append(feedback.echo);
        pending_event = feedback.event;
    }
    if (!failed)
        (void)flush(); /* Ensures a final command's output gets a wakeup. */
}

void init(void)
{
#ifdef TCS_SIGNED_INPUT
    append("TCS SIGNED TERMINAL READY (" TCS_PROFILE_NAME ", " TCS_LAUNCH_NAME ")\r\ntcs> ");
#else
    append("TCS TERMINAL READY (" TCS_PROFILE_NAME ", read-only)\r\ntcs> ");
#endif
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
