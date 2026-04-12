// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_peer.hpp"

#include <bit>
#include <random>
#include <algorithm>
#include <chrono>
#include <cstring>
#ifdef HAS_NETDB
#include <netdb.h>
#endif

namespace luxon {
namespace enet {
static uint32_t random_u32() {
    static std::mt19937 rng{std::random_device{}()};
    return std::uniform_int_distribution<uint32_t>{0, 0xFFFFFFFFu}(rng);
}

EnetDeliveryMode FlagsToEnetDeliveryMode(uint8_t flags) {
    switch (flags) {
    case FlagValue::Reliable:
        return EnetDeliveryMode::Reliable;
    case FlagValue::UnreliableUnsequenced:
        return EnetDeliveryMode::UnreliableUnsequenced;
    case FlagValue::ReliableUnsequenced:
        return EnetDeliveryMode::ReliableUnsequenced;
    case FlagValue::Unreliable:
    default:
        return EnetDeliveryMode::Unreliable;
    }
}

EnetPeer::EnetPeer(EnetPeerConfig cfg) : cfg_(cfg) {
    if (cfg.time_base)
        time_base_ = cfg.time_base;
    else
        time_base_ = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    challenge_ = random_u32();

    channels_.reserve(cfg_.channel_count + 1);
    for (uint8_t i = 0; i < cfg_.channel_count; ++i)
        channels_.push_back(std::make_unique<EnetChannel>(i));
    channels_.push_back(std::make_unique<EnetChannel>(ControlChannel));

    sent_reliable_.reserve(100);
}

bool EnetPeer::use(UdpSocket& sock) {
    if (state_ != EnetConnectionState::Disconnected)
        return false;

    sock_ = &sock;
    remote_.reset();

    state_ = EnetConnectionState::Connecting;
    if (on_state_changed)
        on_state_changed(state_);

    send_connect();

    return true;
}

int EnetPeer::now_ms() const { return create_time_base() - time_base_; }

bool EnetPeer::connect(UdpSocket& sock, const std::string& host, uint16_t port) {
    if (state_ != EnetConnectionState::Disconnected)
        return false;
    if (!sock.connect_to(host, port))
        return false;

    sock_ = &sock;
    remote_.reset();

    sock_->set_nonblocking(true);

    state_ = EnetConnectionState::Connecting;
    if (on_state_changed)
        on_state_changed(state_);

    send_connect();

    return true;
}

void EnetPeer::attach_server_side(UdpSocket& sock, const EnetEndpoint& remote, int16_t assigned_peer_id, uint32_t challenge) {
    sock_ = &sock;
    remote_ = remote;

    peer_id_ = assigned_peer_id;
    challenge_ = challenge;

    sock_->set_nonblocking(true);

    state_ = EnetConnectionState::Connecting;
    if (on_state_changed)
        on_state_changed(state_);

    // Server should respond to a new connect with VerifyConnect
    EnetOutCommand oc;
    oc.cmd.header.command_type = EnetCommandType::VerifyConnect;
    oc.cmd.header.channel_id = ControlChannel;
    oc.cmd.header.flags = FlagValue::Reliable;

    // VerifyConnect payload is 32 bytes: first two bytes are assigned peerID, then 30 bytes unused(?)
    ByteArray payload(32, 0);
    payload[0] = (uint8_t)((uint16_t)assigned_peer_id >> 8);
    payload[1] = (uint8_t)((uint16_t)assigned_peer_id & 0xFF);
    oc.cmd.payload = payload;

    queue_outgoing_reliable(std::move(oc));
    flush_send_queue(false);
}

void EnetPeer::disconnect(bool noflush) {
    if (state_ == EnetConnectionState::Disconnected || state_ == EnetConnectionState::Disconnecting)
        return;
    state_ = EnetConnectionState::Disconnecting;
    if (on_state_changed)
        on_state_changed(state_);

    EnetOutCommand oc;
    oc.cmd.header.command_type = EnetCommandType::Disconnect;
    oc.cmd.header.channel_id = ControlChannel;
    oc.cmd.header.flags = FlagValue::Reliable;
    queue_outgoing_reliable(std::move(oc));

    if (!noflush)
        flush_send_queue(false);

    // Close is done by owner of socket, transition to Disconnected after send
    state_ = EnetConnectionState::Disconnected;
    if (on_state_changed)
        on_state_changed(state_);
    reset_callbacks();
}

size_t EnetPeer::calculate_initial_offset() const {
    if (cfg_.crc_enabled)
        return 16;
    return 12;
}

size_t EnetPeer::calculate_buffer_len() const {
    return cfg_.mtu;
}

size_t EnetPeer::get_fragment_length() {
    // fragmentLength = mtu - 12 - 36
    if (fragment_length_ == 0 || fragment_length_mtu_ != cfg_.mtu) {
        fragment_length_ = (size_t)cfg_.mtu - 12 - 36;
        fragment_length_mtu_ = cfg_.mtu;
    }
    return fragment_length_;
}

EnetChannel& EnetPeer::channel(uint8_t ch) {
    if (ch == ControlChannel)
        return *channels_.back();
    return *channels_.at(ch);
}

const EnetChannel& EnetPeer::channel(uint8_t ch) const {
    if (ch == ControlChannel)
        return *channels_.back();
    return *channels_.at(ch);
}

bool EnetPeer::send_payload(const ByteArray& payload, const EnetSendOptions& opt) {
    if (state_ != EnetConnectionState::Connected)
        return false;
    if (opt.channel >= cfg_.channel_count)
        return false;

    // Determine command type
    EnetCommandType ct = EnetCommandType::SendUnreliable;
    uint8_t flags = FlagValue::Unreliable;

    switch (opt.mode) {
    case EnetDeliveryMode::Unreliable:
        ct = EnetCommandType::SendUnreliable;
        flags = FlagValue::Unreliable;
        break;
    case EnetDeliveryMode::Reliable:
        ct = EnetCommandType::SendReliable;
        flags = FlagValue::Reliable;
        break;
    case EnetDeliveryMode::UnreliableUnsequenced:
        ct = EnetCommandType::SendUnreliableUnsequenced;
        flags = FlagValue::UnreliableUnsequenced;
        break;
    case EnetDeliveryMode::ReliableUnsequenced:
        ct = EnetCommandType::EgSendReliableUnsequenced;
        flags = FlagValue::ReliableUnsequenced;
        break;
    }

    // Send entire command if no fragmentation needed
    const size_t frag_len = get_fragment_length();
    if (payload.size() <= frag_len) {
        if (flags & FlagValue::Reliable) {
            EnetOutCommand oc;
            oc.cmd.header.command_type = ct;
            oc.cmd.header.channel_id = opt.channel;
            oc.cmd.header.flags = flags;
            oc.cmd.payload = payload;
            queue_outgoing_reliable(std::move(oc));
        } else {
            EnetCommand c;
            c.header.command_type = ct;
            c.header.channel_id = opt.channel;
            c.header.flags = flags;
            c.payload = payload;
            queue_outgoing_unreliable(std::move(c));
        }
        return true;
    }

    const bool unseq = (ct == EnetCommandType::EgSendReliableUnsequenced) || (ct == EnetCommandType::SendUnreliableUnsequenced) ||
                       (ct == EnetCommandType::EgSendFragmentUnsequenced);

    EnetChannel& ch = channel(opt.channel);
    const uint32_t start_seq = (unseq ? ch.outgoing_reliable_unsequenced_seq : ch.outgoing_reliable_seq) + 1;
    const uint32_t frag_count = (uint32_t)((payload.size() + frag_len - 1) / frag_len);

    // Send fragments if any
    for (uint32_t frag_no = 0; frag_no < frag_count; ++frag_no) {
        const size_t off = (size_t)frag_no * frag_len;
        const size_t len = std::min(frag_len, payload.size() - off);

        EnetOutCommand oc;
        oc.cmd.header.command_type = unseq ? EnetCommandType::EgSendFragmentUnsequenced : EnetCommandType::SendFragment;
        oc.cmd.header.channel_id = opt.channel;
        oc.cmd.header.flags = unseq ? FlagValue::ReliableUnsequenced : FlagValue::Reliable;

        oc.cmd.fragment_start_seq = start_seq;
        oc.cmd.fragment_count = frag_count;
        oc.cmd.fragment_number = frag_no;
        oc.cmd.fragment_total_length = (uint32_t)payload.size();
        oc.cmd.fragment_offset = (uint32_t)off;

        oc.cmd.payload.assign(payload.begin() + off, payload.begin() + off + len);

        queue_outgoing_reliable(std::move(oc));
    }

    return true;
}

void EnetPeer::queue_outgoing_ack(const EnetCommand& received_reliable_cmd, uint32_t sent_time) {
    // Create 20-byte ACK command blob
    EnetCommand ack;
    const bool is_unseq =
        (received_reliable_cmd.header.flags & FlagValue::UnreliableUnsequenced) != 0 || (received_reliable_cmd.header.flags == FlagValue::ReliableUnsequenced);

    ack.header.command_type = is_unseq ? EnetCommandType::EgAcknowledgeUnsequenced : EnetCommandType::Acknowledge;
    ack.header.channel_id = received_reliable_cmd.header.channel_id;
    ack.header.flags = 0;
    ack.header.reserved = received_reliable_cmd.header.reserved;
    ack.header.reliable_seq = received_reliable_cmd.header.reliable_seq;
    ack.ack_received_reliable_sequence_number = received_reliable_cmd.header.reliable_seq;
    ack.ack_received_sent_time = sent_time;

    // Serialize this ACK command alone into a 20-byte buffer and push to pool
    ByteArray buf;
    buf.reserve(20);
    // Header
    buf.push_back(static_cast<uint8_t>(ack.header.command_type));
    buf.push_back(ack.header.channel_id);
    buf.push_back(ack.header.flags);
    buf.push_back(ack.header.reserved);

    auto write_u32_be = [&](uint32_t v) {
        uint32_t be = v;
        if constexpr (std::endian::native == std::endian::little)
            be = std::byteswap(be);
        const uint8_t *p = reinterpret_cast<const uint8_t *>(&be);
        buf.insert(buf.end(), p, p + 4);
    };

    // command_length=20, reliable_seq=0 (writes Size=20 then reliableSequenceNumber=0)
    // Header reliable_sequence_number field is 0 for ACK commands!
    write_u32_be(20);
    write_u32_be(0);
    write_u32_be(ack.ack_received_reliable_sequence_number);
    write_u32_be(ack.ack_received_sent_time);

    if (buf.size() != 20)
        throw std::runtime_error("ACK serialization size mismatch");
    outgoing_ack_pool_.push_back(std::move(buf));
}

void EnetPeer::queue_sent_reliable(EnetOutCommand& outcmd) {
    outcmd.command_sent_time = time_int_;
    outcmd.command_sent_count++;

    if (outcmd.round_trip_timeout == 0) {
        outcmd.round_trip_timeout = rtt_ + 4 * rtt_var_;
        outcmd.timeout_time = time_int_ + cfg_.disconnect_timeout_ms;
    } else if (outcmd.command_sent_count > cfg_.fast_resend_count + 1) {
        outcmd.round_trip_timeout *= 2;
    }

    if (sent_reliable_.empty()) {
        int t = outcmd.command_sent_time + outcmd.round_trip_timeout;
        if (timeout_int_ == 0 || t < timeout_int_)
            timeout_int_ = t;
    }

    sent_reliable_.push_back(outcmd);
}

void EnetPeer::queue_outgoing_reliable(EnetOutCommand outcmd) {
    EnetChannel& ch = channel(outcmd.cmd.header.channel_id);

    if (outcmd.cmd.header.reliable_seq == 0) {
        const bool unseq = (outcmd.cmd.header.flags & FlagValue::UnreliableUnsequenced) != 0 || (outcmd.cmd.header.flags == FlagValue::ReliableUnsequenced);
        if (unseq)
            outcmd.cmd.header.reliable_seq = ++ch.outgoing_reliable_unsequenced_seq;
        else
            outcmd.cmd.header.reliable_seq = ++ch.outgoing_reliable_seq;
    }
    ch.outgoing_reliable.push(std::move(outcmd));
}

void EnetPeer::queue_outgoing_unreliable(EnetCommand cmd) {
    EnetChannel& ch = channel(cmd.header.channel_id);

    const bool unseq = (cmd.header.flags == FlagValue::UnreliableUnsequenced);
    if (unseq) {
        cmd.header.reliable_seq = 0;
        cmd.unsequenced_group_number = ++outgoing_unsequenced_group_;
    } else {
        cmd.header.reliable_seq = ch.outgoing_reliable_seq;
        cmd.unreliable_seq = ++ch.outgoing_unreliable_seq;
    }

    ch.outgoing_unreliable.push(std::move(cmd));
}

bool EnetPeer::are_reliable_commands_in_transit() const {
    for (size_t i = 0; i < cfg_.channel_count; ++i)
        if (!channels_[i]->outgoing_reliable.empty())
            return true;
    if (!channels_.back()->outgoing_reliable.empty())
        return true;
    return false;
}

void EnetPeer::update_rtt(int last_rtt) {
    if (last_rtt < 0)
        return;
    rtt_var_ -= rtt_var_ / 4;
    if (last_rtt >= rtt_) {
        rtt_ += (last_rtt - rtt_) / 8;
        rtt_var_ += (last_rtt - rtt_) / 4;
    } else {
        rtt_ += (last_rtt - rtt_) / 8;
        rtt_var_ -= (last_rtt - rtt_) / 4;
    }
}

std::optional<EnetOutCommand> EnetPeer::remove_sent_reliable(uint32_t ack_seq, uint8_t channel_id, bool is_unsequenced) {
    for (size_t i = 0; i < sent_reliable_.size(); ++i) {
        const auto& s = sent_reliable_[i];
        if (s.cmd.header.reliable_seq == ack_seq && s.cmd.header.channel_id == channel_id) {

            const bool s_unseq = (s.cmd.header.flags == FlagValue::ReliableUnsequenced) ||
                                 (s.cmd.header.command_type == EnetCommandType::EgSendReliableUnsequenced) ||
                                 (s.cmd.header.command_type == EnetCommandType::EgSendFragmentUnsequenced);
            if (s_unseq != is_unsequenced)
                continue;

            EnetOutCommand ret = s;
            sent_reliable_.erase(sent_reliable_.begin() + (ptrdiff_t)i);
            if (!sent_reliable_.empty())
                timeout_int_ = time_int_ + 25;
            return ret;
        }
    }
    return std::nullopt;
}

void EnetPeer::handle_fragment(const EnetCommand& fragment_cmd) {
    EnetChannel& ch = channel(fragment_cmd.header.channel_id);
    const bool sequenced = (fragment_cmd.header.command_type == EnetCommandType::SendFragment);

    EnetCommand start;
    if (!ch.try_get_fragment(fragment_cmd.fragment_start_seq, sequenced, start))
        return;

    for (uint32_t s = fragment_cmd.fragment_start_seq; s < fragment_cmd.fragment_start_seq + fragment_cmd.fragment_count; ++s) {
        EnetCommand tmp;
        if (!ch.try_get_fragment(s, sequenced, tmp))
            return;
    }

    ByteArray full(fragment_cmd.fragment_total_length, 0);

    for (uint32_t s = fragment_cmd.fragment_start_seq; s < fragment_cmd.fragment_start_seq + fragment_cmd.fragment_count; ++s) {
        EnetCommand frag;
        ch.try_get_fragment(s, sequenced, frag);

        std::memcpy(full.data() + frag.fragment_offset, frag.payload.data(), frag.payload.size());
        ch.remove_fragment(frag.header.reliable_seq, sequenced);
    }

    EnetCommand combined = start;
    combined.payload = std::move(full);

    combined.header.reliable_seq = fragment_cmd.fragment_start_seq + fragment_cmd.fragment_count - 1;

    if (sequenced) {
        combined.header.command_type = EnetCommandType::SendReliable;
        combined.header.flags = FlagValue::Reliable;
        ch.incoming_reliable[fragment_cmd.fragment_start_seq] = std::move(combined);
    } else {
        combined.header.command_type = EnetCommandType::EgSendReliableUnsequenced;
        combined.header.flags = FlagValue::ReliableUnsequenced;
        ch.incoming_unsequenced.push(std::move(combined));
    }
}

bool EnetPeer::queue_incoming_command(const EnetCommand& cmd) {
    EnetChannel& ch = channel(cmd.header.channel_id);

    const bool reliable = (cmd.header.flags & FlagValue::Reliable) != 0;
    const bool unsequenced = (cmd.header.flags & FlagValue::UnreliableUnsequenced) != 0;

    if (reliable) {
        if (unsequenced)
            return ch.queue_incoming_reliable_unsequenced(cmd);
        if (cmd.header.reliable_seq <= ch.incoming_reliable_seq)
            return false;
        if (ch.incoming_reliable.find(cmd.header.reliable_seq) != ch.incoming_reliable.end())
            return false;
        ch.incoming_reliable[cmd.header.reliable_seq] = cmd;
        return true;
    }

    // Unreliable
    if (cmd.header.flags == FlagValue::Unreliable) {
        // Sequenced unreliable: compare against dispatched reliable/unreliable
        if (cmd.header.reliable_seq < ch.incoming_reliable_seq)
            return true;
        if (cmd.unreliable_seq <= ch.incoming_unreliable_seq)
            return true;
        if (ch.incoming_unreliable.find(cmd.unreliable_seq) != ch.incoming_unreliable.end())
            return false;

        // apply LimitOfUnreliableCommands pruning later in dispatch
        ch.incoming_unreliable[cmd.unreliable_seq] = cmd;
        return true;
    }

    if (cmd.header.flags == FlagValue::UnreliableUnsequenced) {
        const uint32_t g = cmd.unsequenced_group_number;
        const uint32_t idx = g % 128;
        if (g >= incoming_unsequenced_group_ + 128) {
            incoming_unsequenced_group_ = g - idx;
            unsequenced_window_[0] = unsequenced_window_[1] = unsequenced_window_[2] = unsequenced_window_[3] = 0;
        } else {
            if (g < incoming_unsequenced_group_)
                return false;
            const uint32_t word = idx / 32;
            const uint32_t bit = idx % 32;
            if (unsequenced_window_[word] & (1u << bit))
                return false;
        }
        unsequenced_window_[idx / 32] |= (1u << (idx % 32));
        ch.incoming_unsequenced.push(cmd);
        return true;
    }

    return false;
}

void EnetPeer::execute_command(const EnetCommand& cmd) {
    switch (cmd.header.command_type) {
    case EnetCommandType::Acknowledge:
    case EnetCommandType::EgAcknowledgeUnsequenced: {
        time_last_ack_receive_ = time_int_;
        last_rtt_ = time_int_ - (int)cmd.ack_received_sent_time;
        if (last_rtt_ < 0 || last_rtt_ > rtt_ * 4)
            last_rtt_ = rtt_ * 4;

        const bool is_unseq_ack = (cmd.header.command_type == EnetCommandType::EgAcknowledgeUnsequenced);
        auto sent = remove_sent_reliable(cmd.ack_received_reliable_sequence_number, cmd.header.channel_id, is_unseq_ack);
        if (sent) {
            if (sent->cmd.header.command_type == EnetCommandType::VerifyConnect && state_ == EnetConnectionState::Connecting) {
                state_ = EnetConnectionState::Connected;
                if (on_state_changed)
                    on_state_changed(state_);
            }

            if (sent->cmd.header.command_type == EnetCommandType::Connect)
                rtt_ = last_rtt_;
            else
                update_rtt(last_rtt_);
        }
        break;
    }

    case EnetCommandType::VerifyConnect: {
        // Client side: assign peer_id from first two payload bytes if we were connecting
        if (state_ == EnetConnectionState::Connecting && cmd.payload.size() >= 2) {
            int16_t pid = (int16_t)((cmd.payload[0] << 8) | cmd.payload[1]);
            if (peer_id_ == -1 || peer_id_ == -2)
                peer_id_ = pid;

            state_ = EnetConnectionState::Connected;
            if (on_state_changed)
                on_state_changed(state_);
        }
        break;
    }

    case EnetCommandType::Disconnect: {
        // Make sure disconnect command is still acknowledged
        flush_send_queue(true);

        state_ = EnetConnectionState::Disconnected;
        if (on_state_changed)
            on_state_changed(state_);
        reset_callbacks();
        break;
    }

    case EnetCommandType::Ping:
        // Does nothing, ping is always reliable so will ACK in response
        break;

    case EnetCommandType::SendReliable:
    case EnetCommandType::EgSendReliableUnsequenced:
    case EnetCommandType::SendUnreliable:
    case EnetCommandType::SendUnreliableUnsequenced: {
        if (state_ == EnetConnectionState::Connected) {
            if (queue_incoming_command(cmd)) {
                // If reliable, will be acked in handle_incoming_datagram
            }
        }
        break;
    }

    case EnetCommandType::SendFragment:
    case EnetCommandType::EgSendFragmentUnsequenced: {
        if (state_ == EnetConnectionState::Connected) {
            // Check fragment sanity
            if (cmd.fragment_number >= cmd.fragment_count || cmd.fragment_offset >= cmd.fragment_total_length ||
                cmd.fragment_offset + cmd.payload.size() > cmd.fragment_total_length) {
                // Invalid fragment, drop immediately to avoid corrupting the reassembly buffer
                break;
            }

            if (queue_incoming_command(cmd))
                handle_fragment(cmd);
        }
        break;
    }

    default:
        // ignore
        break;
    }
}

void EnetPeer::handle_incoming_datagram(const ByteArray& datagram) {
    try {
        EnetPacketHeader hdr;
        auto cmds = parse_packet(datagram, hdr);

        bytes_in_ += datagram.size();

        // Challenge check (plain or encrypted)
        if (hdr.challenge != challenge_) {
            packet_loss_by_challenge_++;
            return;
        }

        time_int_ = now_ms();

        for (const auto& c : cmds) {
            // If reliable, enqueue ack immediately
            const bool reliable = (c.header.flags & FlagValue::Reliable) != 0;
            if (reliable)
                queue_outgoing_ack(c, hdr.sent_time);

            // Execute ACK and VerifyConnect immediately
            if (c.header.command_type == EnetCommandType::Acknowledge || c.header.command_type == EnetCommandType::EgAcknowledgeUnsequenced ||
                c.header.command_type == EnetCommandType::VerifyConnect) {
                execute_command(c);
            } else {
                // Defer others to keep ordering consistent, we simply execute now and rely on channel ordering for dispatch
                execute_command(c);
            }
        }
    } catch (const CRCError&) {
        packet_loss_by_crc_++;
    } catch (const ProtocolError&) {
        // TODO: Handle this somehow, maybe?
    }
}

bool EnetPeer::dispatch_one() {
    // Priority:
    // 1. Incoming_unsequenced queue
    // 2. Unreliable in-order
    // 3. Reliable in-order (next reliable seq)

    for (size_t ci = 0; ci < channels_.size(); ++ci) {
        EnetChannel& ch = *channels_[ci];

        if (!ch.incoming_unsequenced.empty()) {
            EnetCommand cmd = ch.incoming_unsequenced.front();
            ch.incoming_unsequenced.pop();
            if (on_payload_command)
                on_payload_command(cmd);
            return true;
        }

        if (!ch.incoming_unreliable.empty()) {
            // Prune excess packets, oldest first
            if (cfg_.max_pending_unreliable_commands > 0) {
                while ((int)ch.incoming_unreliable.size() > cfg_.max_pending_unreliable_commands) {
                    // Remove the oldest (first) element
                    ch.incoming_unreliable.erase(ch.incoming_unreliable.begin());
                }
            }

            // Now find the best dispatch candidate (smallest key that is >= incoming_unreliable_seq and has reliable_seq <= incoming_reliable_seq)
            uint32_t best = UINT32_MAX;
            std::vector<uint32_t> to_remove;

            for (auto& [k, v] : ch.incoming_unreliable) {
                if (k < ch.incoming_unreliable_seq || v.header.reliable_seq < ch.incoming_reliable_seq) {
                    to_remove.push_back(k);
                    continue;
                }

                if (k < best && v.header.reliable_seq <= ch.incoming_reliable_seq)
                    best = k;
            }
            for (auto k : to_remove)
                ch.incoming_unreliable.erase(k);

            if (best != UINT32_MAX) {
                EnetCommand cmd = ch.incoming_unreliable[best];
                ch.incoming_unreliable.erase(best);
                ch.incoming_unreliable_seq = cmd.unreliable_seq;
                if (on_payload_command)
                    on_payload_command(cmd);
                return true;
            }
        }

        // Reliable in-order
        auto it = ch.incoming_reliable.find(ch.incoming_reliable_seq + 1);
        if (it != ch.incoming_reliable.end()) {
            const EnetCommand& peek = it->second;

            if (peek.header.command_type == EnetCommandType::SendFragment || peek.header.command_type == EnetCommandType::EgSendFragmentUnsequenced)
                // Fragment not reassembled yet, so do not dispatch anything for this channel
                continue;

            EnetCommand cmd = it->second;
            ch.incoming_reliable_seq = cmd.header.reliable_seq;
            ch.incoming_reliable.erase(it);

            if (on_payload_command)
                on_payload_command(cmd);
            return true;
        }
    }

    return false;
}

int EnetPeer::create_time_base() {
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void EnetPeer::sync_local_time_to_remote_dynamic(const EnetPeer& remote) {
    // Calculate difference between old time base and remote's time base
    const int time_diff = time_base_ - remote.time_base_;

    // Adopt remote's time base
    time_base_ = remote.time_base_;

    // Shift all internal tracked time offsets so active timers don't break
    time_int_ += time_diff;

    if (timeout_int_ != 0)
        timeout_int_ += time_diff;

    time_last_ack_receive_ += time_diff;
    time_last_send_ack_ += time_diff;
    time_last_send_outgoing_ += time_diff;

    // Shift the tracking time on all reliable commands currently in transit
    for (auto& cmd : sent_reliable_) {
        cmd.command_sent_time += time_diff;
        cmd.timeout_time += time_diff;
    }
}

void EnetPeer::send_connect() {
    // Send inital connect command
    EnetOutCommand oc;
    oc.cmd.header.command_type = EnetCommandType::Connect;
    oc.cmd.header.channel_id = ControlChannel;
    oc.cmd.header.flags = FlagValue::Reliable;

    // Reproduce those bytes to be wire-compatible
    // These constants seem somewhat random but are apparently required?
    // In Wireshark these constants always seem to be sent, no matter what
    // They're probably just magic constants
    ByteArray payload(32, 0);
    // [2..3] mtu
    payload[2] = (uint8_t)(cfg_.mtu >> 8);
    payload[3] = (uint8_t)(cfg_.mtu & 0xFF);
    payload[6] = 128;
    payload[11] = cfg_.channel_count;
    payload[22] = 19;
    payload[23] = 136;
    payload[27] = 2;
    payload[31] = 2;
    oc.cmd.payload = payload;

    queue_outgoing_reliable(std::move(oc));
    flush_send_queue(false);
}

bool EnetPeer::flush_send_queue(bool only_acks) {
    if (!sock_ || !sock_->is_open())
        return true;

    // Attempt to send still queued datagrams first
    while (!datagram_queue_.empty()) {
        if (send_datagram(datagram_queue_.front()))
            datagram_queue_.pop();
        else
            return false;
    }

    // Update time
    time_int_ = now_ms();
    if (only_acks)
        time_last_send_ack_ = time_int_;
    else
        time_last_send_outgoing_ = time_int_;

    // Resend logic
    if (!only_acks && time_int_ > timeout_int_ && !sent_reliable_.empty()) {
        std::vector<size_t> to_resend;
        to_resend.reserve(sent_reliable_.size());

        for (size_t i = 0; i < sent_reliable_.size(); ++i) {
            auto& s = sent_reliable_[i];
            if (s.round_trip_timeout != 0 && (time_int_ - s.command_sent_time) > s.round_trip_timeout) {
                if (s.command_sent_count > cfg_.max_resends || time_int_ > s.timeout_time) {
                    state_ = EnetConnectionState::Stale;
                    if (on_state_changed)
                        on_state_changed(state_);
                    disconnect(true);
                    return true;
                }
                to_resend.push_back(i);
            }
        }

        // Extract lost commands first
        std::vector<EnetOutCommand> extracted;
        extracted.reserve(to_resend.size());

        std::sort(to_resend.begin(), to_resend.end());
        for (auto it = to_resend.rbegin(); it != to_resend.rend(); ++it) {
            extracted.emplace_back(std::move(sent_reliable_[*it]));
            sent_reliable_.erase(sent_reliable_.begin() + static_cast<ptrdiff_t>(*it));
        }

        // Re-queue in the correct chronological (FIFO) order
        for (auto it = extracted.rbegin(); it != extracted.rend(); ++it)
            queue_outgoing_reliable(std::move(*it));

        if (!sent_reliable_.empty())
            timeout_int_ = time_int_ + 25;
    }

    // Ping injection
    if (!only_acks && state_ == EnetConnectionState::Connected && cfg_.time_ping_interval_ms > 0 && sent_reliable_.empty() &&
        (time_int_ - time_last_ack_receive_) > cfg_.time_ping_interval_ms && !are_reliable_commands_in_transit()) {

        EnetOutCommand ping;
        ping.cmd.header.command_type = EnetCommandType::Ping;
        ping.cmd.header.channel_id = ControlChannel;
        ping.cmd.header.flags = FlagValue::Reliable;
        queue_outgoing_reliable(std::move(ping));
    }

    // Keep building as many datagrams as we can
    while (true) {
        // Build packet up to MTU
        std::vector<EnetCommand> cmds_to_send;
        cmds_to_send.reserve(64);
        bool has_reliable_data = false;

        const size_t header_size = calculate_initial_offset();
        const size_t mtu = cfg_.mtu;

        size_t used = header_size;
        outgoing_command_count_ = 0;

        // Serialize ACK pool first (each ack is exactly 20 bytes)
        while (!outgoing_ack_pool_.empty()) {
            if (used + 20 > mtu)
                break;

            // Reuse command parser on a fake buffer that begins with the ack header (command parser expects command header at 0)
            // We'll just parse via manual decode:
            const ByteArray& b = outgoing_ack_pool_.front();
            if (b.size() != 20) {
                outgoing_ack_pool_.pop_front();
                continue;
            }

            EnetCommand ack;
            ack.header.command_type = static_cast<EnetCommandType>(b[0]);
            ack.header.channel_id = b[1];
            ack.header.flags = b[2];
            ack.header.reserved = b[3];
            // length and reliable_seq are ignored by ack semantics, but parse anyways:
            ack.header.command_length = (uint32_t)((b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7]);
            ack.header.reliable_seq = (uint32_t)((b[8] << 24) | (b[9] << 16) | (b[10] << 8) | b[11]);
            ack.ack_received_reliable_sequence_number = (uint32_t)((b[12] << 24) | (b[13] << 16) | (b[14] << 8) | b[15]);
            ack.ack_received_sent_time = (uint32_t)((b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19]);

            cmds_to_send.push_back(std::move(ack));
            outgoing_ack_pool_.pop_front();
            used += 20;
            outgoing_command_count_++;
        }

        if (!only_acks) {
            // outgoing commands per channel
            for (auto& chptr : channels_) {
                EnetChannel& ch = *chptr;

                // reliable first
                while (!ch.outgoing_reliable.empty()) {
                    auto oc = ch.outgoing_reliable.front();
                    const auto length = compute_command_length(oc.cmd);
                    if (used + length > mtu)
                        break;

                    // Mark as sent reliable and track for resends
                    queue_sent_reliable(oc);

                    cmds_to_send.push_back(std::move(oc.cmd));
                    ch.outgoing_reliable.pop();
                    used += length;
                    outgoing_command_count_++;

                    has_reliable_data = true;
                }

                // unreliable
                while (!ch.outgoing_unreliable.empty()) {
                    EnetCommand c = ch.outgoing_unreliable.front();

                    const auto length = compute_command_length(c);
                    if (used + length > mtu)
                        break;

                    cmds_to_send.push_back(std::move(c));
                    ch.outgoing_unreliable.pop();
                    used += length;
                    outgoing_command_count_++;
                }
            }
        }

        if (cmds_to_send.empty())
            break;

        EnetPacketHeader hdr;
        hdr.peer_id = peer_id_;
        hdr.type = cfg_.crc_enabled ? EnetUdpHeaderType::PlainWithCrc : EnetUdpHeaderType::PlainNoCrc;
        hdr.command_count = outgoing_command_count_;
        hdr.sent_time = (uint32_t)time_int_;
        hdr.challenge = challenge_;

        ByteArray datagram = create_packet(hdr, cmds_to_send);
        bytes_out_ += datagram.size();

        // Try to send datagram
        if (!send_datagram(datagram)) {
            // Failure, send again later if there's any reliable data in it
            if (has_reliable_data) {
                datagram_queue_.emplace(std::move(datagram));
                return false;
            }
        }
    }

    // No datagrams had to be queued up
    return true;
}

bool EnetPeer::send_datagram(const ByteArray& datagram) {
    if (remote_)
        return sock_->send_to(datagram.data(), datagram.size(), *remote_);
    else
        return sock_->send_connected(datagram.data(), datagram.size());
}

bool EnetPeer::send_outgoing_commands() { return flush_send_queue(false); }
bool EnetPeer::send_acks_only() { return flush_send_queue(true); }

bool EnetPeer::service() {
    if (state_ == EnetConnectionState::Disconnected)
        return true;

    // Dispatch incoming until empty
    while (dispatch_one())
        ;

    // Send some outgoing commands
    return send_outgoing_commands();
}
} // namespace enet
} // namespace luxon
