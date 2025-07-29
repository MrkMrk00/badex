#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "log.h"
#include "tcp_server.h"

#define CLIENT_QUEUE_MAX_SIZE 20

// Main loop
static void* _server_do_loop(void* arg);

typedef enum
{
    SL_STOPPED = 0,
    SL_SYNC,
    SL_THREADED,
} ServerLoopKind;

struct server_context_t
{
    int sock_fd;
    ServerLoopKind loop_kind;
    fd_set client_fdset;
    OnReadyFunc on_ready;

    // only used when `look_kind` == `SL_THREADED`
    pthread_t thread;
};

int server_context_create(ServerContext** ctx_out, uint32_t address, uint16_t port)
{
    BX_ASSERT(ctx_out != NULL, "expected a non-NULL ctx_out parameter");

    int sock_fd;
    BX_PASSERT((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) != -1);

    // make the socket non-blocking
    // (if there are no clients waiting to connect, the accept() call returns an error instead of blocking)
    BX_PASSERT(fcntl(sock_fd, F_SETFL, fcntl(sock_fd, F_GETFL, 0) | O_NONBLOCK) != -1);

    // reuse the address
    int yes = 1;
    BX_PASSERT(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) != -1);

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address);
    addr.sin_port = htons(port);

    // bind the socket to the specified address and port (from args `address` and `port`)
    int bind_ret = bind(sock_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
    if (bind_ret == -1) {
        // if the error has something to do with the user-provided arguments
        // return the error
        switch (errno) {
            case EACCES:
            case EADDRINUSE:
            case EADDRNOTAVAIL:
            case EAFNOSUPPORT:
            case EFAULT:
                return -errno;

            default:
                BX_PFATAL(NULL);
        };
    }

    // start listening for incomming TCP connections
    BX_PASSERT(listen(sock_fd, CLIENT_QUEUE_MAX_SIZE) != -1);

    char addr_as_string[INET_ADDRSTRLEN] = { 0 };
    inet_ntop(AF_INET, &addr.sin_addr, addr_as_string, sizeof(addr_as_string));
    BX_LOG("INFO", "TCP server :: LISTENING { fd: %d, addr: \"%s:%d\", listen_queue_size: %d }\n", sock_fd, addr_as_string, port, CLIENT_QUEUE_MAX_SIZE);

    (*ctx_out) = malloc(sizeof(ServerContext));
    BX_ASSERT((*ctx_out) != NULL, "out of memory");

    memset((*ctx_out), 0, sizeof(ServerContext));
    (*ctx_out)->sock_fd = sock_fd;
    (*ctx_out)->loop_kind = SL_STOPPED;

    return 0;
}

void server_run_sync(ServerContext* ctx, OnReadyFunc on_ready)
{
    BX_ASSERT(ctx->loop_kind == SL_STOPPED, "the server was already started");
    ctx->on_ready = on_ready;
    ctx->loop_kind = SL_SYNC;

    _server_do_loop((void*)ctx);
}

pthread_t server_run_detached(ServerContext* ctx, OnReadyFunc on_ready)
{
    BX_ASSERT(ctx->loop_kind == SL_STOPPED, "the server was already started");
    ctx->on_ready = on_ready;
    ctx->loop_kind = SL_THREADED;

    pthread_create(&ctx->thread, NULL, _server_do_loop, (void*)ctx);

    return ctx->thread;
}

void server_disconnect_client(ServerContext* ctx, int client_sock_fd)
{
    BX_ASSERT(FD_ISSET(client_sock_fd, &ctx->client_fdset), "this socket fd is not manged by this server");

    BX_PASSERT(close(client_sock_fd) != -1);
    FD_CLR(client_sock_fd, &ctx->client_fdset);

    BX_LOG("INFO", "TCP server :: CLIENT_BYE { fd: %d }\n", client_sock_fd);
}

void server_close(ServerContext* ctx)
{
    ctx->loop_kind = SL_STOPPED;

    if (ctx->sock_fd > 0) {
        close(ctx->sock_fd);
    }
    ctx->sock_fd = -1;
}

static void* _server_do_loop(void* arg)
{
    ServerContext* ctx = (ServerContext*)arg;

    struct sockaddr_in client_addr;
    socklen_t addr_sz = sizeof(client_addr);
    char addr_buf[INET_ADDRSTRLEN] = { 0 };

    FD_ZERO(&ctx->client_fdset);

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    // Wait a bit, to not keep the CPU core on 100 % usage
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 10000, // 10 ms
    };

    int max_fd = 0;

    BX_LOG("INFO",
           "TCP server :: LOOP { threaded: %s, select_timeout: %ld ms }\n",
           ctx->loop_kind == SL_THREADED ? "true" : "false",
           (timeout.tv_usec / 1000) + timeout.tv_sec * 1000);

    while (ctx->loop_kind != SL_STOPPED) {
        int client_fd = accept(ctx->sock_fd, (struct sockaddr*)&client_addr, &addr_sz);

        // accept() call failed (how?)
        // TODO: handle gracefully?
        BX_PASSERT(client_fd != -1 || errno == EWOULDBLOCK);

        if (client_fd > 0) {
            inet_ntop(AF_INET, &client_addr.sin_addr, addr_buf, sizeof(addr_buf));
            BX_LOG("INFO", "TCP server :: CLIENT_NEW { fd: %d, ip: \"%s:%d\" }\n", client_fd, addr_buf, ntohs(client_addr.sin_port));

            FD_SET(client_fd, &ctx->client_fdset);

            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
        }

        FD_COPY(&ctx->client_fdset, &read_fds);
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready == -1) {
            BX_LOG("ERROR", "select() %s\n", strerror(errno));

            continue;
        } else if (ready == 0) {
            continue;
        }

        for (int fd = 3; fd <= max_fd && ready > 0 && ctx->loop_kind != SL_STOPPED; ++fd) {
            if (!FD_ISSET(fd, &read_fds)) {
                continue;
            }

            ctx->on_ready(ctx, fd);

            ready--;
        }
    }

    for (int fd = 3; fd <= max_fd; ++fd) {
        if (!FD_ISSET(fd, &ctx->client_fdset)) {
            continue;
        }

        close(fd);
        FD_CLR(fd, &ctx->client_fdset);
    }

    return NULL;
}
