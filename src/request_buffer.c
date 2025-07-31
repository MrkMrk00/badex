#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "support/log.h"
#include "tcp_server.h"

// 128 MiB maximum requst size
#define TCP_BUF_MAX_SIZE (128 << 20)
// 2 kB intermediate buffer for reading the request.
// If there is a lot of threads this could crash it, if too large?
#define TCP_STACK_BUF_SIZE 2048
#define TCP_BUF_FACTOR 2

size_t request_buffer_append(RequestBuffer* rb, const char* data, size_t data_size)
{
    // +1 for possible null termination
    size_t new_capacity = rb->capacity == 0 ? data_size + 1 : rb->capacity;

    while (new_capacity < rb->size + data_size) {
        new_capacity *= TCP_BUF_FACTOR;
    }

    if (new_capacity > TCP_BUF_MAX_SIZE) {
        new_capacity = TCP_BUF_MAX_SIZE;
    }

    // Cannot allocate more memory, the limit has been reached. Data would not fit
    // into the already allocated memory.
    if (new_capacity < rb->size + data_size) {
        return 0;
    }

    if (new_capacity != rb->capacity) {
        BX_INFO("reallocating request buffer (from: %lu, to: %lu)\n", rb->capacity, new_capacity);
        if (rb->data == NULL) {
            BX_RTASSERT((rb->data = malloc(new_capacity)) != NULL, "out of memory");
        } else {
            BX_RTASSERT((rb->data = realloc(rb->data, new_capacity)) != NULL, "out of memory");
        }

        rb->capacity = new_capacity;
    }

    memcpy(rb->data + rb->size, data, data_size);
    rb->size += data_size;

    return TCP_BUF_MAX_SIZE - rb->size;
}

RequestRecvStatus request_buffer_recv(RequestBuffer* rb)
{
    BX_ASSERT(rb != NULL, "expected a non-null RequestBuffer pointer\n");

    while (rb->size < TCP_BUF_MAX_SIZE) {
        char buf[TCP_STACK_BUF_SIZE] = { 0 };

        ssize_t bytes_read = recv(rb->sock_fd, buf, sizeof(buf), 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN) {
                break;
            } else {
                BX_PFATAL(NULL);
            }
        }

        if (bytes_read == 0) {
            return RB_RECV_DISCONNECTED;
        }

        size_t remaining_memory = request_buffer_append(rb, buf, bytes_read);
        if (remaining_memory == 0) {
            return RB_RECV_OOM;
        }

        if (bytes_read < TCP_STACK_BUF_SIZE) {
            break;
        }
    }

    return rb->size;
}

void request_buffer_dispose(RequestBuffer* rb)
{
    BX_INFO("disposing of RequestBuffer (fd: %d)\n", rb->sock_fd);
    BX_ASSERT(rb != NULL, "expected a non-null RequestBuffer pointer\n");

    if (rb->data != NULL) {
        free(rb->data);
    }

    rb->data = NULL;
    rb->capacity = 0;
    rb->size = 0;
}

bool request_ring_buffer_put(RequestRingBuffer* ring_buffer, RequestBuffer* req)
{
    uint8_t best_index = req->sock_fd % REQUEST_RING_BUF_MAX_INDEX;
    uint8_t cur_index = best_index;

    RequestBuffer* current;
    do {
        current = ring_buffer->entries[cur_index];
        if (current == NULL || current->sock_fd == req->sock_fd) {
            ring_buffer->entries[cur_index] = req;

            return true;
        }

        cur_index++;
    } while (cur_index != best_index);

    return false;
}

RequestBuffer* request_ring_buffer_pop(RequestRingBuffer* ring_buffer, int sock_fd)
{
    uint8_t best_index = sock_fd % REQUEST_RING_BUF_MAX_INDEX;
    uint8_t cur_index = best_index;

    RequestBuffer* current;
    do {
        current = ring_buffer->entries[cur_index];
        if (current == NULL) {
            return NULL;
        }

        // found the request buffer
        if (current->sock_fd == sock_fd) {
            ring_buffer->entries[cur_index] = NULL;

            return current;
        }

        cur_index++;
    } while (cur_index != best_index);

    return NULL;
}
