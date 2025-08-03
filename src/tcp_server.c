#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "support/log.h"
#include "tcp_server.h"

#define CLIENT_QUEUE_MAX_SIZE 20

#ifndef FD_COPY
#define FD_COPY(src, dest) memcpy((dest), (src), sizeof(fd_set))
#endif

struct server_context_t
{
    int sock_fd;
    bool running;
    OnReadyFunc on_ready;

#ifdef __APPLE__
    int kqueue_fd;
#else
    int max_fd;
    fd_set client_fdset;
    fd_set busy_fds;
    pthread_mutex_t mutex;
#endif
};

// Main loop
static void _server_do_loop(ServerContext* arg);

int server_context_create(ServerContext** ctx_out, uint32_t address, uint16_t port)
{
    ServerContext* ctx = malloc(sizeof(ServerContext));
    BX_RTASSERT(ctx != NULL, "out of memory\n");

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

#ifdef DEBUG
    char addr_as_string[INET_ADDRSTRLEN] = { 0 };
    inet_ntop(AF_INET, &addr.sin_addr, addr_as_string, sizeof(addr_as_string));
    BX_INFO("TCP server :: LISTENING { fd: %d, addr: \"%s:%d\", listen_queue_size: %d }\n",
            sock_fd,
            addr_as_string,
            port,
            CLIENT_QUEUE_MAX_SIZE);
#endif

    ctx->sock_fd = sock_fd;
    BX_PASSERT(pthread_mutex_init(&ctx->mutex, NULL) == 0);

    return 0;
}

void server_context_dispose(ServerContext** ctx_out)
{
    if (ctx_out == NULL || (*ctx_out) == NULL) {
        return;
    }

    ServerContext* ctx = *ctx_out;

    pthread_mutex_destroy(&ctx->mutex);
    free(ctx);
    (*ctx_out) = NULL;
}

void server_run(ServerContext* ctx, OnReadyFunc on_ready)
{
    BX_ASSERT(!ctx->running, "the server was already started\n");
    ctx->on_ready = on_ready;
    ctx->running = true;

    _server_do_loop((void*)ctx);
}

void server_disconnect_client(ServerContext* ctx, int client_sock_fd)
{
    BX_ASSERT(FD_ISSET(client_sock_fd, &ctx->client_fdset), "this socket fd is not manged by this server\n");
    if (close(client_sock_fd) == -1) {
        BX_ERROR("(%s) IGNORED :: failed to close() client socket - %s\n", __func__, strerror(errno));
    }
    FD_CLR(client_sock_fd, &ctx->client_fdset);

    // TODO: mutex :/
    pthread_mutex_lock(&ctx->mutex);
    {
        FD_CLR(client_sock_fd, &ctx->busy_fds);
    }
    pthread_mutex_unlock(&ctx->mutex);

    BX_INFO("TCP server :: CLIENT_BYE { fd: %d }\n", client_sock_fd);
}

void server_close(ServerContext* ctx)
{
    ctx->running = false;

    if (ctx->sock_fd > 0) {
        close(ctx->sock_fd);
    }
    ctx->sock_fd = -1;
}

static int accept_client(int server_sock_fd)
{
    struct sockaddr_in client_addr;
    socklen_t addr_sz = sizeof(client_addr);

    int client_fd = accept(server_sock_fd, (struct sockaddr*)&client_addr, &addr_sz);
    if (client_fd < 0) {
        switch (errno) {
            // no new clients waiting to be connected or handling of the syscall was interrupted?
            // continue with calling select()
            case EWOULDBLOCK:
            case EINTR:
                return -1;
            default:
                BX_PFATAL(NULL);
        }
    }

    // set the socket to not block
    BX_PASSERT(fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK) != -1);

    // this is the way on BSD (MacOS/FreeBSD/...), on Linux there is a flag for the send() function
#ifdef SO_NOSIGPIPE
    int yes = 1;
    setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&yes, sizeof(yes));
#endif

#ifdef DEBUG

    char addr_buf[INET_ADDRSTRLEN] = { 0 };
    inet_ntop(AF_INET, &client_addr.sin_addr, addr_buf, sizeof(addr_buf));
    BX_INFO("TCP server :: CLIENT_NEW { fd: %d, ip: \"%s:%d\" }\n", client_fd, addr_buf, ntohs(client_addr.sin_port));
#endif

    return client_fd;
}

#ifdef __APPLE__
#include <sys/event.h>
#define KQUEUE_MAX_EVENTS 128

static void _server_do_loop(ServerContext* ctx)
{
    BX_PASSERT((ctx->kqueue_fd = kqueue()) > 0);

    struct kevent changeset = { 0 };
    struct kevent eventset[KQUEUE_MAX_EVENTS] = { 0 };

    // register the server socket for the EVFILT_READ
    // when the server socket becomes readable -> a new incomming connection
    // is waiting
    EV_SET(&changeset, ctx->sock_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);

    while (ctx->running) {
        int nevts = kevent(ctx->kqueue_fd, &changeset, 1, eventset, KQUEUE_MAX_EVENTS, NULL);
        if (nevts == -1 && errno != EINTR) {
            BX_PFATAL(NULL);
        }

        if (nevts <= 0) {
            continue;
        }

        for (int i = 0; i < nevts; ++i) {
            if (eventset[i].ident == ctx->sock_fd) {
                int client_fd = -1;
                while ((client_fd = accept_client(ctx->sock_fd)) != -1) {
                    EV_SET(&changeset, client_fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, NULL);
                }

                continue;
            }

            if (eventset[i].flags & EV_ERROR) {
                int ev_errno = (int)eventset[i].data;
                if (ev_errno != 0) {
                    close(ev_errno);
                    BX_ERROR("BSD loop :: got EV_ERROR from fd %lu: %s\n", eventset[i].ident, strerror(ev_errno));
                }

                continue;
            }

            ClientFlags flags = 0;
            if (eventset[i].filter == EVFILT_READ) {
                flags |= CF_WANTS_READ;
            }

            if (flags == 0) {
                continue;
            }

            ctx->on_ready(ctx, eventset[i].ident, flags);
        }
    }
}

void server_unblock_client(ServerContext* ctx, int sock_fd, ClientFlags flags)
{
    struct kevent changeset = { 0 };
    uint16_t ev_flags = 0;
    if (flags & CF_WANTS_READ) {
        ev_flags |= EVFILT_READ;
    }
    if (flags & CF_WANTS_WRITE) {
        ev_flags |= EVFILT_WRITE;
    }

    if (ev_flags == 0) {
        return;
    }

    EV_SET(&changeset, sock_fd, EV_ADD | EV_ONESHOT, ev_flags, 0, 0, NULL);
    BX_PASSERT(kevent(ctx->kqueue_fd, &changeset, 1, NULL, 0, NULL) != -1);
}

#else
static void _server_do_loop(ServerContext* ctx)
{
    fd_set read_fds, write_fds, error_fds;
    FD_ZERO(&ctx->client_fdset);
    FD_ZERO(&ctx->busy_fds);

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);

    // Wait a bit, to not keep the CPU core on 100 % usage
    // TODO: probably not the best way to do this?
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 1000,
    };

    BX_INFO("TCP server :: LOOP { select_timeout: %ld ms }\n", (timeout.tv_usec / 1000) + timeout.tv_sec * 1000);

    while (ctx->running) {
        int client_fd = accept_client(ctx->sock_fd);
        if (client_fd != -1) {
            FD_SET(client_fd, &ctx->client_fdset);

            if (client_fd > ctx->max_fd) {
                ctx->max_fd = client_fd;
            }
        }

        FD_COPY(&ctx->client_fdset, &read_fds);
        FD_COPY(&ctx->client_fdset, &write_fds);
        FD_COPY(&ctx->client_fdset, &error_fds);

        int ready = select(ctx->max_fd + 1, &read_fds, &write_fds, &error_fds, &timeout);
        if (ready == -1) {
            switch (errno) {
                // the timeout was interrupted, continue as if nothing happened
                case EINTR:
                    break;
                default:
                    BX_PFATAL(NULL);
            }
        } else if (ready == 0) {
            continue;
        }

        for (int fd = 3; fd <= ctx->max_fd && ready > 0 && ctx->running; ++fd) {
            if (FD_ISSET(fd, &error_fds)) {
                int err = 0;
                socklen_t err_sz = sizeof(err);
                BX_PASSERT(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_sz) != -1);

                if (err == 0) {
                    BX_NOTICE("IGNORED :: socket %d in error state, but got no error from getsockopt()\n", fd);
                } else {
                    // got an actual socket error -> get rid of the client
                    server_disconnect_client(ctx, fd);
                }

                ready--;

                continue;
            }

            ClientFlags flags = 0;
            if (FD_ISSET(fd, &read_fds)) {
                flags |= CF_WANTS_READ;
            }
            if (FD_ISSET(fd, &write_fds)) {
                flags |= CF_WANTS_WRITE;
            }

            if (flags == 0) {
                continue;
            }

            // This will be slow, replace with atomics?
            pthread_mutex_lock(&ctx->mutex);
            {
                if (FD_ISSET(fd, &ctx->busy_fds)) {
                    ready--;
                    continue;
                } else {
                    FD_SET(fd, &ctx->busy_fds);
                }
            }
            pthread_mutex_unlock(&ctx->mutex);

            ctx->on_ready(ctx, fd, flags);
            ready--;
        }
    }

    for (int fd = 3; fd <= ctx->max_fd; ++fd) {
        if (!FD_ISSET(fd, &ctx->client_fdset)) {
            continue;
        }

        close(fd);
        FD_CLR(fd, &ctx->client_fdset);
    }

    return NULL;
}
#endif
