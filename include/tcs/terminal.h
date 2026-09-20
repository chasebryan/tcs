#ifndef TCS_TERMINAL_H
#define TCS_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The final byte is reserved for a terminator; parsing uses an explicit span. */
#define TCS_LINE_CAPACITY 128u

enum tcs_line_event {
    TCS_LINE_NONE, TCS_LINE_READY, TCS_LINE_REJECTED, TCS_LINE_CANCELLED
};

struct tcs_line {
    char bytes[TCS_LINE_CAPACITY];
    size_t length;
    bool rejected;
    bool previous_cr;
    bool completed;
};

enum tcs_command_kind {
    TCS_CMD_EMPTY, TCS_CMD_HELP, TCS_CMD_VERSION, TCS_CMD_STATUS, TCS_CMD_READ,
    TCS_CMD_INVALID
};

struct tcs_command {
    enum tcs_command_kind kind;
    uint64_t generation;
};

/* At most four generated display bytes plus NUL; never raw control input. */
struct tcs_line_feedback {
    enum tcs_line_event event;
    char echo[5];
};
struct tcs_line_feedback tcs_terminal_input(struct tcs_line *line, uint8_t byte);

/* After READY, bytes/length remain valid until the next input byte. A rejected
 * line stays rejected until its delimiter or Ctrl-C, even after backspaces.
 * The caller must mark UART/ring-buffer byte loss using tcs_line_discard(). */
enum tcs_line_event tcs_line_feed(struct tcs_line *line, uint8_t byte);
void tcs_line_discard(struct tcs_line *line);
struct tcs_command tcs_command_parse(const char *bytes, size_t length);

#endif
