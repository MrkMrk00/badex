#ifndef RESP_H
#define RESP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "../support/structures.h"

#define RESP_TYPES                                                                                                     \
    X(SIMPLE_STRING, '+')                                                                                              \
    X(SIMPLE_ERROR, '-')                                                                                               \
    X(INTEGER, ':')                                                                                                    \
    X(BULK_STRING, '$')                                                                                                \
    X(ARRAY, '*')

typedef enum
{
#define X(name, char) RESP_##name = char,
    RESP_TYPES
#undef X
      RESP_INVALID,
} RespDataType;

typedef struct
{
    char name[24];
    char* args;
    uint8_t args_count;
} RespCommand;

enum
{
    RESPERR_INVALID_LENGTH = -(1 << 0),
};

ssize_t resp_try_parse(RespCommand* cmd, StringBuilder* sb, const char* source, size_t source_len);

#endif // !RESP_H
