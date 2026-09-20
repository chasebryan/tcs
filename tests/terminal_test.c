#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tcs/terminal.h"

static struct tcs_command parse(const char *text)
{
    return tcs_command_parse(text, strlen(text));
}

static void command_contract(void)
{
    assert(parse("").kind == TCS_CMD_EMPTY);
    assert(tcs_command_parse(NULL, 0).kind == TCS_CMD_EMPTY);
    assert(tcs_command_parse(NULL, 1).kind == TCS_CMD_INVALID);
    assert(parse(" \t ").kind == TCS_CMD_EMPTY);
    assert(parse("help").kind == TCS_CMD_HELP);
    assert(parse(" version \t").kind == TCS_CMD_VERSION);
    assert(parse("status").kind == TCS_CMD_STATUS);
    assert(parse("read 1").generation == 1);
    assert(parse(" \tread\t18446744073709551615  ").generation == UINT64_MAX);
    const char *invalid[] = {
        "grant 1", "revoke 1", "quarantine 1", "restore 1", "admin", "sudo",
        "read", "read 0", "read -1", "read +1", "read 0x1", "read 1 2",
        "read 18446744073709551616", "read 999999999999999999999999",
        "status 1", "help;", "help\n", "HELP", "read 1;grant 1"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        struct tcs_command command = parse(invalid[i]);
        assert(command.kind == TCS_CMD_INVALID && command.generation == 0);
    }
    const char nul[] = {'h', 'e', 'l', 'p', 0, 'x'};
    assert(tcs_command_parse(nul, sizeof nul).kind == TCS_CMD_INVALID);
    const char unterminated[] = {'h', 'e', 'l', 'p'};
    assert(tcs_command_parse(unterminated, sizeof unterminated).kind == TCS_CMD_HELP);
    char long_line[TCS_LINE_CAPACITY];
    memset(long_line, ' ', sizeof long_line);
    assert(tcs_command_parse(long_line, sizeof long_line).kind == TCS_CMD_INVALID);
    puts("PASS read-only command allowlist, exact arguments, and integer bounds");
}

static enum tcs_line_event feed(struct tcs_line *line, const char *text)
{
    enum tcs_line_event event = TCS_LINE_NONE;
    for (size_t i = 0; text[i] != '\0'; ++i)
        event = tcs_line_feed(line, (uint8_t)text[i]);
    return event;
}

static void editing_and_recovery(void)
{
    struct tcs_line line = {0};
    assert(feed(&line, "helx\bp\r") == TCS_LINE_READY);
    assert(tcs_command_parse(line.bytes, line.length).kind == TCS_CMD_HELP);
    assert(tcs_line_feed(&line, '\n') == TCS_LINE_NONE);
    assert(feed(&line, "version\n") == TCS_LINE_READY);
    assert(tcs_command_parse(line.bytes, line.length).kind == TCS_CMD_VERSION);
    assert(feed(&line, "help\177\177\177\177\177status\n") == TCS_LINE_READY);
    assert(tcs_command_parse(line.bytes, line.length).kind == TCS_CMD_STATUS);
    for (size_t i = 0; i < TCS_LINE_CAPACITY; ++i)
        assert(tcs_line_feed(&line, 'x') == TCS_LINE_NONE);
    assert(feed(&line, "\b\bhelp\n") == TCS_LINE_REJECTED);
    assert(feed(&line, "help\n") == TCS_LINE_READY);
    assert(feed(&line, "help\033[31m\n") == TCS_LINE_REJECTED);
    feed(&line, "read 1");
    tcs_line_discard(&line); /* Simulate a dropped UART byte. */
    assert(feed(&line, "\bhelp\n") == TCS_LINE_REJECTED);
    feed(&line, "read 1");
    assert(tcs_line_feed(&line, 0) == TCS_LINE_NONE);
    assert(tcs_line_feed(&line, '\n') == TCS_LINE_REJECTED);
    assert(feed(&line, "bad\003") == TCS_LINE_CANCELLED);
    assert(feed(&line, "help\n") == TCS_LINE_READY);
    tcs_line_discard(&line);
    assert(tcs_line_feed(&line, 3) == TCS_LINE_CANCELLED);
    assert(feed(&line, "read 1\r") == TCS_LINE_READY);
    assert(tcs_line_feed(&line, '\n') == TCS_LINE_NONE);
    feed(&line, "help");
    for (size_t i = 4; i < TCS_LINE_CAPACITY - 1; ++i)
        tcs_line_feed(&line, ' ');
    assert(tcs_line_feed(&line, '\n') == TCS_LINE_READY);
    assert(tcs_command_parse(line.bytes, line.length).kind == TCS_CMD_HELP);
    feed(&line, "help");
    for (size_t i = 4; i < TCS_LINE_CAPACITY; ++i)
        tcs_line_feed(&line, ' ');
    assert(tcs_line_feed(&line, '\n') == TCS_LINE_REJECTED);
    puts("PASS line editing, CRLF, overflow/control-byte rejection, and recovery");
}

static void byte_sequences(void)
{
    struct tcs_line line = {0};
    uint64_t random = UINT64_C(0x544353);
    for (unsigned i = 0; i < 100000; ++i) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        enum tcs_line_event event = tcs_line_feed(&line, (uint8_t)random);
        assert(line.length < TCS_LINE_CAPACITY);
        assert(line.bytes[line.length] == '\0');
        if (event == TCS_LINE_READY) {
            assert(!line.rejected);
            struct tcs_command command = tcs_command_parse(line.bytes, line.length);
            assert((unsigned)command.kind <= (unsigned)TCS_CMD_INVALID);
            if (command.kind != TCS_CMD_READ)
                assert(command.generation == 0);
        }
    }
    puts("PASS 100000 deterministic input bytes preserve line-buffer invariants");
}

int main(void)
{
    command_contract();
    editing_and_recovery();
    byte_sequences();
    puts("TCS TERMINAL CORE TESTS PASS");
    return 0;
}
