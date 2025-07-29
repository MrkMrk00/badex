#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <pthread.h>
#include <stdint.h>

// opaque context type ;)
struct server_context_t;
typedef struct server_context_t ServerContext;

typedef void (*OnReadyFunc)(ServerContext* ctx, int sock_fd);

// Creates the server context.
// - creates, binds and starts listening on the socket defined by `address` and `port`
//
// When some client is ready to send/recv the `on_ready` callback gets called.
ServerContext* server_context_create(uint32_t address, uint16_t port);

void server_run_sync(ServerContext*, OnReadyFunc on_ready);
pthread_t server_run_detached(ServerContext*, OnReadyFunc on_ready);

void server_disconnect_client(ServerContext*, int client_sock_fd);
void server_close(ServerContext*);

#endif // !TCP_SERVER_H_
