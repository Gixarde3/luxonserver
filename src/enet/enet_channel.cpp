// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_peer.hpp"

#include <cstring>
#ifndef _WIN32
#include <netdb.h>
#endif

namespace luxon {
namespace enet {
EnetChannel::EnetChannel(uint8_t channel) : channel_(channel) {}

void EnetChannel::clear_all() {
    while (!outgoing_reliable.empty())
        outgoing_reliable.pop();
    while (!outgoing_unreliable.empty())
        outgoing_unreliable.pop();

    incoming_reliable.clear();
    incoming_unreliable.clear();
    while (!incoming_unsequenced.empty())
        incoming_unsequenced.pop();
    incoming_unsequenced_frags.clear();

    incoming_reliable_seq = 0;
    incoming_unreliable_seq = 0;
    outgoing_reliable_seq = 0;
    outgoing_unreliable_seq = 0;
    outgoing_reliable_unsequenced_seq = 0;

    reliable_unsequenced_completely_received = 0;
    reliable_unsequenced_received.clear();
}

bool EnetChannel::queue_incoming_reliable_unsequenced(const EnetCommand& cmd) {
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

    if (cmd.header.command_type == EnetCommandType::EgSendFragmentUnsequenced)
        incoming_unsequenced_frags[cmd.header.reliable_seq] = cmd;
    else
        incoming_unsequenced.push(cmd);
    return true;
}

bool EnetChannel::try_get_fragment(uint32_t reliable_seq, bool sequenced, EnetCommand& out) const {
    if (sequenced) {
        auto it = incoming_reliable.find(reliable_seq);
        if (it == incoming_reliable.end())
            return false;
        out = it->second;
        return true;
    }
    auto it = incoming_unsequenced_frags.find(reliable_seq);
    if (it == incoming_unsequenced_frags.end())
        return false;
    out = it->second;
    return true;
}

void EnetChannel::remove_fragment(uint32_t reliable_seq, bool sequenced) {
    if (sequenced)
        incoming_reliable.erase(reliable_seq);
    else
        incoming_unsequenced_frags.erase(reliable_seq);
}
} // namespace enet
} // namespace luxon
