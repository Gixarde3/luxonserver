#include_next <fcntl.h>

#ifndef WASM_SOCKET_FCNTL_H
#define WASM_SOCKET_FCNTL_H

#include <stdarg.h>
#include <bits/wasm_socket_import.h>

#ifdef __cplusplus
extern "C" {
#endif

#define O_NONBLOCK 0x00000800

/* Non-standard on purpose: avoids conflict with WASI/libc fcntl(). */
int socket_fcntl(int fd, int cmd, int arg) __WASM_SOCKET_IMPORT("socket_fcntl");

#ifdef __cplusplus
}
#endif

#endif /* WASM_SOCKET_FCNTL_H */
