#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <stdint.h>

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct in6_addr {
    unsigned char s6_addr[16];
};

struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};

struct sockaddr_in6 {
    sa_family_t     sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

#define INADDR_ANY   ((in_addr_t)0x00000000u)
#define INADDR_NONE  ((in_addr_t)0xFFFFFFFFu)

#define IN6ADDR_ANY_INIT {{0}}
static const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;

static inline uint16_t htons(uint16_t x) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return x;
#else
    return (uint16_t)((x << 8) | (x >> 8));
#endif
}

static inline uint16_t ntohs(uint16_t x) {
    return htons(x);
}

static inline uint32_t htonl(uint32_t x) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return x;
#else
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8)  |
           ((x & 0x00FF0000u) >> 8)  |
           ((x & 0xFF000000u) >> 24);
#endif
}

static inline uint32_t ntohl(uint32_t x) {
    return htonl(x);
}

#ifdef __cplusplus
}
#endif

#endif /* _NETINET_IN_H */
