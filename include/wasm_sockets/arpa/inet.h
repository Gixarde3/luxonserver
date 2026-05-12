#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <stdint.h>

#include <netinet/in.h>
#include <bits/wasm_socket_import.h>

#ifndef HAS_PTON
#define HAS_PTON 1
#endif

#ifndef LUXON_ENET_HAS_PTON
#define LUXON_ENET_HAS_PTON 1
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

#ifdef __cplusplus
extern "C" {
#endif

int inet_pton(int af, const char *src, void *dst)
    __WASM_SOCKET_IMPORT("socket_inet_pton");

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
    __WASM_SOCKET_IMPORT("socket_inet_ntop");

static inline int inet_aton(const char *cp, struct in_addr *inp) {
    if (!cp || !inp)
        return 0;
    return inet_pton(AF_INET, cp, inp) == 1 ? 1 : 0;
}

static inline in_addr_t inet_addr(const char *cp) {
    struct in_addr addr;
    return inet_aton(cp, &addr) ? addr.s_addr : INADDR_NONE;
}

static inline char *inet_ntoa(struct in_addr in) {
    static char buffer[INET_ADDRSTRLEN];
    return (char *)inet_ntop(AF_INET, &in, buffer, sizeof(buffer));
}

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_INET_H */
