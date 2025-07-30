#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

// =========================================================
// Server context and lifecycle functions
// =========================================================

// opaque context type ;)
struct server_context_t;
typedef struct server_context_t ServerContext;

typedef enum
{
    CF_WANTS_WRITE = 1 << 0,
    CF_WANTS_READ = 1 << 1,
} ClientFlags;

typedef void (*OnReadyFunc)(ServerContext* ctx, int sock_fd, ClientFlags flags);

// Creates the server context.
// - creates, binds and starts listening on the socket defined by `address` and `port`
//
// Returns: 0 on success; -errno on failure
// If the reason for failing is out of the user's control, exits the program.
int server_context_create(ServerContext** ctx_out, uint32_t address, uint16_t port);
void server_context_dispose(ServerContext**);

void server_run_sync(ServerContext*, OnReadyFunc on_ready);
pthread_t server_run_detached(ServerContext*, OnReadyFunc on_ready);

void server_disconnect_client(ServerContext*, int client_sock_fd);
void server_close(ServerContext*);

// =========================================================
// Request buffering
// =========================================================

typedef struct
{
    char* data;
    size_t size, capacity;
} RequestBuffer;

typedef ssize_t RequestRecvStatus;

#define RB_RECV_OOM ((RequestRecvStatus)-1)
#define RB_RECV_DISCONNECTED ((RequestRecvStatus)0)

// Read from socket and copy the data into the request buffer.
// Returns the number of bytes that have been read.
RequestRecvStatus request_buffer_recv(RequestBuffer* rb, int sock_fd);

// Returns the number of bytes, that can be copied into the buffer.
// Returns 0 on failure - buffer is full, 0 bytes can be written.
size_t request_buffer_append(RequestBuffer* rb, const char* data, size_t data_size);

// Destructor.
void request_buffer_dispose(RequestBuffer* rb);

#endif // !TCP_SERVER_H_
