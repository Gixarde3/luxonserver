#include "enet_peer.hpp"

namespace luxon {
namespace enet {
EnetServer::EnetServer(EnetPeerConfig cfg) : cfg_(cfg) {
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

void EnetServer::service_self() {
    // Poll socket
    ByteArray buf(cfg_.mtu);
    for (;;) {
        EnetEndpoint from;
        size_t r = sock_.recv_from(buf.data(), buf.size(), from);
        if (r == 0)
            break;

        ByteArray datagram(buf.begin(), buf.begin() + r);

        // Parse header to find challenge/peer id, etc.
        EnetPacketHeader hdr;
        std::vector<EnetCommand> cmds;
        try {
            cmds = parse_packet(datagram, hdr);
        } catch (...) {
            continue;
        }

        auto itp = peers_by_ep_.find(from);
        if (itp == peers_by_ep_.end()) {
            // Only accept if contains Connect command
            bool has_connect = false;
            for (auto& c : cmds) {
                if (c.header.command_type == EnetCommandType::Connect) {
                    has_connect = true;
                    break;
                }
            }
            if (!has_connect)
                continue;

            int16_t assigned = next_peer_id_++;
            auto peer = std::make_shared<EnetPeer>(cfg_);
            peer->attach_server_side(sock_, from, assigned, hdr.challenge);

            peers_by_ep_[from] = peer;
            peers_by_id_[assigned] = peer;

            peer->on_state_changed = [this, peer](EnetConnectionState st) {
                if (st == EnetConnectionState::Connected)
                    if (on_peer_connected)
                        on_peer_connected(peer);
            };

            // Feed the connect packet into the peer as well (so it acks, etc.)
            peer->handle_incoming_datagram(datagram);
        } else {
            itp->second->handle_incoming_datagram(datagram);

            // Make sure disconnected peer is no longer serviced, ever again
            if (itp->second->state() == EnetConnectionState::Disconnected)
                remove_peer(itp->second);
        }
    }
}

void EnetServer::service_peers() {
    for (auto& [ep, peer] : peers_by_ep_)
        peer->service();
}
} // namespace enet
} // namespace luxon
