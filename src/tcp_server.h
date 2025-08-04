#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include "support/structures.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>

// =========================================================
// Server context and lifecycle functions
// =========================================================

enum
{
    CF_WANTS_WRITE = 1 << 0,
    CF_WANTS_READ = 1 << 1,

    // Stop condition
    CF_STOP = 1 << 2,
};

typedef uint32_t ClientFlags;

struct server_context_t;
typedef struct server_context_t ServerContext;

// Signature of the function, that gets called when a client is ready to
// receive or send data to the server.
typedef void (*OnReadyFunc)(struct server_context_t* ctx, int sock_fd, ClientFlags flags);

// Creates the server context.
// - creates, binds and starts listening on the socket defined by `address` and `port`
//
// Returns: 0 on success; -errno on failure
// If the reason for failing is out of the user's control, exits the program.
int server_context_create(ServerContext**, uint32_t address, uint16_t port);
void server_context_dispose(ServerContext**);

void server_run(ServerContext*, OnReadyFunc on_ready);

// Unset the `busy` flag from a FD and allow another thread
// to handle the client. Should be called after all work was
// done and there is nothing more to do with the client.
void server_unblock_client(ServerContext*, int sock_fd, ClientFlags flags);

// Basically - close the socket.
void server_disconnect_client(ServerContext*, int client_sock_fd);
void server_close(ServerContext*);

// =========================================================
// Multi-threaded IO handling
// =========================================================

typedef struct
{
    int sock_fd;
    ClientFlags flags;
} RequestQueueTask;

struct request_queue_t;
typedef struct request_queue_t RequestQueue;
typedef void (*RequestQueueWorker)(void* context,
                                   StringBuilder* request_buffer,
                                   StringBuilder* response_buffer,
                                   RequestQueueTask task);

RequestQueue* request_queue_create(int worker_thread_count);
void request_queue_dispose(RequestQueue**);

void request_queue_run(RequestQueue*, void* context, RequestQueueWorker worker);
void request_queue_stop(RequestQueue*);
void request_queue_enqueue(RequestQueue*, RequestQueueTask task);

#endif // !TCP_SERVER_H_
