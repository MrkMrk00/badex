#include <stdlib.h>
#include <string.h>

#include "./structures.h"
#include "log.h"

#define SB_GROW_FACTOR 2

void sb_ensure_capacity(StringBuilder* sb, size_t requested_capacity)
{
    if (sb->capacity == 0) {
        sb->capacity = requested_capacity * SB_GROW_FACTOR + 1;
        BX_RTASSERT((sb->memory = malloc(sb->capacity)) != NULL, "out of memory\n");

        return;
    }

    size_t original_capacity = sb->capacity;
    while (sb->capacity < sb->size + requested_capacity + 1) {
        sb->capacity *= SB_GROW_FACTOR;
    }

    if (original_capacity != sb->capacity) {
        BX_RTASSERT((sb->memory = realloc(sb->memory, sb->capacity)) != NULL, "out of memory\n");
    }
}

void sb_putchar(StringBuilder* sb, char ch)
{
    if (sb->capacity == 0) {
        sb_ensure_capacity(sb, 8);
    } else {
        sb_ensure_capacity(sb, 1);
    }

    sb->memory[sb->size++] = ch;
}

void sb_append_slice(StringBuilder* sb, const char* string, size_t len)
{
    sb_ensure_capacity(sb, len);
    memcpy(sb->memory, string, len);
    sb->size += len;
}

void sb_append(StringBuilder* sb, const char* string)
{
    sb_append_slice(sb, string, strlen(string));
}

void sb_dispose(StringBuilder* sb)
{
    BX_ASSERT(sb != NULL, "expected a non-NULL pointer to StringBuilder\n");

    free(sb->memory);
    memset(sb, 0, sizeof(StringBuilder));
}
