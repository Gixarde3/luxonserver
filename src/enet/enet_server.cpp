// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_peer.hpp"
#include "enet_metrics_macros.hpp"

#include <array>
#include <span>

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

bool EnetServer::request_stun_binding(const char *server_hostname, uint16_t server_port) {
    const auto ep_opt = sock_.lookup_hostname(server_hostname, server_port);
    if (!sock_.send_stun_binding_request(*ep_opt))
        return false;
    stun_ep_ = *ep_opt;
    return true;
}

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
            if (auto ep_opt = sock_.parse_stun_binding_response(datagram)) {
                on_stun_bind(std::move(*ep_opt));
                stun_ep_ = {};
            }
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

            peer->on_state_changed = [this, peer](EnetConnectionState st) {
                if (st == EnetConnectionState::Connected)
                    if (on_peer_connected)
                        on_peer_connected(peer);
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

bool EnetServer::service_peers() {
    for (auto& [ep, peer] : peers_by_ep_)
        if (!peer->service())
            return false; // Stop if datagrams had to be queued up
    return true;
}
} // namespace enet
} // namespace luxon
