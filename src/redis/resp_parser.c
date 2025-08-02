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
static inline bool match(RespParser* p, char ch);
static inline bool match_rn(RespParser* p);
static inline bool ensure_source_len(RespParser* p, size_t token_len);

static long read_long(RespParser* p);
static void read_string(RespParser* p, StringBuilder* sb, size_t expected_len);

/**
 * Makes quite a lot of allocations :/ (for now, will fix later)
 * And is ugly, but good for now.
 */
ssize_t resp_try_parse(RespCommand* cmd, StringBuilder* sb, const char* source, size_t source_len)
{
    RespParser p = { 0 };
    p.source = source;
    p.source_len = source_len;
    sb->size = 0;

    char type = advance(&p);
    long cmd_len = read_long(&p);

    // incomplete command
    if (!match_rn(&p)) {
        return 0;
    }
    // invalid command
    if (type != RESP_ARRAY || cmd_len <= 0 || cmd_len > UINT8_MAX) {
        return -1;
    }

    // === Command name

    char cmd_type = advance(&p);
    long expected_str_len = read_long(&p);
    if (!match_rn(&p)) {
        return 0;
    }

    // invalid (shortest command = 3)
    if (cmd_type != RESP_BULK_STRING || expected_str_len < 3) {
        return -1;
    }

    // incomplete packet
    if (!ensure_source_len(&p, expected_str_len)) {
        return 0;
    }

    // Read command name
    sb->size = 0;
    read_string(&p, sb, expected_str_len);

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
            return 0;
        }

        expected_str_len = read_long(&p);
        if (expected_str_len <= 0) {
            return 0;
        }

        if (!match_rn(&p)) {
            return 0;
        }

        sb_ensure_capacity(sb, expected_str_len);
        read_string(&p, sb, expected_str_len);
        if ((sb->size - last_size) != expected_str_len || !match_rn(&p)) {
            return 0;
        }

        sb_putchar(sb, '\0');
        last_size = sb->size;
    }

    if (cmd->args_count > 0) {
        BX_RTASSERT((cmd->args = malloc(sb->size)) != NULL, "out of memory\n");
        memcpy(cmd->args, sb->memory, sb->size);
    }

    return p.offset + 1;
}

#define IS_DIGIT(ch) (strchr("1234567890", (ch)) != NULL)

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

#define COMPARE_3(parser, keyword)                                                                                     \
    ((parser)->source[p->offset] == (keyword)[0] && (parser)->source[p->offset + 1] == (keyword)[1] &&                 \
     (parser)->source[p->offset + 2] == (keyword)[2])

static void read_string(RespParser* p, StringBuilder* sb, size_t expeceted_len)
{
    if (!ensure_source_len(p, expeceted_len)) {
        return;
    }

    switch (expeceted_len) {
        case 3: {
            if (COMPARE_3(p, GET))
                sb_append_slice(sb, GET, 3);
            else if (COMPARE_3(p, SET))
                sb_append_slice(sb, SET, 3);
            else
                break;

            p->offset += 3;
        } break;
    }

    sb_ensure_capacity(sb, expeceted_len);
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

static inline bool ensure_source_len(RespParser* p, size_t token_len)
{
    // The starting char of the parsed token, is the current char -> thus - 1

    return p->source_len > (p->offset + token_len - 1);
}
