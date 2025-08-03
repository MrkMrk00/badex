#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "support/log.h"
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
    void* worker_context;
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

static void* task_dispatcher(void* arg)
{
    // don't access anything inside of this without a mutex...
    RequestQueue* q = (RequestQueue*)arg;
    RequestRingBuffer* ring_buffer = malloc(sizeof(RequestRingBuffer));
    BX_RTASSERT(ring_buffer != NULL, "out of memory\n");

    while (true) {
        RequestQueueTask task = request_queue_pop(q);
        if (task.flags & CF_STOP) {
            break;
        }

        RequestBuffer* request_buffer = request_ring_buffer_pop(ring_buffer, task.sock_fd);
        if (request_buffer == NULL) {
            BX_ERROR("RequestRingBuffer out of memory, skipping running work for fd: %d\n", task.sock_fd);

            // return task back into the queue
            request_queue_enqueue(q, task);

            continue;
        }

        q->worker(q->worker_context, request_buffer, task);
    }

    free(ring_buffer);
    pthread_exit(NULL);
}

void request_queue_run(RequestQueue* q, void* ctx, RequestQueueWorker worker)
{
    BX_ASSERT(q->thread_count > 0, "cannot have negative thread_count :)\n");
    BX_ASSERT(!q->is_running, "the thread pool is already running\n");

    q->worker = worker;
    q->worker_context = ctx;

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
