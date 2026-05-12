#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#include <sys/types.h>
#include <bits/wasm_socket_import.h>

#ifndef HAS_SOCKADDR_IN6
#define HAS_SOCKADDR_IN6 1
#endif

#ifndef HAS_PTON
#define HAS_PTON 1
#endif

#ifndef LUXON_ENET_HAS_PTON
#define LUXON_ENET_HAS_PTON 1
#endif

#ifndef HAS_NETDB
#define HAS_NETDB 1
#endif

#ifndef LUXON_ENET_HAS_NETDB
#define LUXON_ENET_HAS_NETDB 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};
typedef struct sockaddr sockaddr;

struct sockaddr_storage {
    sa_family_t ss_family;
    unsigned char __ss_data[126];
};
typedef struct sockaddr_storage sockaddr_storage;

#define AF_UNSPEC   0
#define AF_INET     2
#define AF_INET6    10

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SOL_SOCKET  1
#define SO_REUSEADDR 2
#define SO_ERROR     4

#define SHUT_RD    0
#define SHUT_WR    1
#define SHUT_RDWR  2

#define IPPROTO_IP    0
#define IPPROTO_TCP   6
#define IPPROTO_UDP   17
#define IPPROTO_IPV6  41

#define IPV6_V6ONLY   26

#define SOMAXCONN 128

int socket(int domain, int type, int protocol)
    __WASM_SOCKET_IMPORT("socket_socket");

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
    __WASM_SOCKET_IMPORT("socket_bind");

int listen(int sockfd, int backlog)
    __WASM_SOCKET_IMPORT("socket_listen");

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
    __WASM_SOCKET_IMPORT("socket_accept");

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
    __WASM_SOCKET_IMPORT("socket_connect");

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
    __WASM_SOCKET_IMPORT("socket_send");

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
    __WASM_SOCKET_IMPORT("socket_recv");

ssize_t sendto(int sockfd,
               const void *buf,
               size_t len,
               int flags,
               const struct sockaddr *dest_addr,
               socklen_t addrlen)
    __WASM_SOCKET_IMPORT("socket_sendto");

ssize_t recvfrom(int sockfd,
                 void *buf,
                 size_t len,
                 int flags,
                 struct sockaddr *src_addr,
                 socklen_t *addrlen)
    __WASM_SOCKET_IMPORT("socket_recvfrom");

int setsockopt(int sockfd,
               int level,
               int optname,
               const void *optval,
               socklen_t optlen)
    __WASM_SOCKET_IMPORT("socket_setsockopt");

int shutdown(int sockfd, int how)
    __WASM_SOCKET_IMPORT("socket_shutdown");

/* Non-standard on purpose: avoids conflict with WASI/libc close(). */
int socket_close(int sockfd)
    __WASM_SOCKET_IMPORT("socket_close");

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SOCKET_H */
