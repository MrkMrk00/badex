#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "./support/log.h"
#include "tcp_server.h"

typedef struct
{
    bool threaded;
    const char* program_name;
    uint16_t port;
} CmdArgs;

// === forward declarations
static CmdArgs parse_args(int argc, char* argv[]);
static void print_usage(FILE* file, const char* program_name);
static RequestBuffer* request_buffer_create(int sock_fd);

static const uint32_t INADDR_LOCALHOST = (127 << 24) + 1;
static const unsigned char EOT = 0x4;

static RequestRingBuffer request_ring_buffer = { 0 };

static void on_ready(ServerContext* ctx, int sock_fd, ClientFlags flags)
{
    RequestBuffer* rb = request_ring_buffer_pop(&request_ring_buffer, sock_fd);

    if (flags & CF_WANTS_READ) {
        // initialize request buffer when the socket is ready to send data
        if (rb == NULL) {
            rb = request_buffer_create(sock_fd);
        }

        if (rb->size == 0) {
            RequestRecvStatus status = request_buffer_recv(rb);
            switch (status) {
                case RB_RECV_OOM:
                    BX_INFO("REQUEST TOO LARGE disconnecting client: %d\n", sock_fd);
                    // fallthrough

                case RB_RECV_DISCONNECTED:
                    server_disconnect_client(ctx, sock_fd);
                    goto cleanup;
            }
        }

        if (rb->size == 1 && *rb->data == EOT) {
            server_disconnect_client(ctx, sock_fd);
            goto cleanup;
        }

        // === everything was sucessfull -> we have a valid RequestBuffer

        // null terminate -> be able to use as string
        request_buffer_append(rb, "\0", 1);

        if (strcmp(rb->data, "/close\r\n") == 0) {
            server_close(ctx);
        }

        printf("=== %d says \"%s\" ===\n", sock_fd, rb->data);

        // reuse the allocated request buffer for subsequent requests
        rb->size = 0;

        // if there is no more space in the ring_buffer, we can just destroy it
        if (request_ring_buffer_put(&request_ring_buffer, rb)) {
            return;
        }
    }

    if ((flags & CF_WANTS_WRITE) && rb != NULL) {
        if (request_ring_buffer_put(&request_ring_buffer, rb)) {
            return;
        }
    }

cleanup:
    if (rb == NULL) {
        return;
    }

    request_buffer_dispose(rb);
    free(rb);
}

int main(int argc, char* argv[])
{
    CmdArgs args = parse_args(argc, argv);
    ServerContext* ctx = NULL;
    int err = server_context_create(&ctx, INADDR_LOCALHOST, args.port);
    if (err < 0) {
        BX_FATAL("failed to create server context - %s\n", strerror(-err));
    }

    if (args.threaded) {
        pthread_t thread = server_run_detached(ctx, on_ready);

        void* ret = NULL;
        pthread_join(thread, ret);
    } else {
        server_run_sync(ctx, on_ready);
    }

    server_context_dispose(&ctx);

    return 0;
}

static RequestBuffer* request_buffer_create(int sock_fd)
{
    RequestBuffer* rb = NULL;
    BX_RTASSERT((rb = malloc(sizeof(RequestBuffer))) != NULL, "out of memory");

    memset(rb, 0, sizeof(RequestBuffer));
    rb->sock_fd = sock_fd;

    return rb;
}

static CmdArgs parse_args(int argc, char* argv[])
{
#define ARGS_SHIFT(argc, argv) (argc--, *argv++)

    CmdArgs args = { 0 };
    args.port = 1234;
    args.program_name = ARGS_SHIFT(argc, argv);

    while (argc > 0) {
        const char* arg = ARGS_SHIFT(argc, argv);

        // TODO: implement thread pool for handling TCP requests
        // then change this to --threads=<number> or something
        if (strcmp(arg, "--threaded") == 0) {
            args.threaded = true;

            continue;
        }

        if (strcmp(arg, "--help") == 0) {
            print_usage(stdout, args.program_name);

            exit(0);
        }

        if (strstr(arg, "--port=") == arg) {
            int offset = strlen("--port=");
            char* endptr;
            long port = strtol(arg + offset, &endptr, 10);

            if (*(arg + offset) == '\0' || *endptr != '\0' || port <= 0 || port > UINT16_MAX) {
                fprintf(stderr, "invalid value for argument port \"%s\"\n", arg + offset);
                print_usage(stderr, args.program_name);

                exit(1);
            }

            args.port = port;
            continue;
        }

        fprintf(stderr, "unknown command line argument \"%s\"\n", arg);
        print_usage(stderr, args.program_name);

        exit(1);
    }

    return args;
}

static void print_usage(FILE* file, const char* program_name)
{
    fprintf(file, "Usage: %s\n", program_name);
    fprintf(file, "Flags:\n");
    fprintf(file, "\t --threaded         spawns the server on a separate thread (useless for now)\n");
    fprintf(file, "\t --port=<number>    port on which the server will listen (default = 1234)\n");
}
