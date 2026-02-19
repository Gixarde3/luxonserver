// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_peer.hpp"

#include <cstring>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#ifdef HAS_NETDB
#include <netdb.h>
#endif

namespace luxon {
namespace enet {
UdpSocket::UdpSocket() {
#if defined(_WIN32)
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
#if defined(_WIN32)
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
#else
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
#endif
    connected_ = false;
}

void UdpSocket::set_nonblocking(bool nb) {
#if defined(_WIN32)
    u_long mode = nb ? 1 : 0;
    ioctlsocket(sock_, FIONBIO, &mode);
#else
    int flags = fcntl(sock_, F_GETFL, 0);
    if (flags < 0)
        return;
    if (nb)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    fcntl(sock_, F_SETFL, flags);
#endif
}

bool UdpSocket::bind_any(uint16_t port, bool ipv6) {
    close();

    int af = ipv6 ? AF_INET6 : AF_INET;
#if defined(_WIN32)
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
#else
    if (inet_aton(host.c_str(), &sa4.sin_addr) != 0) {
#endif
        std::memcpy(&ss, &sa4, sizeof(sa4));
        slen = sizeof(sa4);
#if defined(_WIN32)
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
    if (inet_pton(AF_INET6, host.c_str(), &sa6.sin6_addr) == 1) {
        std::memcpy(&ss, &sa6, sizeof(sa6));
        slen = sizeof(sa6);
#if defined(_WIN32)
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

bool UdpSocket::send_connected(const uint8_t *data, size_t len) {
    if (!connected_)
        return false;
#if defined(_WIN32)
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
#if defined(_WIN32)
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
#if defined(_WIN32)
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
