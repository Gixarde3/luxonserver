// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "enet_protocol.hpp"

#include <bit>
#include <cstring>
#include <stdexcept>

namespace luxon {
namespace enet {
namespace {
static inline uint16_t read_u16_be(const uint8_t *p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    if constexpr (std::endian::native == std::endian::little)
        v = std::byteswap(v);
    return v;
}

static inline int16_t read_i16_be(const uint8_t *p) { return static_cast<int16_t>(read_u16_be(p)); }

static inline uint32_t read_u32_be(const uint8_t *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    if constexpr (std::endian::native == std::endian::little)
        v = std::byteswap(v);
    return v;
}

static inline void write_u16_be(DatagramBuffer& out, unsigned& position, uint16_t v) {
    if constexpr (std::endian::native == std::endian::little)
        v = std::byteswap(v);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&v);

    out.at(position++) = p[0];
    out.at(position++) = p[1];
}

static inline void write_i16_be(DatagramBuffer& out, unsigned& position, int16_t v) { write_u16_be(out, position, static_cast<uint16_t>(v)); }

static inline void write_u32_be(DatagramBuffer& out, unsigned& position, uint32_t v) {
    if constexpr (std::endian::native == std::endian::little)
        v = std::byteswap(v);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&v);

    out.at(position++) = p[0];
    out.at(position++) = p[1];
    out.at(position++) = p[2];
    out.at(position++) = p[3];
}

static inline void ensure_available(size_t need, size_t have, const char *msg) {
    if (have < need)
        throw ProtocolError(msg);
}

static uint32_t *crc_table_ptr() {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        const uint32_t poly = 0xEDB88320u;
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? ((c >> 1) ^ poly) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }
    return table;
}
} // namespace

uint32_t calculate_crc(const uint8_t *data, size_t length, unsigned leading_zeros = 4) {
    uint32_t crc = 0xFFFFFFFFu;
    const uint32_t *tbl = crc_table_ptr();

    // Cap leading_zeros to avoid overflowing the total length
    size_t zeros_count = (leading_zeros < length) ? leading_zeros : length;

    // Process the leading zeros
    for (size_t i = 0; i < zeros_count; ++i)
        crc = (crc >> 8) ^ tbl[crc & 0xFF];

    // Process the actual data using pointers
    const uint8_t *p = data + zeros_count;
    const uint8_t *end = data + length;

    while (p != end)
        crc = (crc >> 8) ^ tbl[(*p++) ^ (crc & 0xFF)];

    return crc;
}

EnetCommand parse_command(const uint8_t *data, size_t data_len, size_t& inout_offset) {
    ensure_available(inout_offset + kCmdHeaderSize, data_len, "ENet: not enough for command header");

    EnetCommand cmd;
    cmd.header.command_type = static_cast<EnetCommandType>(data[inout_offset + 0]);
    cmd.header.channel_id = data[inout_offset + 1];
    cmd.header.flags = data[inout_offset + 2];
    cmd.header.reserved = data[inout_offset + 3];
    cmd.header.command_length = read_u32_be(data + inout_offset + 4);
    cmd.header.reliable_seq = read_u32_be(data + inout_offset + 8);

    const size_t cmd_start = inout_offset;
    const size_t cmd_len = cmd.header.command_length;

    if (cmd_len < kCmdHeaderSize)
        throw ProtocolError("ENet: invalid command length (<12)");
    if (cmd_start + cmd_len > data_len)
        throw ProtocolError("ENet: command length exceeds datagram size");

    size_t off = cmd_start + kCmdHeaderSize;

    auto remaining_in_cmd = [&]() -> size_t { return (cmd_start + cmd_len) - off; };

    switch (cmd.header.command_type) {
    case EnetCommandType::Acknowledge:
    case EnetCommandType::EgAcknowledgeUnsequenced: {
        ensure_available(8, remaining_in_cmd(), "ENet: ack missing fields");
        cmd.ack_received_reliable_sequence_number = read_u32_be(data + off);
        off += 4;
        cmd.ack_received_sent_time = read_u32_be(data + off);
        off += 4;
        break;
    }
    case EnetCommandType::SendUnreliable: {
        ensure_available(4, remaining_in_cmd(), "ENet: unreliable missing seq");
        cmd.unreliable_seq = read_u32_be(data + off);
        off += 4;
        break;
    }
    case EnetCommandType::SendUnreliableUnsequenced: {
        ensure_available(4, remaining_in_cmd(), "ENet: unsequenced missing group");
        cmd.unsequenced_group_number = read_u32_be(data + off);
        off += 4;
        break;
    }
    case EnetCommandType::SendFragment:
    case EnetCommandType::EgSendFragmentUnsequenced: {
        ensure_available(20, remaining_in_cmd(), "ENet: fragment missing fields");
        cmd.fragment_start_seq = read_u32_be(data + off);
        off += 4;
        cmd.fragment_count = read_u32_be(data + off);
        off += 4;
        cmd.fragment_number = read_u32_be(data + off);
        off += 4;
        cmd.fragment_total_length = read_u32_be(data + off);
        off += 4;
        cmd.fragment_offset = read_u32_be(data + off);
        off += 4;
        break;
    }
    default:
        // No extra fixed fields
        break;
    }

    // Remaining bytes in this command are payload
    const size_t payload_len = (cmd_start + cmd_len) - off;
    if (payload_len > 0)
        cmd.set_payload(DatagramView{data + off, data + off + payload_len});

    inout_offset = cmd_start + cmd_len;
    return cmd;
}

uint32_t compute_command_length(const EnetCommand& cmd) {
    uint32_t fres = static_cast<uint32_t>(kCmdHeaderSize);

    switch (cmd.header.command_type) {
    case EnetCommandType::Acknowledge:
    case EnetCommandType::EgAcknowledgeUnsequenced:
        fres += 8;
        fres += static_cast<uint32_t>(cmd.get_payload_size());
        break;

    case EnetCommandType::SendUnreliable:
        fres += 4;
        fres += static_cast<uint32_t>(cmd.get_payload_size());
        break;

    case EnetCommandType::SendUnreliableUnsequenced:
        fres += 4;
        fres += static_cast<uint32_t>(cmd.get_payload_size());
        break;

    case EnetCommandType::SendFragment:
    case EnetCommandType::EgSendFragmentUnsequenced:
        fres += 20;
        fres += static_cast<uint32_t>(cmd.get_payload_size());
        break;

    default:
        fres += static_cast<uint32_t>(cmd.get_payload_size());
        break;
    }

    return fres;
}

void write_command(DatagramBuffer& out, unsigned& position, const EnetCommand& cmd) {
    // Compute total length
    const uint32_t total_len = compute_command_length(cmd);

    out.at(position++) = static_cast<uint8_t>(cmd.header.command_type);
    out.at(position++) = cmd.header.channel_id;
    out.at(position++) = cmd.header.flags;
    out.at(position++) = cmd.header.reserved;
    write_u32_be(out, position, total_len);
    write_u32_be(out, position, cmd.header.reliable_seq);

    switch (cmd.header.command_type) {
    case EnetCommandType::Acknowledge:
    case EnetCommandType::EgAcknowledgeUnsequenced:
        write_u32_be(out, position, cmd.ack_received_reliable_sequence_number);
        write_u32_be(out, position, cmd.ack_received_sent_time);
        break;

    case EnetCommandType::SendUnreliable:
        write_u32_be(out, position, cmd.unreliable_seq);
        break;

    case EnetCommandType::SendUnreliableUnsequenced:
        write_u32_be(out, position, cmd.unsequenced_group_number);
        break;

    case EnetCommandType::SendFragment:
    case EnetCommandType::EgSendFragmentUnsequenced:
        write_u32_be(out, position, cmd.fragment_start_seq);
        write_u32_be(out, position, cmd.fragment_count);
        write_u32_be(out, position, cmd.fragment_number);
        write_u32_be(out, position, cmd.fragment_total_length);
        write_u32_be(out, position, cmd.fragment_offset);
        break;

    default:
        break;
    }

    if (!cmd.is_payload_empty()) {
        out.at(position + cmd.get_payload_size() - 1); // Quick bounds check
        const auto payload = cmd.get_payload();
        std::copy(payload.begin(), payload.end(), out.begin() + position);
        position += cmd.get_payload_size();
    }
}

std::vector<EnetCommand> parse_packet(std::span<const uint8_t> datagram, EnetPacketHeader& out_header) {
    if (datagram.size() < 12)
        throw ProtocolError("ENet: datagram too small");

    const uint8_t *p = datagram.data();
    size_t off = 0;

    out_header.peer_id = read_i16_be(p + off);
    off += 2;
    out_header.type = static_cast<EnetUdpHeaderType>(p[off++]);

    if (out_header.type == EnetUdpHeaderType::Encrypted)
        throw ProtocolError("ENet: encrypted datagram to be parsed but not supported");

    // Plain layout:
    // [0..1]=peerId, [2]=0 or 204, [3]=commandCount, [4..7]=sent_time, [8..11]=challenge, [12..15]=crc (optional)
    out_header.command_count = p[off++];
    out_header.sent_time = read_u32_be(p + off);
    off += 4;
    out_header.challenge = read_u32_be(p + off);
    off += 4;

    if (out_header.type == EnetUdpHeaderType::PlainWithCrc) {
        ensure_available(off + 4, datagram.size(), "ENet: CRC flag but no CRC field");
        uint32_t recv_crc = read_u32_be(p + off);
        out_header.crc32 = recv_crc;

        uint32_t calc = calculate_crc(datagram.data(), datagram.size(), 4);
        if (calc != recv_crc)
            throw CRCError("ENet: CRC mismatch");
        off += 4;
    } else {
        out_header.crc32.reset();
    }

    std::vector<EnetCommand> cmds;
    cmds.reserve(out_header.command_count);

    for (uint8_t i = 0; i < out_header.command_count; ++i)
        cmds.push_back(parse_command(p, datagram.size(), off));

    return cmds;
}

size_t create_packet(DatagramBuffer& out, EnetPacketHeader header, const std::vector<EnetCommand>& commands) {
    header.command_count = static_cast<uint8_t>(commands.size());

    if (header.type == EnetUdpHeaderType::Encrypted)
        throw ProtocolError("ENet: encrypted datagram to be created but not supported");

    unsigned position{};

    write_i16_be(out, position, header.peer_id);
    out.at(position++) = static_cast<uint8_t>(header.type);
    out.at(position++) = header.command_count;
    write_u32_be(out, position, header.sent_time);
    write_u32_be(out, position, header.challenge);

    size_t crc_pos = 0;
    if (header.type == EnetUdpHeaderType::PlainWithCrc) {
        // placeholder
        crc_pos = position;
        write_u32_be(out, position, 0);
    }

    for (const auto& c : commands)
        write_command(out, position, c);

    if (header.type == EnetUdpHeaderType::PlainWithCrc) {
        // Compute CRC with field zeroed
        uint32_t crc = calculate_crc(out.data(), position, 4);

        // Write CRC back big-endian at crc_pos
        uint32_t be = crc;
        if constexpr (std::endian::native == std::endian::little)
            be = std::byteswap(be);
        std::memcpy(out.data() + crc_pos, &be, 4);
    }

    return position;
}
} // namespace enet
} // namespace luxon
