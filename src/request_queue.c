#include <pthread.h>
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
};

RequestQueue* request_queue_create(void)
{
    RequestQueue* q = malloc(sizeof(RequestQueue));
    BX_RTASSERT(q != NULL, "out of memory\n");

    memset(q, 0, sizeof(RequestQueue));

    BX_RTASSERT(pthread_mutex_init(&q->mutex, NULL) == 0, "\n");
    BX_RTASSERT(pthread_cond_init(&q->cond_not_empty, NULL) == 0, "\n");
    BX_RTASSERT(pthread_cond_init(&q->cond_not_full, NULL) == 0, "\n");

    return q;
}

void request_queue_dispose(RequestQueue** q)
{
    BX_ASSERT(q != NULL && (*q) != NULL, "expected non-NULL handle to RequestQueue\n");

    BX_RTASSERT(pthread_mutex_destroy(&(*q)->mutex) == 0, "\n");
    BX_RTASSERT(pthread_cond_destroy(&(*q)->cond_not_empty) == 0, "\n");
    BX_RTASSERT(pthread_cond_destroy(&(*q)->cond_not_full) == 0, "\n");

    free(*q);
    (*q) = NULL;
}

void request_queue_enqueue(RequestQueue* q, int client_sock_fd, ClientFlags flags)
{
    pthread_mutex_lock(&q->mutex);
    {
        // wait for the queue to not be full
        while (q->count >= REQUEST_QUEUE_SIZE) {
            pthread_cond_wait(&q->cond_not_full, &q->mutex);
        }

        // append to the end of the ring buffer
        q->tasks[q->tail] = (RequestQueueTask){
            .sock_fd = client_sock_fd,
            .flags = flags,
        };

        q->tail = (q->tail + 1) % REQUEST_QUEUE_SIZE;
        q->count++;

        pthread_cond_signal(&q->cond_not_empty);
    }
    pthread_mutex_unlock(&q->mutex);
}

RequestQueueTask request_queue_pop(RequestQueue* q)
{
    RequestQueueTask ret;

    pthread_mutex_lock(&q->mutex);
    {
        while (q->count <= 0) {
            pthread_cond_wait(&q->cond_not_empty, &q->mutex);
        }

        ret = q->tasks[q->head];
        q->head = (q->head + 1) % REQUEST_QUEUE_SIZE;
        q->count--;

        pthread_cond_signal(&q->cond_not_full);
    }
    pthread_mutex_unlock(&q->mutex);

    return ret;
}
