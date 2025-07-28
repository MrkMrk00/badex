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

#include "tcp_server.h"

// Main loop
static void* _server_do_loop(void* arg);

struct server_context_t
{
    int sock_fd;
    pthread_t thread;
    OnReadyFunc on_ready;
    fd_set client_fdset;
    bool should_be_running;
};

void* server_join(ServerContext* ctx)
{
    void* ret = NULL;
    pthread_join(ctx->thread, ret);

    return ret;
}

ServerContext* server_create(uint32_t address, uint16_t port, OnReadyFunc on_ready)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        fprintf(stderr, "ERROR: failed to open a socket\n");

        return NULL;
    }

    if (fcntl(sock_fd, F_SETFL, fcntl(sock_fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
        fprintf(stderr, "ERROR: failed to set socket to non-blocking - %s\n", strerror(errno));

        return NULL;
    }

    int yes = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address);
    addr.sin_port = htons(port);

    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "ERROR: %d failed to bind to address %x - %s\n", errno, addr.sin_addr.s_addr, strerror(errno));

        return NULL;
    }

    if (listen(sock_fd, 20) == -1) {
        fprintf(stderr, "ERROR: failed to listen on socket - %s\n", strerror(errno));

        return NULL;
    }

    ServerContext* ctx = malloc(sizeof(ServerContext));
    memset(ctx, 0, sizeof(ServerContext));
    ctx->sock_fd = sock_fd;
    ctx->on_ready = on_ready;
    ctx->should_be_running = true;

    pthread_create(&ctx->thread, NULL, _server_do_loop, ctx);

    return ctx;
}

void server_disconnect_client(ServerContext* ctx, int client_sock_fd)
{
    close(client_sock_fd);
    FD_CLR(client_sock_fd, &ctx->client_fdset);
}

void server_close(ServerContext* ctx)
{
    ctx->should_be_running = false;
    close(ctx->sock_fd);
    free(ctx);
}

static void* _server_do_loop(void* arg)
{
    ServerContext* ctx = (ServerContext*)arg;

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    FD_ZERO(&ctx->client_fdset);

    fd_set read_fds;
    FD_ZERO(&read_fds);

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 20000, // 20 ms
    };

    printf("INFO: listening for incomming connections (fd: %d)\n", ctx->sock_fd);

    int max_fd = 0;

    while (ctx->should_be_running) {
        int client_fd = accept(ctx->sock_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd == -1 && errno != EWOULDBLOCK) {
            fprintf(stderr, "ERROR: failed to accept connection from socket - %s\n", strerror(errno));
            close(client_fd);

            return NULL;
        } else if (client_fd > 0) {
            FD_SET(client_fd, &ctx->client_fdset);

            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
        }

        FD_COPY(&ctx->client_fdset, &read_fds);
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready == -1) {
            fprintf(stderr, "ERROR: select() %s\n", strerror(errno));
        } else if (ready == 0) {
            continue;
        }

        for (int fd = 3; fd <= max_fd && ready > 0 && ctx->should_be_running; ++fd) {
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
