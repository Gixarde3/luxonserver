#include_next <fcntl.h>

#ifndef _FCNTL_H
#define _FCNTL_H

#include <stdarg.h>

#include <bits/wasm_socket_import.h>

#ifdef __cplusplus
extern "C" {
#endif

#define O_NONBLOCK 0x00000800

#define F_GETFL 3
#define F_SETFL 4

int __wasm_socket_import_fcntl(int fd, int cmd, int arg)
    __WASM_SOCKET_IMPORT("socket_fcntl");

static inline int fcntl(int fd, int cmd, ...) {
    int arg = 0;

    if (cmd != F_GETFL) {
        va_list ap;
        va_start(ap, cmd);
        arg = va_arg(ap, int);
        va_end(ap);
    }

    return __wasm_socket_import_fcntl(fd, cmd, arg);
}

#ifdef __cplusplus
}
#endif

#endif /* _FCNTL_H */
