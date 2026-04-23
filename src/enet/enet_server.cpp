// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_peer.hpp"
#include "enet_metrics_macros.hpp"

#include <span>
#include <chrono>

namespace luxon {
namespace enet {
EnetServer::EnetServer(EnetPeerConfig cfg
#ifdef LUXON_ENET_ENABLE_METRICS
                       ,
                       Metrics& metrics
#endif
                       )
    :
#ifdef LUXON_ENET_ENABLE_METRICS
      metrics_(metrics),
#endif
      cfg_(cfg) {
    if (!cfg_.time_base)
        cfg_.time_base = EnetPeer::create_time_base();
}

bool EnetServer::bind(uint16_t port, bool ipv6) {
    if (!sock_.bind_any(port, ipv6))
        return false;
    sock_.set_nonblocking(true);

    return true;
}

std::shared_ptr<EnetPeer> EnetServer::find_peer(int16_t peer_id) const {
    auto it = peers_by_id_.find(peer_id);
    if (it == peers_by_id_.end())
        return {};
    return it->second;
}

void EnetServer::remove_peer(std::shared_ptr<EnetPeer> peer) {
    for (auto it = peers_by_ep_.begin(); it != peers_by_ep_.end(); ++it) {
        if (it->second == peer) {
            peers_by_ep_.erase(it);
            break;
        }
    }

    for (auto it = peers_by_id_.begin(); it != peers_by_id_.end(); ++it) {
        if (it->second == peer) {
            peers_by_id_.erase(it);
            break;
        }
    }
}

bool EnetServer::request_stun_binding(const char *server_hostname, bool ipv6, uint16_t server_port) {
    const auto ep_opt = sock_.lookup_hostname(server_hostname, ipv6, server_port);
    if (!ep_opt)
        return false;
    if (!sock_.send_stun_binding_request(*ep_opt))
        return false;
    stun_ep_ = *ep_opt;
    return true;
}

bool EnetServer::keepalive_stun_binding() { return sock_.send_to(reinterpret_cast<const uint8_t *>(""), 1, stun_ep_); }

void EnetServer::service_self() {
    // Poll socket
    DatagramBuffer buf;
    for (;;) {
        EnetEndpoint from;
        size_t r = sock_.recv_from(buf.data(), buf.size(), from);
        if (r == 0)
            break;

        DatagramView datagram(buf.begin(), buf.begin() + r);

        // Update metrics
        ENET_METRIC_ADD(udp.datagrams_in, 1);
        ENET_METRIC_ADD(global.bytes_in, r);

        // Handle STUN response
        if (stun_ep_ == from) {
            if (auto ep_opt = sock_.parse_stun_binding_response(datagram))
                on_stun_bind(std::move(*ep_opt));
            break;
        }

        // Parse header to find challenge/peer id, etc.
        EnetPacketHeader hdr;
        std::vector<EnetCommand> cmds;
        try {
            cmds = parse_packet(datagram, hdr);
        } catch (...) {
            ENET_METRIC_ADD(enet.datagram_validation_failures, 1);
            continue;
        }

        auto itp = peers_by_ep_.find(from);
        if (itp == peers_by_ep_.end()) {
            // Only accept if contains Connect command
            const EnetCommand *connect_cmd = nullptr;
            for (auto& c : cmds) {
                if (c.header.command_type == EnetCommandType::Connect) {
                    connect_cmd = &c;
                    break;
                }
            }
            if (!connect_cmd)
                continue;

            // Apply connect payload settings
            EnetPeerConfig peer_cfg = cfg_;
            peer_cfg.apply_connect_command(*connect_cmd);

            const int16_t assigned = next_peer_id_++;
            auto peer = std::make_shared<EnetPeer>(peer_cfg
#ifdef LUXON_ENET_ENABLE_METRICS
                                                   ,
                                                   metrics_
#endif
            );
            peer->attach_server_side(sock_, from, assigned, hdr.challenge);

            peers_by_ep_[from] = peer;
            peers_by_id_[assigned] = peer;

            peer->on_state_changed = [this, weak_peer = std::weak_ptr(peer)](EnetConnectionState st) {
                if (st == EnetConnectionState::Connected) {
                    if (auto p = weak_peer.lock()) {
                        if (on_peer_connected)
                            on_peer_connected(p);
                    }
                }
            };

            // Feed the connect packet into the peer as well (so it acks, etc.)
            peer->handle_incoming_packet(hdr, cmds, datagram.size());
        } else {
            itp->second->handle_incoming_packet(hdr, cmds, datagram.size());

            // Make sure disconnected peer is no longer serviced, ever again
            if (itp->second->state() == EnetConnectionState::Disconnected)
                remove_peer(itp->second);
        }
    }
}

bool EnetServer::service_peers(uint32_t& timeout_us) {
    auto start_time = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<EnetPeer>> dead_peers;
    bool result = true;

    // If queue is empty, we've completed a full cycle, repopulate with all current peer IDs
    if (service_queue_.empty()) {
        service_queue_.reserve(peers_by_id_.size());
        for (const auto& [id, peer] : peers_by_id_)
            service_queue_.push_back(id);
    }

    while (!service_queue_.empty()) {
        // Pop peer ID to process
        int16_t peer_id = service_queue_.back();
        service_queue_.pop_back();

        // Find peer
        auto it = peers_by_id_.find(peer_id);
        if (it == peers_by_id_.end())
            continue; // Peer has disconnected

        auto peer = it->second;

        if (peer->state() == EnetConnectionState::Disconnected) {
            dead_peers.push_back(peer);
            continue;
        }

        if (!peer->service()) {
            // Processing stopped, re-add to the back so it gets processed first next time
            service_queue_.push_back(peer_id);
            result = false;
            break;
        }

        // Check if timeout is exceeded
        auto now = std::chrono::steady_clock::now();
        uint32_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
        if (elapsed_us >= timeout_us)
            break;
    }

    // Clean up dead peers
    for (const auto& dead_peer : dead_peers)
        remove_peer(dead_peer);

    // Calculate total time taken and adjust the timeout parameter
    auto end_time = std::chrono::steady_clock::now();
    uint32_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    if (elapsed_us >= timeout_us)
        timeout_us = 0;
    else
        timeout_us -= elapsed_us;

    return result;
}
} // namespace enet
} // namespace luxon
