// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "ser_interface.hpp"

namespace luxon::ser {
class IPCBinaryProtocol : public IProtocol {
public:
    ProtocolImplID GetProtcolImplID() override;

    std::expected<ByteArray, Error> Serialize(const Message& message) override;
    std::expected<Message, Error> Deserialize(std::span<const uint8_t> packet_bytes) override;

    std::expected<void, Error> EncodeValue(ByteWriter& w, const Value& v, int depth = 0) const override;
    std::expected<Value, Error> DecodeValue(ByteReader& r, int depth = 0) const override;

    // Disabled for IPC
    std::expected<ByteArray, Error> CreateInitEncryptionRequest() override;
    std::expected<void, Error> HandleInitEncryptionResponse(const InternalOperationResponseMessage& response) override;
    std::expected<InternalOperationResponseMessage, Error> HandleInitEncryptionRequest(const InternalOperationRequestMessage& request) override;

private:
    static void encode_string(ByteWriter& w, const std::string& str);
    static std::expected<std::string, Error> decode_string(ByteReader& r);

    static void encode_bytearray(ByteWriter& w, const ByteArray& arr);
    static std::expected<ByteArray, Error> decode_bytearray(ByteReader& r);

    std::expected<void, Error> encode_dict(ByteWriter& w, const Dictionary& d, int depth) const;
    std::expected<Dictionary, Error> decode_dict(ByteReader& r, int depth) const;

    std::expected<void, Error> encode_event(ByteWriter& w, const EventMessage& ev, int depth) const;
    std::expected<EventMessage, Error> decode_event(ByteReader& r, int depth) const;

    // Template helpers stay inline in the header
    template <typename T> std::expected<void, Error> encode_pod_vector(ByteWriter& w, const std::vector<T>& vec) const {
        w.write_u32_le(static_cast<uint32_t>(vec.size()));
        w.write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(vec.data()), vec.size() * sizeof(T)));
        return {};
    }

    template <typename T> std::expected<Value, Error> decode_pod_vector(ByteReader& r) const {
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        auto bytes = r.read_span(*len * sizeof(T));
        if (!bytes)
            return std::unexpected(bytes.error());
        std::vector<T> vec(*len);
        std::memcpy(vec.data(), bytes->data(), bytes->size());
        return Value(std::move(vec));
    }

    template <typename T> std::expected<void, Error> encode_op_req(ByteWriter& w, const T& req, int depth) const {
        w.write_u8(req.operation_code);
        return encode_dict(w, req.parameters, depth);
    }

    template <typename T> std::expected<T, Error> decode_op_req(ByteReader& r, int depth) const {
        T req;
        auto code = r.read_u8();
        if (!code)
            return std::unexpected(code.error());
        req.operation_code = *code;
        auto dict = decode_dict(r, depth);
        if (!dict)
            return std::unexpected(dict.error());
        req.parameters = std::move(*dict);
        return req;
    }

    template <typename T> std::expected<void, Error> encode_op_resp(ByteWriter& w, const T& resp, int depth) const {
        w.write_u8(resp.operation_code);
        w.write_i16_le(resp.return_code);
        w.write_u8(resp.debug_message.has_value() ? 1 : 0);
        if (resp.debug_message)
            encode_string(w, *resp.debug_message);
        return encode_dict(w, resp.parameters, depth);
    }

    template <typename T> std::expected<T, Error> decode_op_resp(ByteReader& r, int depth) const {
        T resp;
        auto code = r.read_u8();
        if (!code)
            return std::unexpected(code.error());
        resp.operation_code = *code;

        auto rc = r.read_i16_le();
        if (!rc)
            return std::unexpected(rc.error());
        resp.return_code = *rc;

        auto has_msg = r.read_u8();
        if (!has_msg)
            return std::unexpected(has_msg.error());

        if (*has_msg) {
            auto str = decode_string(r);
            if (!str)
                return std::unexpected(str.error());
            resp.debug_message = std::move(*str);
        }

        auto dict = decode_dict(r, depth);
        if (!dict)
            return std::unexpected(dict.error());
        resp.parameters = std::move(*dict);

        return resp;
    }
};
} // namespace luxon::ser
