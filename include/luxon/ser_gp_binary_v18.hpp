#pragma once

#include "ser_buffer.hpp"
#include "ser_encryption.hpp"
#include "ser_interface.hpp"

namespace luxon::ser {
class GpBinaryV18 final : public IProtocol {
public:
    GpBinaryV18() = default;
    ~GpBinaryV18() override = default;

    std::expected<ByteArray, Error> Serialize(const Message& message) override;
    std::expected<Message, Error> Deserialize(std::span<const uint8_t> packet_bytes) override;

private:
    static constexpr int MAX_DEPTH = 32;

    std::expected<void, Error> encode_value(ByteWriter& w, const Value& v, int depth) const;
    std::expected<Value, Error> decode_value(ByteReader& r, int depth) const;

    std::expected<void, Error> encode_parameters(ByteWriter& w, const ParameterList& params, int depth) const;
    std::expected<ParameterList, Error> decode_parameters(ByteReader& r, int depth) const;
};
} // namespace luxon::ser
