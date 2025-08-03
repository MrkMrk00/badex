#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "support/log.h"
#include "tcp_server.h"

// In `MiB`s
#define RB_MAX_SIZE (16 << 20)
// In bytes
#define RB_INIT_SIZE 256
#define RB_GROW_FACTOR 2
#define RB_MAX_KEEP_SIZE 2048
// 2 kB intermediate buffer for reading the request.
// If there is a lot of threads this could crash it, if too large?
#define STACK_BUF_SIZE 2048

ssize_t request_buffer_append(RequestBuffer* rb, const char* data, size_t data_size)
{
    size_t new_capacity = RB_INIT_SIZE;
    while (new_capacity < (rb->size + data_size + 1)) {
        new_capacity *= RB_GROW_FACTOR;
    }

    if (new_capacity > RB_MAX_SIZE) {
        new_capacity = RB_MAX_SIZE;
    }

    // Cannot allocate more memory, the limit has been reached. Data would not fit
    // into the already allocated memory.
    if (new_capacity < (rb->size + data_size)) {
        return -1;
    }

    if (new_capacity > rb->capacity) {
        BX_INFO("reallocating request buffer (from: %lu, to: %lu)\n", rb->capacity, new_capacity);
        if (rb->data == NULL) {
            BX_RTASSERT((rb->data = malloc(new_capacity)) != NULL, "out of memory\n");
        } else {
            BX_RTASSERT((rb->data = realloc(rb->data, new_capacity)) != NULL, "out of memory\n");
        }

        rb->capacity = new_capacity;
    }

    memcpy(rb->data + rb->size, data, data_size);
    rb->size += data_size;

    return RB_MAX_SIZE - rb->size;
}

ssize_t request_buffer_recv(RequestBuffer* rb)
{
#define STATUS_DISCONNECTED 0

    BX_ASSERT(rb != NULL, "expected a non-null RequestBuffer pointer\n");
    char buf[STACK_BUF_SIZE] = { 0 };

    // receive until the buffer isn't full
    while (rb->size < (RB_MAX_SIZE - STACK_BUF_SIZE)) {
        ssize_t bytes_read = recv(rb->sock_fd, buf, sizeof(buf), 0);
        if (bytes_read == -1) {
            switch (errno) {
                case EAGAIN: // no more messages to be read
                    return rb->size;

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

        BX_RTASSERT(request_buffer_append(rb, buf, bytes_read) >= 0,
                    "!this should never happen!, no more space in the request buffer\n");

        if (bytes_read < STACK_BUF_SIZE) {
            break;
        }
    }

    return rb->size;
}

void request_buffer_dispose(RequestBuffer* rb)
{
    rb->sock_fd = -1;
    rb->size = 0;

    // keep small buffers, to not have a lot of small allocations on every request
    if (rb->capacity > RB_MAX_KEEP_SIZE) {
        BX_INFO("disposing of RequestBuffer (fd: %d)\n", rb->sock_fd);
        BX_RTASSERT(rb->data != NULL, "rb->capacity is > 0, but rb->data is NULL (WTF?!)\n");

        free(rb->data);
        rb->data = NULL;
        rb->capacity = 0;
    }
}

RequestBuffer* request_ring_buffer_pop(RequestRingBuffer* ring_buffer, int sock_fd)
{
    uint8_t best_index = sock_fd % REQUEST_RING_BUF_MAX_INDEX;
    uint8_t cur_index = best_index;

    RequestBuffer* current;
    do {
        // return the found request buffer (matching FD), or the first available one
        current = &ring_buffer->entries[cur_index];
        if (current->sock_fd == sock_fd) {
            return current;
        } else if (current->sock_fd <= 0) {
            current->sock_fd = sock_fd;
            current->size = 0;

            return current;
        }

        cur_index++;
    } while (cur_index != best_index);

    return NULL;
}
