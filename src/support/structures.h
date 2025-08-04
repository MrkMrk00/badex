#ifndef STRUCTURES_H_
#define STRUCTURES_H_

#include <stddef.h>
#include <sys/types.h>

typedef struct
{
    char* memory;
    size_t size, capacity;
} StringBuilder;

void sb_ensure_capacity(StringBuilder* sb, size_t requested_capacity);
void sb_append_slice(StringBuilder*, const char* string, size_t len);
void sb_putchar(StringBuilder*, char ch);
void sb_append(StringBuilder*, const char* string);
void sb_dispose(StringBuilder*);
void sb_shift(StringBuilder*, size_t by);

#define sb_reset(sb) (sb)->size = 0

// Read from socket and copy the data into the string buffer.
// Returns the number of bytes that have been read.
ssize_t sb_recv(StringBuilder*, int sock_fd);

#endif
