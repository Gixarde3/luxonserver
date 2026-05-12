#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <stdarg.h>

#include <bits/wasm_socket_import.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIONBIO 0x5421u

int __wasm_socket_import_ioctl(int fd, unsigned long request, void *argp)
    __WASM_SOCKET_IMPORT("socket_ioctl");

static inline int ioctl(int fd, unsigned long request, ...) {
    void *argp = 0;
    va_list ap;
    va_start(ap, request);
    argp = va_arg(ap, void *);
    va_end(ap);
    return __wasm_socket_import_ioctl(fd, request, argp);
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IOCTL_H */
