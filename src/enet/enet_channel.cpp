// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_peer.hpp"

#include <cstring>
#ifdef HAS_NETDB
#include <netdb.h>
#endif

namespace luxon {
namespace enet {
EnetChannel::EnetChannel(uint8_t channel) : incoming_reliable(1), incoming_unreliable(1), incoming_unsequenced_frags(1), channel_(channel) {}

void EnetChannel::clear_all() {
    while (!outgoing_reliable.empty())
        outgoing_reliable.pop();
    while (!outgoing_unreliable.empty())
        outgoing_unreliable.pop();

    incoming_reliable.reset(1);
    incoming_unreliable.reset(1);
    while (!incoming_unsequenced.empty())
        incoming_unsequenced.pop();
    incoming_unsequenced_frags.reset(1);

    incoming_reliable_seq = 0;
    incoming_unreliable_seq = 0;
    outgoing_reliable_seq = 0;
    outgoing_unreliable_seq = 0;
    outgoing_reliable_unsequenced_seq = 0;

    reliable_unsequenced_completely_received = 0;
    reliable_unsequenced_received.clear();
}

void EnetChannel::sync_reliable_window() { incoming_reliable.advance_to(static_cast<uint32_t>(incoming_reliable_seq + 1)); }

void EnetChannel::sync_unreliable_window() { incoming_unreliable.advance_to(static_cast<uint32_t>(incoming_unreliable_seq + 1)); }

void EnetChannel::sync_reliable_unsequenced_fragment_window() {
    incoming_unsequenced_frags.advance_to(static_cast<uint32_t>(reliable_unsequenced_completely_received + 1));
}

bool EnetChannel::queue_incoming_reliable_unsequenced(const EnetCommand& cmd) {
    sync_reliable_unsequenced_fragment_window();

    if (cmd.header.reliable_seq <= reliable_unsequenced_completely_received)
        return false;
    if (reliable_unsequenced_received.find(cmd.header.reliable_seq) != reliable_unsequenced_received.end())
        return false;

    if (cmd.header.reliable_seq == reliable_unsequenced_completely_received + 1)
        reliable_unsequenced_completely_received++;
    else
        reliable_unsequenced_received.insert(cmd.header.reliable_seq);

    while (reliable_unsequenced_received.find(reliable_unsequenced_completely_received + 1) != reliable_unsequenced_received.end()) {
        reliable_unsequenced_completely_received++;
        reliable_unsequenced_received.erase(reliable_unsequenced_completely_received);
    }

    if (cmd.header.command_type == EnetCommandType::EgSendFragmentUnsequenced) {
        auto res = incoming_unsequenced_frags.insert_or_assign(cmd.header.reliable_seq, cmd);
        if (res.out_of_window())
            return false;
    } else {
        incoming_unsequenced.push(cmd);
    }

    sync_reliable_unsequenced_fragment_window();
    return true;
}

bool EnetChannel::try_get_fragment(uint32_t reliable_seq, bool sequenced, EnetCommand& out) const {
    const auto *cmd_ptr = sequenced ? incoming_reliable.find(reliable_seq) : incoming_unsequenced_frags.find(reliable_seq);

    if (cmd_ptr) {
        out = *cmd_ptr;
        return true;
    }
    return false;
}

void EnetChannel::remove_fragment(uint32_t reliable_seq, bool sequenced) {
    if (sequenced)
        incoming_reliable.erase(reliable_seq);
    else
        incoming_unsequenced_frags.erase(reliable_seq);
}
} // namespace enet
} // namespace luxon
