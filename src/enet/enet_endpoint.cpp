// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_peer.hpp"

#include <format>
#include <cstring>
#ifdef HAS_NETDB
#include <netdb.h>
#endif

namespace luxon {
namespace enet {
static bool sockaddr_equal(const sockaddr_storage& a, socklen_t alen, const sockaddr_storage& b, socklen_t blen) {
    if (alen != blen)
        return false;
    if (a.ss_family != b.ss_family)
        return false;
    return std::memcmp(&a, &b, alen) == 0;
}

std::optional<EnetEndpoint> EnetEndpoint::from(const char *host, uint16_t port) {
    sockaddr_storage ss{};
    socklen_t slen = 0;

    // Try IPv4
    sockaddr_in sa4{};
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(port);
#ifdef HAS_PTON
    if (inet_pton(AF_INET, host, &sa4.sin_addr) == 1) {
#else
    if (inet_aton(host, &sa4.sin_addr) != 0) {
#endif
        std::memcpy(&ss, &sa4, sizeof(sa4));
        slen = sizeof(sa4);
    }

    // Try IPv6
#ifdef HAS_SOCKADDR_IN6
    sockaddr_in6 sa6{};
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(port);
    if (inet_pton(AF_INET6, host, &sa6.sin6_addr) == 1) {
        std::memcpy(&ss, &sa6, sizeof(sa6));
        slen = sizeof(sa6);
    }
#endif

    if (!slen)
        return std::nullopt;

    return EnetEndpoint{ss, slen};
}

bool EnetEndpoint::operator==(const EnetEndpoint& o) const { return sockaddr_equal(addr, len, o.addr, o.len); }

std::string EnetEndpoint::to_string() const {
#ifdef HAS_GETNAMEINFO
    char host[NI_MAXHOST]{};
    char serv[NI_MAXSERV]{};
    if (getnameinfo(reinterpret_cast<const sockaddr *>(&addr), len, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        return std::format("{}:{}", host, serv);
#else
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(&addr);
    char *ip_str = inet_ntoa(sin->sin_addr);
    if (ip_str)
        return std::format("{}:{}", ip_str, ntohs(sin->sin_port));
#endif
    return "<endpoint>";
}

std::size_t EnetEndpointHash::operator()(const EnetEndpoint& ep) const noexcept {
    // Hash raw bytes (good enough for endpoint keying)
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&ep.addr);

    std::size_t h;
    std::size_t prime;

    if constexpr (sizeof(std::size_t) == 4) {
        // 32-bit FNV-1a constants
        h = 2166136261u;
        prime = 16777619u;
    } else {
        // 64-bit FNV-1a constants
        h = 14695981039346656037ull;
        prime = 1099511628211ull;
    }

    for (size_t i = 0; i < static_cast<size_t>(ep.len); ++i) {
        h ^= p[i];
        h *= prime;
    }

    h ^= ep.len;
    return h;
}
} // namespace enet
} // namespace luxon
