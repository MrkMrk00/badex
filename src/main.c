#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "log.h"
#include "tcp_server.h"

#define TCP_RECV_BUF_SIZE 2048
static const uint32_t INADDR_LOCALHOST = (127 << 24) + 1;
static const unsigned char EOT = 0x4;

static void on_ready(ServerContext* ctx, int sock_fd)
{
    char buf[TCP_RECV_BUF_SIZE] = { 0 };
    const size_t max_bytes = sizeof(buf) - 1;

    ssize_t bytes_read = recv(sock_fd, buf, max_bytes, 0);
    if (bytes_read == 0 || (bytes_read == 1 && buf[bytes_read - 1] == EOT)) {
        server_disconnect_client(ctx, sock_fd);

        return;
    }

    BX_PASSERT(bytes_read > 0);
    if (bytes_read == max_bytes) {
        BX_NOTICE("message from \"%d\" too long", sock_fd);
    }

    buf[bytes_read] = 0;

    if (strcmp(buf, "/close\r\n") == 0) {
        server_close(ctx);
    }

    printf("=== %d says \"%s\" ===\n", sock_fd, buf);
}

void print_usage(FILE* file, const char* program_name)
{
    fprintf(file, "Usage: %s\n", program_name);
    fprintf(file, "Flags:\n");
    fprintf(file, "\t --threaded\tspawns the server on a separate thread (useless for now)\n");
}

typedef struct
{
    bool threaded;
    const char* program_name;
} CmdArgs;

#define ARGS_SHIFT(argc, argv) (argc--, *argv++)

int main(int argc, char* argv[])
{
    CmdArgs args = { 0 };
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

        print_usage(stderr, args.program_name);
        fprintf(stderr, "unknown command line argument \"%s\"\n", arg);

        exit(1);
    }

    ServerContext* ctx = NULL;
    int err = server_context_create(&ctx, INADDR_LOCALHOST, 1234);
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
