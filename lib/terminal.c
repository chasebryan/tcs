#include "tcs/terminal.h"

static void reset_line(struct tcs_line *line)
{
    line->length = 0;
    line->bytes[0] = '\0';
    line->rejected = false;
    line->completed = false;
}

void tcs_line_discard(struct tcs_line *line)
{
    reset_line(line);
    line->rejected = true;
    line->previous_cr = false;
}

enum tcs_line_event tcs_line_feed(struct tcs_line *line, uint8_t byte)
{
    if (line->completed)
        reset_line(line);
    if (byte == '\n' && line->previous_cr) {
        line->previous_cr = false;
        return TCS_LINE_NONE;
    }
    line->previous_cr = byte == '\r';
    if (byte == 3) { /* Ctrl-C cancels, but cannot execute a buffered prefix. */
        reset_line(line);
        return TCS_LINE_CANCELLED;
    }
    if (byte == '\r' || byte == '\n') {
        line->completed = true;
        return line->rejected ? TCS_LINE_REJECTED : TCS_LINE_READY;
    }
    if (line->rejected)
        return TCS_LINE_NONE;
    if (byte == 8 || byte == 127) {
        if (line->length != 0)
            line->bytes[--line->length] = '\0';
        return TCS_LINE_NONE;
    }
    if ((byte < 32 && byte != '\t') || byte > 126 ||
        line->length == TCS_LINE_CAPACITY - 1) {
        tcs_line_discard(line);
        return TCS_LINE_NONE;
    }
    line->bytes[line->length++] = (char)byte;
    line->bytes[line->length] = '\0';
    return TCS_LINE_NONE;
}

struct tcs_line_feedback tcs_terminal_input(struct tcs_line *line, uint8_t byte)
{
    size_t previous_length = line->completed ? 0 : line->length;
    struct tcs_line_feedback feedback = {tcs_line_feed(line, byte), {0}};
    if (feedback.event == TCS_LINE_READY || feedback.event == TCS_LINE_REJECTED) {
        feedback.echo[0] = '\r'; feedback.echo[1] = '\n';
    } else if (feedback.event == TCS_LINE_CANCELLED) {
        feedback.echo[0] = '^'; feedback.echo[1] = 'C';
        feedback.echo[2] = '\r'; feedback.echo[3] = '\n';
    } else if (!line->rejected && (byte == 8 || byte == 127) &&
               line->length < previous_length) {
        feedback.echo[0] = '\b'; feedback.echo[1] = ' '; feedback.echo[2] = '\b';
    } else if (!line->rejected && line->length == previous_length + 1) {
        /* A tab occupies one displayed cell, matching one-byte backspace. */
        feedback.echo[0] = byte == '\t' ? ' ' : (char)byte;
    }
    return feedback;
}

static bool space(char byte)
{
    return byte == ' ' || byte == '\t';
}

static bool word(const char *bytes, size_t length, const char *expected)
{
    size_t i = 0;
    while (i < length && expected[i] != '\0' && bytes[i] == expected[i])
        ++i;
    return i == length && expected[i] == '\0';
}

struct tcs_command tcs_command_parse(const char *bytes, size_t length)
{
    struct tcs_command invalid = {TCS_CMD_INVALID, 0};
    if (length >= TCS_LINE_CAPACITY || (bytes == NULL && length != 0))
        return invalid;
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = (uint8_t)bytes[i];
        if ((byte < 32 && byte != '\t') || byte > 126)
            return invalid;
    }
    size_t start = 0, end = length;
    while (start < end && space(bytes[start]))
        ++start;
    while (end > start && space(bytes[end - 1]))
        --end;
    if (start == end)
        return (struct tcs_command){TCS_CMD_EMPTY, 0};
    size_t split = start;
    while (split < end && !space(bytes[split]))
        ++split;
    size_t name_length = split - start;
    if (split == end) {
        if (word(bytes + start, name_length, "help"))
            return (struct tcs_command){TCS_CMD_HELP, 0};
        if (word(bytes + start, name_length, "version"))
            return (struct tcs_command){TCS_CMD_VERSION, 0};
        if (word(bytes + start, name_length, "status"))
            return (struct tcs_command){TCS_CMD_STATUS, 0};
        return invalid;
    }
    if (!word(bytes + start, name_length, "read"))
        return invalid;
    while (split < end && space(bytes[split]))
        ++split;
    uint64_t generation = 0;
    for (size_t i = split; i < end; ++i) {
        if (bytes[i] < '0' || bytes[i] > '9')
            return invalid;
        uint64_t digit = (uint64_t)(bytes[i] - '0');
        if (generation > (UINT64_MAX - digit) / 10)
            return invalid;
        generation = generation * 10 + digit;
    }
    if (generation == 0)
        return invalid;
    return (struct tcs_command){TCS_CMD_READ, generation};
}
