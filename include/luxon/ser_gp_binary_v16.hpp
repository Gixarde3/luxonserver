// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "ser_buffer.hpp"
#include "ser_interface.hpp"

namespace luxon::ser {
class GpBinaryV16 final : public IProtocol {
public:
    GpBinaryV16() = default;
    ~GpBinaryV16() override = default;

    ProtocolImplID GetProtcolImplID() override { return ProtocolImplID::GpBinaryV16; }

    std::expected<ByteArray, Error> Serialize(const Message& message) override;
    std::expected<Message, Error> Deserialize(std::span<const uint8_t> packet_bytes) override;

    std::expected<void, Error> EncodeValue(ByteWriter& w, const Value& v, int depth = 0) const override;
    std::expected<Value, Error> DecodeValue(ByteReader& r, int depth = 0) const override;

    std::expected<void, Error> encode_parameters(ByteWriter& w, const ParameterList& params, int depth) const;
    std::expected<ParameterList, Error> decode_parameters(ByteReader& r, int depth) const;

    std::expected<void, Error> write_string_u16(ByteWriter& w, const std::string& s) const;
    std::expected<std::string, Error> read_string_u16(ByteReader& r) const;

private:
    static constexpr int MAX_DEPTH = 32;

    std::expected<void, Error> encode_value_with_type_flag(ByteWriter& w, const Value& v, int depth, bool write_type) const;
    std::expected<Value, Error> decode_value_payload(ByteReader& r, uint8_t tc, int depth) const;
};
} // namespace luxon::ser
