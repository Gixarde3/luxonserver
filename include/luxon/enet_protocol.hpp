// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <array>
#include <span>
#include <memory>
#include <variant>
#include <stdexcept>

namespace luxon {
namespace enet {
using ByteArray = std::vector<uint8_t>;
using DatagramBuffer = std::array<uint8_t, 1500>;
using DatagramView = std::span<const uint8_t>;

class DatagramBufferPool {
public:
    struct Deleter {
        DatagramBufferPool *pool = nullptr;

        void operator()(DatagramBuffer *ptr) const noexcept {
            if (!ptr)
                return;
            if (pool)
                pool->release(ptr);
            else
                delete ptr;
        }
    };

    using PooledPtr = std::unique_ptr<DatagramBuffer, Deleter>;

    static DatagramBufferPool& instance();

    PooledPtr acquire();

private:
    void release(DatagramBuffer *ptr) noexcept;

    DatagramBufferPool() = default;
    ~DatagramBufferPool() = default;

    DatagramBufferPool(const DatagramBufferPool&) = delete;
    DatagramBufferPool& operator=(const DatagramBufferPool&) = delete;

    std::vector<std::unique_ptr<DatagramBuffer>> free_;
};

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CRCError : public ProtocolError {
public:
    using ProtocolError::ProtocolError;
};

enum class EnetCommandType : uint8_t {
    None = 0,
    Acknowledge = 1,
    Connect = 2,
    VerifyConnect = 3,
    Disconnect = 4,
    Ping = 5,
    SendReliable = 6,
    SendUnreliable = 7,
    SendFragment = 8,
    SendUnreliableUnsequenced = 11,
    EgServerTime = 12,
    EgSendUnreliableProcessed = 13,
    EgSendReliableUnsequenced = 14,
    EgSendFragmentUnsequenced = 15,
    EgAcknowledgeUnsequenced = 16
};

namespace FlagValue {
constexpr uint8_t Unreliable = 0;
constexpr uint8_t Reliable = 1;
constexpr uint8_t UnreliableUnsequenced = 2;
constexpr uint8_t ReliableUnsequenced = 3;
}; // namespace FlagValue

// UDP header "type" byte
enum class EnetUdpHeaderType : uint8_t { PlainNoCrc = 0, PlainWithCrc = 204, Encrypted = 1 };

struct EnetPacketHeader {
    // Can be -1/-2 for connect tracing
    int16_t peer_id = -1;

    EnetUdpHeaderType type = EnetUdpHeaderType::PlainNoCrc;

    uint8_t command_count = 0;

    // For plain packets: int serverSentTime at offset 4
    // For encrypted packets: this field lives inside ciphertext (after command_count)
    uint32_t sent_time = 0;

    // int challenge at offset 8 (or in encrypted header at offset 3)
    uint32_t challenge = 0;

    // Optional CRC field (present iff type==PlainWithCrc). Stored as uint32_t but written like int32_t
    std::optional<uint32_t> crc32;
};

// Common header of each ENet command
struct EnetCommandHeader {
    EnetCommandType command_type = EnetCommandType::None;
    uint8_t channel_id = 0;
    uint8_t flags = 0;
    uint8_t reserved = 4;        // always going to be 4, allow reading/writing other values just in case
    uint32_t command_length = 0; // total size including header
    uint32_t reliable_seq = 0;
};

// Full command including type-specific fields and payload bytes (payload is raw command body beyond headers)
struct EnetCommand {
    EnetCommandHeader header;

    // For Acknowledge / EgAcknowledgeUnsequenced
    uint32_t ack_received_reliable_sequence_number = 0;
    uint32_t ack_received_sent_time = 0;

    // For SendUnreliable
    uint32_t unreliable_seq = 0;

    // For SendUnreliableUnsequenced
    uint32_t unsequenced_group_number = 0;

    // For fragments (SendFragment, EgSendFragmentUnsequenced)
    uint32_t fragment_start_seq = 0;
    uint32_t fragment_count = 0;
    uint32_t fragment_number = 0;
    uint32_t fragment_total_length = 0;
    uint32_t fragment_offset = 0;

    struct HeapBuffer {
        DatagramBufferPool::PooledPtr data;
        size_t size = 0;

        HeapBuffer() = default;

        HeapBuffer(DatagramBufferPool::PooledPtr&& data, size_t size) : data(std::move(data)), size(size) {}

        HeapBuffer(const HeapBuffer& other) : size(other.size) {
            if (other.data) {
                data = DatagramBufferPool::instance().acquire();
                std::copy(other.data->begin(), other.data->begin() + size, data->begin());
            }
        }

        HeapBuffer& operator=(const HeapBuffer& other);

        HeapBuffer(HeapBuffer&& other) noexcept : data(std::move(other.data)), size(other.size) { other.size = 0; }

        HeapBuffer& operator=(HeapBuffer&& other) noexcept;
    };

    std::variant<ByteArray, HeapBuffer> payload_;

    std::span<uint8_t> get_payload();

    std::span<const uint8_t> get_payload() const;

    size_t get_payload_size() const;

    bool is_payload_empty() const;

    void set_payload(DatagramView payload);

    void set_payload(ByteArray payload) { payload_ = std::move(payload); }

    void reset_payload() { payload_ = {}; }
};

// CRC32: init=0xFFFFFFFF, table poly=0xEDB88320, update: crc=(crc>>8) ^ table[byte ^ (crc&0xFF)], NO final xor
uint32_t calculate_crc(const uint8_t *data, size_t length);

// Parse a raw UDP datagram
std::vector<EnetCommand> parse_packet(std::span<const uint8_t> datagram, EnetPacketHeader& out_header);

// Create a raw UDP datagram from header+commands
size_t create_packet(DatagramBuffer& out, EnetPacketHeader header, const std::vector<EnetCommand>& commands);

// Helpers to serialize/deserialize individual commands
EnetCommand parse_command(const uint8_t *data, size_t data_len, size_t& inout_offset);
uint32_t compute_command_length(const EnetCommand& cmd);
void write_command(DatagramBuffer& out, unsigned& position, const EnetCommand& cmd);

// Size helpers
constexpr size_t kCmdHeaderSize = 12;
constexpr size_t kCmdAckSize = 20;
constexpr size_t kCmdFragHeader = 32;
} // namespace enet
} // namespace luxon
