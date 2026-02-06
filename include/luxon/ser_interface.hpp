// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "ser_types.hpp"
#include "ser_encryption.hpp"

#include <memory>

namespace luxon::ser {
class IProtocol {
public:
    virtual ~IProtocol() = default;

    virtual std::expected<ByteArray, Error> Serialize(const Message& message) = 0;
    virtual std::expected<Message, Error> Deserialize(std::span<const uint8_t> packet_bytes) = 0;

    virtual std::expected<ByteArray, Error> CreateInitEncryptionRequest();
    virtual std::expected<void, Error> HandleInitEncryptionResponse(const InternalOperationResponseMessage& response);
    virtual std::expected<InternalOperationResponseMessage, Error> HandleInitEncryptionRequest(const InternalOperationRequestMessage& request);

    bool has_encryption_key() const;

    std::expected<ByteArray, Error> Serialize(Message& message, bool encrypt) {
        message.encrypted = encrypt;
        return Serialize(message);
    }
    std::expected<ByteArray, Error> Serialize(Message message, bool encrypt) {
        message.encrypted = encrypt;
        return Serialize(message);
    }

    static std::unique_ptr<IProtocol> make(unsigned major_version, unsigned minor_version);

protected:
    CryptoContext crypto_{};

    std::expected<ByteArray, Error> maybe_encrypt_payload(Kind kind, bool encrypt_flag, std::span<const uint8_t> payload);
    std::expected<ByteArray, Error> maybe_decrypt_payload(Kind kind, bool encrypted_flag, std::span<const uint8_t> payload);
};
} // namespace luxon::ser
