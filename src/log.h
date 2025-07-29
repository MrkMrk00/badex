#ifndef SUPPORT_H_
#define SUPPORT_H_

#define BX_LOG(level, message, ...) printf("%s:%d: [%s] " message, __FILE__, __LINE__, (level), __VA_ARGS__)

#define BX_FATAL(message, ...)                                                                                                                                 \
    do {                                                                                                                                                       \
        fprintf(stderr, "%s:%d: [FATAL] " message, __FILE__, __LINE__, __VA_ARGS__);                                                                                   \
        exit(1);                                                                                                                                               \
    } while (0)

#define BX_LOGS(level, message) BX_LOG(level, "%s\n", (message))

#define BX_ASSERT(cond, message)                                                                                                                               \
    if (!(cond))                                                                                                                                               \
    BX_FATAL("(%s): %s\n", #cond, (message))

// Print FATAL error message from `errno` (after a libc call) and exit program.
#define BX_PFATAL(message)                                                                                                                                     \
    do {                                                                                                                                                       \
        fprintf(stderr, "%s:%d: [FATAL] ", __FILE__, __LINE__);                                                                                                 \
        perror(message);                                                                                                                                       \
        exit(1);                                                                                                                                               \
    } while (0)

// Assert a succesfull call with the possibility of having `errno` set.
#define BX_PASSERT(cond)                                                                                                                                       \
    if (!(cond))                                                                                                                                               \
    BX_PFATAL(#cond)

#endif // !SUPPORT_H_
