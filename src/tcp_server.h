#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <stdint.h>
struct server_context_t;
typedef struct server_context_t ServerContext;

typedef void (*OnReadyFunc)(ServerContext* ctx, int sock_fd);

ServerContext* server_create(uint32_t address, uint16_t port, OnReadyFunc on_ready);
void* server_join(ServerContext*);
void server_disconnect_client(ServerContext*, int client_sock_fd);
void server_close(ServerContext*);

#endif // !TCP_SERVER_H_
