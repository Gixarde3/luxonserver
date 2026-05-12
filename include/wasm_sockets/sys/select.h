#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include <stdint.h>
#include <string.h>

#include <sys/types.h>
#include <bits/wasm_socket_import.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

typedef struct fd_set {
    uint64_t __fds_bits[(FD_SETSIZE + 63) / 64];
} fd_set;

#define __FD_ELT(d)   ((unsigned)(d) >> 6)
#define __FD_MASK(d)  (1ULL << ((unsigned)(d) & 63u))

#define FD_ZERO(set) \
    do { memset((void *)(set), 0, sizeof(*(set))); } while (0)

#define FD_SET(fd, set) \
    do { (set)->__fds_bits[__FD_ELT(fd)] |= __FD_MASK(fd); } while (0)

#define FD_CLR(fd, set) \
    do { (set)->__fds_bits[__FD_ELT(fd)] &= ~__FD_MASK(fd); } while (0)

#define FD_ISSET(fd, set) \
    (((set)->__fds_bits[__FD_ELT(fd)] & __FD_MASK(fd)) != 0)

int select(int nfds,
           fd_set *readfds,
           fd_set *writefds,
           fd_set *exceptfds,
           struct timeval *timeout) __WASM_SOCKET_IMPORT("socket_select");

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SELECT_H */
