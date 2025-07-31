#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/log.h"
#include "../support/structures.h"
#include "./resp.h"

const char* GET = "GET";
const char* SET = "SET";

typedef struct
{
    const char* source;
    size_t source_len;
    size_t offset;
} RespParser;

static inline char advance(RespParser* p);
static inline char peek(RespParser* p);
static inline char peek_n(RespParser* p, size_t n);
static inline bool match(RespParser* p, char ch);
static inline bool match_rn(RespParser* p);

static long read_long(RespParser* p);
static void read_string(RespParser* p, StringBuilder* sb);

/**
 * Makes quite a lot of allocations :/ (for now, will fix later)
 */
ssize_t resp_try_parse(RespCommand* cmd, StringBuilder* sb, const char* source, size_t source_len)
{
    RespParser p = { 0 };
    p.source = source;
    p.source_len = source_len;
    sb->size = 0;

    // invalid command (it should start as array)
    if (!match(&p, RESP_ARRAY)) {
        return 0;
    }

    // Is valid number? also: command cannot be a null array (*-1\r\r)
    long cmd_len = read_long(&p);
    if (cmd_len > UINT8_MAX || cmd_len < 1) {
        return 0;
    }

    if (!match_rn(&p)) {
        return 0;
    }

    // parse command
    //    1) Has to be a string
    if (!match(&p, RESP_BULK_STRING)) {
        return 0;
    }

    long expected_str_len = read_long(&p);
    if (expected_str_len <= 0) {
        BX_INFO("invalid bulk string length (%ld)\n", expected_str_len);

        return 0;
    }
    if (!match_rn(&p)) {
        return 0;
    }

    // Read command name
    sb->size = 0;
    sb_ensure_capacity(sb, expected_str_len);
    read_string(&p, sb);

    if (!match_rn(&p)) {
        return 0;
    }

    if (sb->size > sizeof(cmd->name) || sb->size != expected_str_len) {
        return 0;
    }
    sb_putchar(sb, '\0');

    // assign the command name into the struct
    memcpy(cmd->name, sb->memory, sb->size);

    cmd->args_count = cmd_len - 1;

    // parse the arguments
    sb->size = 0;
    size_t last_size = 0;
    for (int i = 0; i < cmd->args_count; ++i) {
        if (!match(&p, RESP_BULK_STRING)) {
            BX_RTASSERT(1 == 0, "non-string arguments not implemented\n");
        }

        expected_str_len = read_long(&p);
        if (expected_str_len <= 0) {
            return 0;
        }

        if (!match_rn(&p)) {
            return 0;
        }

        sb_ensure_capacity(sb, expected_str_len);
        read_string(&p, sb);
        if ((sb->size - last_size) != expected_str_len || !match_rn(&p)) {
            return 0;
        }

        sb_putchar(sb, '\0');
        last_size = sb->size;
    }

    BX_RTASSERT((cmd->args = malloc(sb->size)) != NULL, "out of memory\n");
    memcpy(cmd->args, sb->memory, sb->size);

    return p.offset;
}

#define IS_DIGIT(char) (strchr("1234567890", (char)) != NULL)

// returns LONG_MIN on failure
static long read_long(RespParser* p)
{
    char ch;
    // should not be longer that INT_MAX :)
    char cmd_len_buf[12] = { 0 };
    int lenbuf_index = 0;

    // read number until \r\n (or end of buffer)
    while (lenbuf_index < sizeof(cmd_len_buf) - 1 && (ch = peek(p)) != EOF && IS_DIGIT(ch)) {
        cmd_len_buf[lenbuf_index++] = advance(p);
    }

    char* endptr;
    long cmd_len = strtol(cmd_len_buf, &endptr, 10);

    if (*endptr != '\0') {
        return LONG_MIN;
    }

    return cmd_len;
}

static void read_string(RespParser* p, StringBuilder* sb)
{
    if (peek_n(p, 1) == GET[0] && peek_n(p, 2) == GET[1] && peek_n(p, 3) == GET[2]) {
        p->offset += 3;
        sb_append_slice(sb, GET, 3);

        return;
    } else if (peek_n(p, 1) == SET[0] && peek_n(p, 2) == SET[1] && peek_n(p, 3) == SET[2]) {
        p->offset += 3;
        sb_append_slice(sb, SET, 3);

        return;
    }

    while (peek(p) != '\r' && peek(p) != EOF) {
        sb_putchar(sb, advance(p));
    }
}

static inline char advance(RespParser* p)
{
    if (p->offset >= p->source_len) {
        return EOF;
    }

    return p->source[p->offset++];
}

static inline char peek_n(RespParser* p, size_t n)
{
    if ((p->offset + n - 1) >= p->source_len) {
        return EOF;
    }

    return p->source[p->offset + n - 1];
}

static inline char peek(RespParser* p)
{
    if (p->offset >= p->source_len) {
        return EOF;
    }

    return p->source[p->offset];
}

static inline bool match(RespParser* p, char ch)
{
    if (peek(p) == ch) {
        advance(p);

        return true;
    }

    return false;
}

static inline bool match_rn(RespParser* p)
{
    return match(p, '\r') && match(p, '\n');
}
