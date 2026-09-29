// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include <luxon/enet_peer.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace luxon::enet {
struct EnetPeerTestAccess {
    static void mark_connected(EnetPeer& peer) {
        peer.state_ = EnetConnectionState::Connected;
    }

    static size_t ack_count(const EnetPeer& peer) {
        return peer.outgoing_ack_pool_.size();
    }
};
} // namespace luxon::enet

int main() {
    using namespace luxon::enet;

    EnetPeer peer(EnetPeerConfig{});
    EnetPeerTestAccess::mark_connected(peer);

    std::vector<uint32_t> dispatched;
    peer.on_payload_command = [&](EnetCommand&& cmd) {
        dispatched.push_back(cmd.header.reliable_seq);
    };

    EnetPacketHeader hdr;
    hdr.challenge = peer.challenge();
    hdr.sent_time = 77;
    const auto send_reliable = [&](uint32_t seq) {
        EnetCommand cmd;
        cmd.header.command_type = EnetCommandType::SendReliable;
        cmd.header.flags = FlagValue::Reliable;
        cmd.header.reliable_seq = seq;
        cmd.set_payload(ByteArray{static_cast<uint8_t>(seq)});
        std::array<EnetCommand, 1> commands{std::move(cmd)};
        peer.handle_incoming_packet(hdr, commands, 20);
    };

    // With base sequence 1 and a 128-command window, sequence 129 is rejected.
    // It must not be ACKed, or the sender will never retransmit the missing gap.
    send_reliable(129);
    assert(EnetPeerTestAccess::ack_count(peer) == 0);
    assert(!peer.dispatch_one());

    // Once the gap is filled, the formerly out-of-window packet is recoverable.
    for (uint32_t seq = 1; seq <= 128; ++seq)
        send_reliable(seq);
    assert(EnetPeerTestAccess::ack_count(peer) == 128);

    for (uint32_t seq = 1; seq <= 128; ++seq)
        assert(peer.dispatch_one());
    assert(dispatched.size() == 128);

    send_reliable(129);
    assert(EnetPeerTestAccess::ack_count(peer) == 129);
    assert(peer.dispatch_one());
    assert(dispatched.size() == 129 && dispatched.back() == 129);

    // A duplicate already accepted remains ACKable so its sender can stop retrying.
    send_reliable(129);
    assert(EnetPeerTestAccess::ack_count(peer) == 130);

    // A single in-window reliable command resets the stalled-window timer.
    EnetPeerConfig progress_config;
    progress_config.incoming_reliable_stall_timeout_ms = 40;
    progress_config.incoming_reliable_stall_quiet_ms = 100;
    EnetPeer progress_peer(progress_config);
    EnetPeerTestAccess::mark_connected(progress_peer);
    EnetPacketHeader progress_hdr;
    progress_hdr.challenge = progress_peer.challenge();
    const auto send_progress_reliable = [&](uint32_t seq) {
        EnetCommand cmd;
        cmd.header.command_type = EnetCommandType::SendReliable;
        cmd.header.flags = FlagValue::Reliable;
        cmd.header.reliable_seq = seq;
        cmd.set_payload(ByteArray{static_cast<uint8_t>(seq)});
        std::array<EnetCommand, 1> commands{std::move(cmd)};
        progress_peer.handle_incoming_packet(progress_hdr, commands, 20);
    };
    int progress_disconnects = 0;
    progress_peer.on_state_changed = [&](EnetConnectionState state) {
        if (state == EnetConnectionState::Disconnected)
            ++progress_disconnects;
    };
    send_progress_reliable(129);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    send_progress_reliable(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    progress_peer.service();
    assert(progress_peer.state() == EnetConnectionState::Connected);
    assert(progress_disconnects == 0);

    // Repeated out-of-window drops with no in-window progress disconnect only
    // this peer; subsequent service ticks do not emit another state change.
    EnetPeerConfig stalled_config;
    stalled_config.incoming_reliable_stall_timeout_ms = 40;
    stalled_config.incoming_reliable_stall_quiet_ms = 100;
    EnetPeer stalled_peer(stalled_config);
    EnetPeerTestAccess::mark_connected(stalled_peer);
    EnetPacketHeader stalled_hdr;
    stalled_hdr.challenge = stalled_peer.challenge();
    const auto send_stalled_reliable = [&] {
        EnetCommand cmd;
        cmd.header.command_type = EnetCommandType::SendReliable;
        cmd.header.flags = FlagValue::Reliable;
        cmd.header.reliable_seq = 129;
        cmd.set_payload(ByteArray{129});
        std::array<EnetCommand, 1> commands{std::move(cmd)};
        stalled_peer.handle_incoming_packet(stalled_hdr, commands, 20);
    };
    int stalled_disconnects = 0;
    stalled_peer.on_state_changed = [&](EnetConnectionState state) {
        if (state == EnetConnectionState::Disconnected)
            ++stalled_disconnects;
    };
    for (int i = 0; i < 5; ++i) {
        send_stalled_reliable();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stalled_peer.service();
    assert(stalled_peer.state() == EnetConnectionState::Disconnected);
    assert(stalled_disconnects == 1);
    stalled_peer.service();
    assert(stalled_disconnects == 1);
}
