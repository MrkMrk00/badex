#ifndef SUPPORT_H_
#define SUPPORT_H_

#include <stdio.h>

// main logging statement
#define _BX_DO_LOG(file, level, message, ...) fprintf((file), "%s:%d: [%s] " message, __FILE__, __LINE__, (level), __VA_ARGS__)

// this always logs (errors and stuff)
#define BX_NOTICE(message, ...) _BX_DO_LOG(stdout, "NOTICE", message, __VA_ARGS__)
#define BX_ERROR(message, ...) _BX_DO_LOG(stderr, "ERROR", message, __VA_ARGS__)

// Non recoverable errors
#define BX_FATAL(...)                                                                                                                                          \
    do {                                                                                                                                                       \
        BX_ERROR(__VA_ARGS__);                                                                                                                                 \
        exit(1);                                                                                                                                               \
    } while (0)

#define BX_PFATAL(message)                                                                                                                                     \
    do {                                                                                                                                                       \
        BX_ERROR("%s", "");                                                                                                                                    \
        perror(message);                                                                                                                                       \
        exit(1);                                                                                                                                               \
    } while (0)

// runtime assert is always available
#define BX_RTASSERT(cond, message)                                                                                                                             \
    if (!(cond))                                                                                                                                               \
    BX_FATAL("(%s): %s\n", #cond, (message))

// PASSERT is always available - only use for stuff that should crash the program
#define BX_PASSERT(cond)                                                                                                                                       \
    if (!(cond))                                                                                                                                               \
    BX_PFATAL(#cond)

#ifdef DEBUG
#define BX_INFO(message, ...) _BX_DO_LOG(stdout, "INFO", message, __VA_ARGS__)
#define BX_INFOS(level, message) BX_INFO(level, "%s\n", (message))

// runtime assert only defined when -DDEBUG is provided
#define BX_ASSERT(cond, message)                                                                                                                               \
    if (!(cond))                                                                                                                                               \
    BX_FATAL("(%s): %s\n", #cond, (message))
#else
#define BX_INFO(...)
#define BX_INFOS(...)
#define BX_ASSERT(...)
#endif

#endif // !SUPPORT_H_
