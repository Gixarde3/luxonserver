#ifndef WASM_SOCKET_SYS_TYPES_H
#define WASM_SOCKET_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SSIZE_T_DEFINED
typedef ptrdiff_t ssize_t;
#define _SSIZE_T_DEFINED 1
#endif

#ifdef __cplusplus
}
#endif

#endif /* WASM_SOCKET_SYS_TYPES_H */
