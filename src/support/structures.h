#ifndef STRUCTURES_H_
#define STRUCTURES_H_

#include <stddef.h>

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

#endif
