#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "tcp_server.h"

const uint32_t INADDR_LOCALHOST = (127 << 24) + 1;

void on_ready(ServerContext* ctx, int sock_fd)
{
    char buf[2048] = { 0 };
    size_t max_bytes = sizeof(buf) - 1;

    ssize_t bytes_read = recv(sock_fd, buf, max_bytes, 0);

    if (bytes_read == 0 || (bytes_read == 1 && buf[bytes_read - 1] == 0x4)) {
        server_disconnect_client(ctx, sock_fd);

        return;
    }

    if (bytes_read < 0) {
        fprintf(stderr, "fuck; %s\n", strerror(errno));

        return;
    }

    if (strcmp(buf, "/close")) {
        server_close(ctx);
    }

    printf("%d says %s", sock_fd, buf);
}

int main(void)
{
    ServerContext* ctx = server_create(INADDR_LOCALHOST, 8080, on_ready);
    server_join(ctx);

    return 0;
}
