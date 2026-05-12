// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_peer.hpp"

#include <cstring>
#include <fcntl.h>
#include <random>
#include <algorithm>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#ifdef HAS_NETDB
#include <netdb.h>
#endif

#ifdef __wasi__
#define FCNTL socket_fcntl
#else
#define FCNTL fcntl
#endif

namespace luxon {
namespace enet {
namespace {
constexpr uint32_t kStunMagicCookie = 0x2112A442u;
constexpr uint16_t kStunBindingRequest = 0x0001u;
constexpr uint16_t kStunBindingSuccessResponse = 0x0101u;
constexpr uint16_t kStunAttrMappedAddress = 0x0001u;
constexpr uint16_t kStunAttrXorMappedAddress = 0x0020u;

inline uint16_t read_be16(const uint8_t *p) { return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1])); }

inline uint32_t read_be32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void write_be16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void write_be32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

std::optional<EnetEndpoint> parse_stun_address_attr(const uint8_t *attr, size_t attr_len, bool is_xor, const std::array<uint8_t, 12>& txid) {
    if (attr_len < 4)
        return std::nullopt;
    if (attr[0] != 0)
        return std::nullopt;

    const uint8_t family = attr[1];
    uint16_t port = read_be16(attr + 2);

    if (is_xor)
        port ^= static_cast<uint16_t>(kStunMagicCookie >> 16);

    if (family == 0x01) {
        if (attr_len < 8)
            return std::nullopt;

        uint8_t addr_bytes[4];
        std::memcpy(addr_bytes, attr + 4, 4);

        if (is_xor) {
            const uint8_t cookie_bytes[4] = {0x21, 0x12, 0xA4, 0x42};
            for (int i = 0; i < 4; ++i)
                addr_bytes[i] ^= cookie_bytes[i];
        }

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        std::memcpy(&sa.sin_addr, addr_bytes, sizeof(addr_bytes));

        EnetEndpoint ep{};
        std::memcpy(&ep.addr, &sa, sizeof(sa));
        ep.len = sizeof(sa);
        return ep;
    }

#ifdef HAS_SOCKADDR_IN6
    if (family == 0x02) {
        if (attr_len < 20)
            return std::nullopt;

        uint8_t addr_bytes[16];
        std::memcpy(addr_bytes, attr + 4, 16);

        if (is_xor) {
            const uint8_t cookie_bytes[4] = {0x21, 0x12, 0xA4, 0x42};
            for (int i = 0; i < 4; ++i)
                addr_bytes[i] ^= cookie_bytes[i];
            for (int i = 0; i < 12; ++i)
                addr_bytes[4 + i] ^= txid[i];
        }

        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(port);
        std::memcpy(&sa.sin6_addr, addr_bytes, sizeof(addr_bytes));

        EnetEndpoint ep{};
        std::memcpy(&ep.addr, &sa, sizeof(sa));
        ep.len = sizeof(sa);
        return ep;
    }
#endif

    return std::nullopt;
}
} // namespace

UdpSocket::UdpSocket() {
#ifdef _WIN32
    static bool wsa_inited = false;
    if (!wsa_inited) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        wsa_inited = true;
    }
#endif
}

UdpSocket::UdpSocket(SocketType native_handle) : sock_(native_handle), owning_(false), connected_(true) {}

UdpSocket::~UdpSocket() {
    if (owning_)
        close();
}

void UdpSocket::close() {
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
#else
    if (sock_ >= 0) {
#ifdef __wasi__
        ::socket_close(sock_);
#else
        ::close(sock_);
#endif
        sock_ = -1;
    }
#endif
    connected_ = false;
}

void UdpSocket::set_nonblocking(bool nb) {
#ifdef _WIN32
    u_long mode = nb ? 1 : 0;
    ioctlsocket(sock_, FIONBIO, &mode);
#else
    int flags = FCNTL(sock_, F_GETFL, 0);
    if (flags < 0)
        return;
    if (nb)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    FCNTL(sock_, F_SETFL, flags);
#endif
}

bool UdpSocket::bind_any(uint16_t port, bool ipv6) {
    close();

    int af = ipv6 ? AF_INET6 : AF_INET;
#ifdef _WIN32
    sock_ = ::socket(af, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET)
        return false;
#else
    sock_ = ::socket(af, SOCK_DGRAM, 0);
    if (sock_ < 0)
        return false;
#endif

    int yes = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));

#ifdef HAS_SOCKADDR_IN6
    if (ipv6) {
        int no = 0;
        setsockopt(sock_, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&no), sizeof(no));
    }
#endif

    if (!ipv6) {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        sa.sin_port = htons(port);
        if (::bind(sock_, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0) {
            close();
            return false;
        }
    } else {
#ifdef HAS_SOCKADDR_IN6
        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        sa.sin6_addr = in6addr_any;
        sa.sin6_port = htons(port);
        if (::bind(sock_, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0) {
            close();
            return false;
        }
#else
        return false;
#endif
    }

    return true;
}

bool UdpSocket::connect_to(const std::string& host, uint16_t port) {
    sockaddr_storage ss{};
    socklen_t slen = 0;

    // Close any previous connection just to be sure
    close();

    // Try IPv4
    sockaddr_in sa4{};
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(port);
#ifdef HAS_PTON
    if (inet_pton(AF_INET, host.c_str(), &sa4.sin_addr) == 1) {
#elif defined(_WIN32)
    sa4.sin_addr.s_addr = inet_addr(host.c_str());
    if (sa4.sin_addr.s_addr != INADDR_NONE || host == "255.255.255.255") {
#else
    if (inet_aton(host.c_str(), &sa4.sin_addr) != 0) {
#endif
        std::memcpy(&ss, &sa4, sizeof(sa4));
        slen = sizeof(sa4);
#ifdef _WIN32
        sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET)
            return false;
#else
        sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ < 0)
            return false;
#endif
        if (::connect(sock_, reinterpret_cast<sockaddr *>(&ss), slen) != 0) {
            close();
            return false;
        }
        connected_ = true;
        return true;
    }

    // Try IPv6
#ifdef HAS_SOCKADDR_IN6
    sockaddr_in6 sa6{};
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(port);
#ifdef HAS_PTON
    if (inet_pton(AF_INET6, host.c_str(), &sa6.sin6_addr) == 1) {
#elif defined(_WIN32)
    int sa6len = sizeof(sa6);
    if (WSAStringToAddressA(const_cast<LPSTR>(host.c_str()), AF_INET6, nullptr, reinterpret_cast<sockaddr *>(&sa6), &sa6len) == 0) {
#else
    if (false) {
#endif
        std::memcpy(&ss, &sa6, sizeof(sa6));
        slen = sizeof(sa6);
#ifdef _WIN32
        sock_ = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET)
            return false;
#else
        sock_ = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ < 0)
            return false;
#endif
        if (::connect(sock_, reinterpret_cast<sockaddr *>(&ss), slen) != 0) {
            close();
            return false;
        }
        connected_ = true;
        return true;
    }
#endif

    return false;
}

std::optional<EnetEndpoint> UdpSocket::lookup_hostname(const char *hostname, bool ipv6, uint16_t port) noexcept {
    if (!hostname || !*hostname)
        return std::nullopt;

#ifdef LUXON_ENET_HAS_PTON
    {
        sockaddr_in sa4{};
        sa4.sin_family = AF_INET;
        sa4.sin_port = htons(port);
        if (inet_pton(AF_INET, hostname, &sa4.sin_addr) == 1) {
            EnetEndpoint ep{};
            std::memcpy(&ep.addr, &sa4, sizeof(sa4));
            ep.len = sizeof(sa4);
            return ep;
        }
    }

#ifdef HAS_SOCKADDR_IN6
    {
        sockaddr_in6 sa6{};
        sa6.sin6_family = AF_INET6;
        sa6.sin6_port = htons(port);
        if (inet_pton(AF_INET6, hostname, &sa6.sin6_addr) == 1) {
            EnetEndpoint ep{};
            std::memcpy(&ep.addr, &sa6, sizeof(sa6));
            ep.len = sizeof(sa6);
            return ep;
        }
    }
#endif
#endif

#if defined(_WIN32) || defined(LUXON_ENET_HAS_NETDB)
    addrinfo hints{};
    hints.ai_family = ipv6 ? AF_UNSPEC : AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
#ifdef IPPROTO_UDP
    hints.ai_protocol = IPPROTO_UDP;
#endif

    addrinfo *result = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0 || !result)
        return std::nullopt;

    std::optional<EnetEndpoint> out;

    for (addrinfo *it = result; it; it = it->ai_next) {
        if (!it->ai_addr)
            continue;

        if (it->ai_family == AF_INET && it->ai_addrlen >= static_cast<socklen_t>(sizeof(sockaddr_in))) {
            sockaddr_in sa{};
            std::memcpy(&sa, it->ai_addr, sizeof(sa));
            sa.sin_port = htons(port);

            EnetEndpoint ep{};
            std::memcpy(&ep.addr, &sa, sizeof(sa));
            ep.len = sizeof(sa);
            out = ep;
            break;
        }

#ifdef HAS_SOCKADDR_IN6
        if (it->ai_family == AF_INET6 && it->ai_addrlen >= static_cast<socklen_t>(sizeof(sockaddr_in6))) {
            sockaddr_in6 sa{};
            std::memcpy(&sa, it->ai_addr, sizeof(sa));
            sa.sin6_port = htons(port);

            EnetEndpoint ep{};
            std::memcpy(&ep.addr, &sa, sizeof(sa));
            ep.len = sizeof(sa);
            out = ep;
            break;
        }
#endif
    }

    freeaddrinfo(result);
    return out;
#else
    return std::nullopt;
#endif
}

bool UdpSocket::send_stun_binding_request(const EnetEndpoint& to) {
    if (!is_open())
        return false;

    uint8_t req[20]{};
    write_be16(req + 0, kStunBindingRequest);
    write_be16(req + 2, 0);
    write_be32(req + 4, kStunMagicCookie);

    std::array<uint8_t, 12> txid{};
    std::random_device rd;
    for (size_t i = 0; i < txid.size(); ++i)
        txid[i] = static_cast<uint8_t>(rd() & 0xFF);

    std::memcpy(req + 8, txid.data(), txid.size());

    const bool ok = connected_ ? send_connected(req, sizeof(req)) : send_to(req, sizeof(req), to);
    if (!ok) {
        stun_request_pending_ = false;
        return false;
    }

    stun_transaction_id_ = txid;
    stun_request_pending_ = true;
    return true;
}

std::optional<EnetEndpoint> UdpSocket::parse_stun_binding_response(DatagramView datagram) {
    if (!stun_request_pending_ || datagram.size() < 20)
        return std::nullopt;

    const uint16_t msg_type = read_be16(datagram.data() + 0);
    const uint16_t msg_len = read_be16(datagram.data() + 2);
    const uint32_t cookie = read_be32(datagram.data() + 4);

    if (cookie != kStunMagicCookie)
        return std::nullopt;
    if (datagram.size() < static_cast<size_t>(20 + msg_len))
        return std::nullopt;
    if (!std::equal(stun_transaction_id_.begin(), stun_transaction_id_.end(), datagram.begin() + 8))
        return std::nullopt;

    // Matching response for our outstanding request.
    stun_request_pending_ = false;

    if (msg_type != kStunBindingSuccessResponse)
        return std::nullopt;

    size_t offset = 20;
    const size_t end = 20 + msg_len;

    while (offset + 4 <= end) {
        const uint16_t attr_type = read_be16(datagram.data() + offset + 0);
        const uint16_t attr_len = read_be16(datagram.data() + offset + 2);
        offset += 4;

        if (offset + attr_len > end)
            return std::nullopt;

        const uint8_t *attr = datagram.data() + offset;

        if (attr_type == kStunAttrXorMappedAddress) {
            if (auto ep = parse_stun_address_attr(attr, attr_len, true, stun_transaction_id_))
                return ep;
        } else if (attr_type == kStunAttrMappedAddress) {
            if (auto ep = parse_stun_address_attr(attr, attr_len, false, stun_transaction_id_))
                return ep;
        }

        offset += attr_len;
        offset = (offset + 3u) & ~size_t(3u);
    }

    return std::nullopt;
}

bool UdpSocket::send_connected(const uint8_t *data, size_t len) {
    if (!connected_)
        return false;
#ifdef _WIN32
    int sent = ::send(sock_, reinterpret_cast<const char *>(data), (int)len, 0);
    return sent == (int)len;
#else
    ssize_t sent = ::send(sock_, data, len, 0);
    return sent == (ssize_t)len;
#endif
}

bool UdpSocket::send_to(const uint8_t *data, size_t len, const EnetEndpoint& to) {
    if (!is_open())
        return false;
#ifdef _WIN32
    int sent = ::sendto(sock_, reinterpret_cast<const char *>(data), (int)len, 0, reinterpret_cast<const sockaddr *>(&to.addr), to.len);
    return sent == (int)len;
#else
    ssize_t sent = ::sendto(sock_, data, len, 0, reinterpret_cast<const sockaddr *>(&to.addr), to.len);
    return sent == (ssize_t)len;
#endif
}

size_t UdpSocket::recv_from(uint8_t *buf, size_t cap, EnetEndpoint& from) {
    if (!is_open())
        return 0;
    sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
#ifdef _WIN32
    int r = ::recvfrom(sock_, reinterpret_cast<char *>(buf), (int)cap, 0, reinterpret_cast<sockaddr *>(&ss), &slen);
    if (r <= 0)
        return 0;
#else
    ssize_t r = ::recvfrom(sock_, buf, cap, 0, reinterpret_cast<sockaddr *>(&ss), &slen);
    if (r <= 0)
        return 0;
#endif
    from.addr = ss;
    from.len = slen;
    return (size_t)r;
}
} // namespace enet
} // namespace luxon
