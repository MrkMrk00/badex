#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "support/log.h"
#include "support/structures.h"
#include "tcp_server.h"

#define REQUEST_QUEUE_SIZE 16

struct request_queue_t
{
    RequestQueueTask tasks[REQUEST_QUEUE_SIZE];
    size_t count;
    size_t head;
    size_t tail;

    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;

    pthread_t* threads;
    int thread_count;
    bool is_running; // the stop condition for the threads

    RequestQueueWorker worker;
    ServerContext* worker_context;
};

static RequestQueueTask request_queue_pop(RequestQueue* q);

RequestQueue* request_queue_create(int worker_thread_count)
{
    RequestQueue* q = malloc(sizeof(RequestQueue));
    BX_RTASSERT(q != NULL, "out of memory\n");

    memset(q, 0, sizeof(RequestQueue));

    BX_RTASSERT(pthread_mutex_init(&q->mutex, NULL) == 0, "\n");
    BX_RTASSERT(pthread_cond_init(&q->cond_not_empty, NULL) == 0, "\n");
    BX_RTASSERT(pthread_cond_init(&q->cond_not_full, NULL) == 0, "\n");

    q->is_running = false;
    q->thread_count = worker_thread_count;
    q->threads = malloc(sizeof(pthread_t) * q->thread_count);

    BX_RTASSERT(q->threads != NULL, "out of memory\n");

    return q;
}

#define MAX_BUFFERS 1024

typedef struct
{
    int sock_fd;
    StringBuilder req;
    StringBuilder res;
} RingBufferEntry;

typedef RingBufferEntry IoRingBuffer[MAX_BUFFERS];

static RingBufferEntry* io_ring_buffer_pop(IoRingBuffer* rb, int sock_fd)
{
    size_t optimal_index = sock_fd % MAX_BUFFERS;
    size_t current_index = optimal_index;

    RingBufferEntry* current;
    do {
        current = rb[current_index];
        if (current->sock_fd == sock_fd || current->sock_fd <= 0) {
            return current;
        }

        current_index = (current_index + 1) % MAX_BUFFERS;
    } while (optimal_index != current_index);

    return NULL;
}

// pthread main loop
static void* task_dispatcher(void* arg)
{
    // don't access anything inside of this without a mutex...
    RequestQueue* q = (RequestQueue*)arg;

    IoRingBuffer* io_ring_buffer = malloc(sizeof(IoRingBuffer));
    BX_RTASSERT(io_ring_buffer != NULL, "out of memory\n");

    while (true) {
        RequestQueueTask task = request_queue_pop(q);
        if (task.flags & CF_STOP) {
            break;
        }

        RingBufferEntry* socket_io = io_ring_buffer_pop(io_ring_buffer, task.sock_fd);
        if (socket_io == NULL) {
            BX_ERROR("RequestRingBuffer out of memory, skipping running work for fd: %d\n", task.sock_fd);

            // return task back into the queue
            request_queue_enqueue(q, task);

            continue;
        }

        ClientFlags next_flags = q->worker(q->worker_context, &socket_io->req, &socket_io->res, task);

        // Neither READ nor WRITE requested... Drain buffers and allow other FDs to use it, disconnect.
        if (next_flags == 0) {
            sb_reset(&socket_io->req);
            sb_reset(&socket_io->res);
            socket_io->sock_fd = 0;
            server_disconnect_client(q->worker_context, task.sock_fd);

            continue;
        }

        // if both drained, let other FDs use this IO buffer
        if (socket_io->req.size == 0 && socket_io->res.size == 0) {
            socket_io->sock_fd = 0;
        } else {
            socket_io->sock_fd = task.sock_fd;
        }

        // commit the flags into the event queue
        server_unblock_client(q->worker_context, task.sock_fd, next_flags);
    }

    for (int i = 0; i < MAX_BUFFERS; ++i) {
        sb_dispose(&io_ring_buffer[i]->req);
        sb_dispose(&io_ring_buffer[i]->res);
    }

    free(io_ring_buffer);
    pthread_exit(NULL);
}

void request_queue_run(RequestQueue* q, ServerContext* ctx, RequestQueueWorker worker)
{
    BX_ASSERT(q->thread_count > 0, "cannot have negative thread_count :)\n");
    BX_ASSERT(!q->is_running, "the thread pool is already running\n");

    q->worker = worker;
    q->worker_context = ctx;
    q->is_running = true;

    for (int i = 0; i < q->thread_count; ++i) {
        pthread_create(&q->threads[i], NULL, task_dispatcher, (void*)q);
    }
}

void request_queue_stop(RequestQueue* q)
{
    if (!q->is_running) {
        return;
    }

    pthread_mutex_lock(&q->mutex);
    {
        q->is_running = false;
        pthread_cond_broadcast(&q->cond_not_empty);
        pthread_cond_broadcast(&q->cond_not_full);
    }
    pthread_mutex_unlock(&q->mutex);

    for (int i = 0; i < q->thread_count; ++i) {
        pthread_join(q->threads[i], NULL);
    }
}

void request_queue_dispose(RequestQueue** q_out)
{
    BX_ASSERT(q_out != NULL && (*q_out) != NULL, "expected non-NULL handle to RequestQueue\n");
    RequestQueue* q = *q_out;

    if (q->is_running) {
        request_queue_stop(q);
    }

    BX_RTASSERT(pthread_mutex_destroy(&q->mutex) == 0, "\n");
    BX_RTASSERT(pthread_cond_destroy(&q->cond_not_empty) == 0, "\n");
    BX_RTASSERT(pthread_cond_destroy(&q->cond_not_full) == 0, "\n");

    free(q->threads);
    free(q);

    (*q_out) = NULL;
}

void request_queue_enqueue(RequestQueue* q, RequestQueueTask task)
{
    pthread_mutex_lock(&q->mutex);
    {
        // wait for the queue to not be full
        while (q->count >= REQUEST_QUEUE_SIZE) {
            pthread_cond_wait(&q->cond_not_full, &q->mutex);
        }

        // append to the end of the ring buffer
        q->tasks[q->tail] = task;
        q->tail = (q->tail + 1) % REQUEST_QUEUE_SIZE;
        q->count++;

        pthread_cond_signal(&q->cond_not_empty);
    }
    pthread_mutex_unlock(&q->mutex);
}

static RequestQueueTask request_queue_pop(RequestQueue* q)
{
    RequestQueueTask ret;

    pthread_mutex_lock(&q->mutex);
    {
        while (q->count <= 0) {
            pthread_cond_wait(&q->cond_not_empty, &q->mutex);
        }

        if (q->is_running) {
            ret = q->tasks[q->head];
            q->head = (q->head + 1) % REQUEST_QUEUE_SIZE;
            q->count--;

            pthread_cond_signal(&q->cond_not_full);
        } else {
            ret = (RequestQueueTask){
                .sock_fd = -1,
                .flags = CF_STOP,
            };
        }
    }
    pthread_mutex_unlock(&q->mutex);

    return ret;
}
