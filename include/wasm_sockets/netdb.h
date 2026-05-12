#ifndef WASM_SOCKET_NETDB_H
#define WASM_SOCKET_NETDB_H

#include <sys/socket.h>
#include <bits/wasm_socket_import.h>

#ifndef HAS_NETDB
#define HAS_NETDB 1
#endif

#ifndef LUXON_ENET_HAS_NETDB
#define LUXON_ENET_HAS_NETDB 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    socklen_t        ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};

int getaddrinfo(const char *node,
                const char *service,
                const struct addrinfo *hints,
                struct addrinfo **res)
    __WASM_SOCKET_IMPORT("socket_getaddrinfo");

void freeaddrinfo(struct addrinfo *res)
    __WASM_SOCKET_IMPORT("socket_freeaddrinfo");

#ifdef __cplusplus
}
#endif

#endif /* WASM_SOCKET_NETDB_H */
