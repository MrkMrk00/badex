#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "log.h"
#include "tcp_server.h"

static const uint32_t INADDR_LOCALHOST = (127 << 24) + 1;
static const unsigned char EOT = 0x4;

static void on_ready(ServerContext* ctx, int sock_fd, ClientFlags flags)
{
    if (~flags & CF_WANTS_READ) {
        return;
    }

    RequestBuffer rb = { 0 };
    RequestRecvStatus status = request_buffer_recv(&rb, sock_fd);
    switch (status) {
        case RB_RECV_OOM:
            BX_INFO("REQUEST TOO LARGE disconnecting client: %d\n", sock_fd);
            // fallthrough

        case RB_RECV_DISCONNECTED:
            server_disconnect_client(ctx, sock_fd);
            request_buffer_dispose(&rb);
            return;
    }

    if (rb.size == 1 && *rb.data == EOT) {
        server_disconnect_client(ctx, sock_fd);
    } else {
        // null terminate -> be able to use as string
        request_buffer_append(&rb, "\0", 1);

        if (strcmp(rb.data, "/close\r\n") == 0) {
            server_close(ctx);
        }

        printf("=== %d says \"%s\" ===\n", sock_fd, rb.data);
    }

    // TODO: this should be reused :)
    request_buffer_dispose(&rb);
}

void print_usage(FILE* file, const char* program_name)
{
    fprintf(file, "Usage: %s\n", program_name);
    fprintf(file, "Flags:\n");
    fprintf(file, "\t --threaded         spawns the server on a separate thread (useless for now)\n");
    fprintf(file, "\t --port=<number>    port on which the server will listen (default = 1234)\n");
}

typedef struct
{
    bool threaded;
    const char* program_name;
    uint16_t port;
} CmdArgs;

#define ARGS_SHIFT(argc, argv) (argc--, *argv++)

int main(int argc, char* argv[])
{
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

            return 0;
        }

        if (strstr(arg, "--port=") == arg) {
            int offset = strlen("--port=");
            char* endptr;
            long port = strtol(arg + offset, &endptr, 10);

            if (*(arg + offset) == '\0' || *endptr != '\0' || port <= 0 || port > UINT16_MAX) {
                fprintf(stderr, "invalid value for argument port \"%s\"\n", arg + offset);
                print_usage(stderr, args.program_name);

                return 1;
            }

            args.port = port;
            continue;
        }

        fprintf(stderr, "unknown command line argument \"%s\"\n", arg);
        print_usage(stderr, args.program_name);

        exit(1);
    }

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
