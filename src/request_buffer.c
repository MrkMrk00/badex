#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "log.h"
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

RequestRecvStatus request_buffer_recv(RequestBuffer* rb, int sock_fd)
{
    while (rb->size < TCP_BUF_MAX_SIZE) {
        char buf[TCP_STACK_BUF_SIZE] = { 0 };

        ssize_t bytes_read = recv(sock_fd, buf, sizeof(buf), 0);
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
    free(rb->data);

    rb->data = NULL;
    rb->capacity = 0;
    rb->size = 0;
}

// #define REQUEST_BUFFER_SIZE UINT8_MAX
// static RequestBuffer request_ringbuf[REQUEST_BUFFER_SIZE + 1] = { 0 };
// static uint8_t ringbuf_start = 0;

// static int find_request_buffer(int sock_fd)
// {
//     int best_bet = sock_fd % REQUEST_BUFFER_SIZE;
//     if (request_ringbuf[best_bet].sock_fd == sock_fd) {
//         return best_bet;
//     }
//
//     // This will be kinda slow, but it should not happen that often?
//     for (uint8_t i = ringbuf_start; i != ringbuf_start - 1; ++i) {
//         if (request_ringbuf[i].sock_fd == sock_fd) {
//             return i;
//         }
//     }
//
//     return -1;
// }
