#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

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
    memcpy(sb->memory + sb->size, string, len);
    sb->size += len;
}

void sb_append(StringBuilder* sb, const char* string)
{
    sb_append_slice(sb, string, strlen(string));
}

void sb_dispose(StringBuilder* sb)
{
    BX_ASSERT(sb != NULL, "expected a non-NULL pointer to StringBuilder\n");

    if (sb->memory != NULL && sb->capacity > 0) {
        free(sb->memory);
    }
}

void sb_shift(StringBuilder* sb, size_t by)
{
    if (by >= sb->size) {
        sb->size = 0;

        return;
    }

    size_t size_to_copy = sb->size - by;
    memcpy(sb->memory, sb->memory + by, size_to_copy);

    sb->size = size_to_copy;
}

ssize_t sb_recv(StringBuilder* sb, int sock_fd)
{
#define STACK_BUF_SIZE 2048
#define RB_MAX_SIZE (64 << 20)
#define STATUS_DISCONNECTED 0

    BX_ASSERT(sb != NULL, "expected a non-null StringBuilder\n");
    char buf[STACK_BUF_SIZE] = { 0 };

    // receive until the buffer isn't full
    while (sb->size < (RB_MAX_SIZE - STACK_BUF_SIZE)) {
        ssize_t bytes_read = recv(sock_fd, buf, sizeof(buf), 0);
        if (bytes_read == -1) {
            switch (errno) {
                case EAGAIN: // no more messages to be read
                    return sb->size;

                case ECONNRESET: // the client has disconnected
                    return STATUS_DISCONNECTED;

                case EINTR: // interrupted -> try again
                    continue;

                default:
                    BX_PFATAL(NULL);
            }
        }

        if (bytes_read == 0) {
            return STATUS_DISCONNECTED;
        }

        sb_append_slice(sb, buf, bytes_read);

        if (bytes_read < STACK_BUF_SIZE) {
            break;
        }
    }

    return sb->size;
}
