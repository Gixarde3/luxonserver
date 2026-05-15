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

static bool extract_normalized_v4(const sockaddr_storage& ss, uint32_t& ip, uint16_t& port) {
    if (ss.ss_family == AF_INET) {
        const auto *sa = reinterpret_cast<const sockaddr_in *>(&ss);
        ip = sa->sin_addr.s_addr;
        port = sa->sin_port;
        return true;
    }
#ifdef HAS_SOCKADDR_IN6
    if (ss.ss_family == AF_INET6) {
        const auto *sa6 = reinterpret_cast<const sockaddr_in6 *>(&ss);
        const uint8_t *raw = sa6->sin6_addr.s6_addr;
        bool mapped = true;
        for (int i = 0; i < 10; ++i)
            if (raw[i] != 0)
                mapped = false;
        if (raw[10] != 0xff || raw[11] != 0xff)
            mapped = false;
        if (mapped) {
            std::memcpy(&ip, &raw[12], 4);
            port = sa6->sin6_port;
            return true;
        }
    }
#endif
    return false;
}

static bool sockaddr_equal(const sockaddr_storage& a, socklen_t alen, const sockaddr_storage& b, socklen_t blen) {
    // Fast path
    if (alen == blen && a.ss_family == b.ss_family) {
        return std::memcmp(&a, &b, alen) == 0;
    }

    // Slow path: cross-family IPv4 and IPv4-mapped IPv6 equality check
    uint32_t ipA = 0, ipB = 0;
    uint16_t portA = 0, portB = 0;
    if (extract_normalized_v4(a, ipA, portA) && extract_normalized_v4(b, ipB, portB))
        return ipA == ipB && portA == portB;

    return false;
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
#elif defined(_WIN32)
    sa4.sin_addr.s_addr = inet_addr(host);
    // inet_addr returns INADDR_NONE (0xFFFFFFFF) on failure, which is also the broadcast address
    if (sa4.sin_addr.s_addr != INADDR_NONE || std::strcmp(host, "255.255.255.255") == 0) {
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
#ifdef HAS_PTON
    if (inet_pton(AF_INET6, host, &sa6.sin6_addr) == 1) {
#elif defined(_WIN32)
    int sa6len = sizeof(sa6);
    // WSAStringToAddressA is available since Windows 2000 and handles both v4 and v6
    if (WSAStringToAddressA(const_cast<LPSTR>(host), AF_INET6, nullptr, reinterpret_cast<sockaddr *>(&sa6), &sa6len) == 0) {
#else
    if (false) { // Fallback for platforms missing both inet_pton and Win32 APIs
#endif
        std::memcpy(&ss, &sa6, sizeof(sa6));
        slen = sizeof(sa6);
    }
#endif

    if (!slen)
        return std::nullopt;

    return EnetEndpoint{ss, slen};
}

bool EnetEndpoint::operator==(const EnetEndpoint& o) const { return sockaddr_equal(addr, len, o.addr, o.len); }

bool EnetEndpoint::operator<(const EnetEndpoint& o) const {
    uint32_t ipA = 0, ipB = 0;
    uint16_t portA = 0, portB = 0;
    bool is_v4_A = extract_normalized_v4(addr, ipA, portA);
    bool is_v4_B = extract_normalized_v4(o.addr, ipB, portB);

    // If both are IPv4 equivalents, sort by normalized value
    if (is_v4_A && is_v4_B) {
        if (ipA != ipB)
            return ipA < ipB;
        return portA < portB;
    }

    // Sort families safely
    if (is_v4_A != is_v4_B)
        return is_v4_A;

    // Fallback to strict memory comparison
    if (len != o.len)
        return len < o.len;
    return std::memcmp(&addr, &o.addr, len) < 0;
}

std::string EnetEndpoint::to_string() const {
    char ip_str[64] = {0}; // Safe buffer for INET6_ADDRSTRLEN
    uint16_t port = 0;

    if (addr.ss_family == AF_INET) {
        const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(&addr);
        inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
        port = ntohs(sin->sin_port);
    }
#ifdef HAS_SOCKADDR_IN6
    else if (addr.ss_family == AF_INET6) {
        const sockaddr_in6 *sin6 = reinterpret_cast<const sockaddr_in6 *>(&addr);
        inet_ntop(AF_INET6, &sin6->sin6_addr, ip_str, sizeof(ip_str));
        port = ntohs(sin6->sin6_port);
    }
#endif
    else {
        return "<endpoint>";
    }

    return std::format("{}:{}", ip_str, port);
}

std::size_t EnetEndpointHash::operator()(const EnetEndpoint& ep) const noexcept {
    uint32_t ip = 0;
    uint16_t port = 0;

    const uint8_t *p;
    size_t len;
    sockaddr_in norm4{};

    // Normalize IPv4 equivalent addresses so they produce the identical hash!
    if (extract_normalized_v4(ep.addr, ip, port)) {
        norm4.sin_family = AF_INET;
        norm4.sin_port = port;
        norm4.sin_addr.s_addr = ip;
        p = reinterpret_cast<const uint8_t *>(&norm4);
        len = sizeof(norm4);
    } else {
        p = reinterpret_cast<const uint8_t *>(&ep.addr);
        len = ep.len;
    }

    std::size_t h;
    std::size_t prime;

    if constexpr (sizeof(std::size_t) == 4) {
        h = 2166136261u;
        prime = 16777619u;
    } else {
        h = 14695981039346656037ull;
        prime = 1099511628211ull;
    }

    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= prime;
    }

    h ^= len;
    return h;
}
} // namespace enet
} // namespace luxon
