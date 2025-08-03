#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "redis/resp.h"
#include "support/log.h"
#include "support/structures.h"
#include "tcp_server.h"

typedef struct
{
    const char* program_name;
    uint16_t port;
    uint8_t io_thread_count;
} CmdArgs;

static CmdArgs parse_args(int argc, char* argv[]);
static void print_usage(FILE* file, const char* program_name);

static const uint32_t INADDR_LOCALHOST = (127 << 24) + 1;
static const unsigned char EOT = 0x4;

static StringBuilder string_builder = { 0 };

static void io_queue_worker(ServerContext* ctx, int sock_fd, uint32_t flags);
static void io_worker(ServerContext* ctx, RequestBuffer* rb, RequestQueueTask task);

int main(int argc, char* argv[])
{
    CmdArgs args = parse_args(argc, argv);
    ServerContext* ctx = NULL;
    RequestQueue* request_queue = NULL;

    int err = server_context_create(&ctx, INADDR_LOCALHOST, args.port);
    if (err < 0) {
        BX_FATAL("failed to create server context - %s\n", strerror(-err));
    }

    request_queue = request_queue_create(args.io_thread_count);
    request_queue_run(request_queue, ctx, (RequestQueueWorker)io_worker);

    server_run(ctx, io_queue_worker);

    return 0;
}

static void io_worker(ServerContext* ctx, RequestBuffer* rb, RequestQueueTask task)
{
    ClientFlags flags = task.flags;
    int sock_fd = task.sock_fd;

    bool should_dispose = false;

    if (flags & CF_WANTS_READ) {
        ssize_t bytes_received = request_buffer_recv(rb);

        if (bytes_received <= 0 || rb->data[rb->size - 1] == EOT) {
            server_disconnect_client(ctx, sock_fd);
            goto cleanup;
        }

        // === everything was sucessfull -> we have a valid RequestBuffer

        RespCommand cmd = { 0 };
        ssize_t new_offset = resp_try_parse(&cmd, &string_builder, rb->data, rb->size);
        if (new_offset > 0) {
            resp_print_command(&cmd);

            // didn't test, if it's correct :)
            size_t size_to_copy = rb->size - new_offset;
            if (size_to_copy > 0) {
                memcpy(rb->data, rb->data + new_offset, size_to_copy);
                rb->size = size_to_copy;
            } else {
                rb->size = 0;
            }
        }

        should_dispose = rb->size == 0;
    }

    if (flags & CF_WANTS_WRITE && rb->size > 0) {
        const char* response = "HTTP/1.1 200 OK\r\n"
                               "Date: Sat, 02 Aug 2025 20:11:12 GMT\r\n"
                               "Content-Type: text/plain; charset=UTF-8\r\n"
                               "Content-Length: 18\r\n"
                               "\r\n"
                               "Ahoj z browseru :)";

        if (send(sock_fd, response, strlen(response), MSG_NOSIGNAL) == -1) {
            switch (errno) {
                case EAGAIN:
                case EINTR:
                case ENETUNREACH:
                case ENETDOWN:
                case EPIPE:
                case ECONNRESET:
                    break;

                default:
                    BX_PFATAL("failed to send() data to client");
            }
        }

        // TODO: mutex :/
        server_disconnect_client(ctx, sock_fd);
        should_dispose = true;
    }

    if (!should_dispose) {
        return;
    }
cleanup:
    request_buffer_dispose(rb);
}

static CmdArgs parse_args(int argc, char* argv[])
{
#define ARGS_SHIFT(argc, argv) (argc--, *argv++)

    CmdArgs args = {
        .port = 1234,
        .program_name = ARGS_SHIFT(argc, argv),
        .io_thread_count = 1,
    };

    while (argc > 0) {
        const char* arg = ARGS_SHIFT(argc, argv);

        if (strcmp(arg, "--io-threads=") == 0) {
            int offset = strlen("--io-threads=");
            char* endptr;
            long io_thread_count = strtol(arg + offset, &endptr, 10);

            if (*(arg + offset) == '\0' || *endptr != '\0' || io_thread_count <= 1 || io_thread_count > UINT8_MAX) {
                fprintf(stderr, "invalid value for argument --io-threads \"%s\"\n", arg + offset);
                print_usage(stderr, args.program_name);

                exit(1);
            }

            args.io_thread_count = io_thread_count;

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
    fprintf(file, "\t --io-thread-count=<number>    how many threads to spawn for handling IO (default = 1)\n");
    fprintf(file, "\t --port=<number>               port on which the server will listen (default = 1234)\n");
}
