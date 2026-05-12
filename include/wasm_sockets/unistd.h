#include_next <unistd.h>

#ifndef _UNISTD_H
#define _UNISTD_H

#include <bits/wasm_socket_import.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Non-standard on purpose: avoids conflict with WASI/libc close(). */
int socket_close(int fd)
    __WASM_SOCKET_IMPORT("socket_close");

#ifdef __cplusplus
}
#endif

#endif /* _UNISTD_H */
