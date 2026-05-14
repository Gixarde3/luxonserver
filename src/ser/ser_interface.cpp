// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "ser_interface.hpp"
#include "ser_gp_binary_v16.hpp"
#include "ser_gp_binary_v18.hpp"
#include "ser_ipc_binary.hpp"

namespace luxon::ser {
std::expected<ByteArray, Error> IProtocol::CreateInitEncryptionRequest() {
    auto pub = crypto_.GetOrCreateDhPublicKey();
    if (!pub)
        return std::unexpected(pub.error());

    InternalOperationRequestMessage req{};
    req.operation_code = 0; // InitEncryption
    req.parameters[1] = ByteArray(pub->begin(), pub->end());

    return Serialize(Message(req));
}

std::expected<void, Error> IProtocol::HandleInitEncryptionResponse(const InternalOperationResponseMessage& response) {
    if (response.operation_code != 0)
        return std::unexpected(Error{.code = Error::Code::HandshakeError, .message = "wrong opcode in encryption response"});
    if (response.return_code != 0)
        return std::unexpected(Error{.code = Error::Code::HandshakeError, .message = "encryption response return_code != 0"});

    const ByteArray *server_pub = nullptr;
    for (const auto& [key, value] : response.parameters) {
        if (key != 1)
            continue;
        server_pub = value.get_ptr<ByteArray>();
        if (server_pub)
            break;
    }

    if (!server_pub)
        return std::unexpected(Error{.code = Error::Code::HandshakeError, .message = "missing server public key param 1"});

    auto d = crypto_.DeriveFromPeerPublicKey(*server_pub);
    if (!d)
        return std::unexpected(d.error());
    return {};
}

std::expected<InternalOperationResponseMessage, Error> IProtocol::HandleInitEncryptionRequest(const InternalOperationRequestMessage& request) {
    InternalOperationResponseMessage resp{};
    resp.operation_code = request.operation_code;

    if (request.operation_code != 0) {
        resp.return_code = 1;
        resp.debug_message = "unsupported internal opcode";
        return resp;
    }

    const ByteArray *client_pub = nullptr;
    for (const auto& [key, value] : request.parameters) {
        if (key != 1)
            continue;
        client_pub = value.get_ptr<ByteArray>();
        if (client_pub)
            break;
    }

    if (!client_pub) {
        resp.return_code = 2;
        resp.debug_message = "missing client public key param 1";
        return resp;
    }

    auto pub = crypto_.GetOrCreateDhPublicKey();
    if (!pub) {
        resp.return_code = 4;
        resp.debug_message = pub.error().message;
        return resp;
    }

    auto derive = crypto_.DeriveFromPeerPublicKey(*client_pub);
    if (!derive) {
        resp.return_code = 3;
        resp.debug_message = derive.error().message;
        return resp;
    }

    resp.return_code = 0;
    resp.parameters[1] = ByteArray(pub->begin(), pub->end());
    return resp;
}

std::expected<ByteArray, Error> IProtocol::maybe_encrypt_payload(Kind kind, bool encrypt_flag, std::span<const uint8_t> payload) {
    if (!encrypt_flag)
        return ByteArray(payload.begin(), payload.end());

    if (kind == Kind::Init || kind == Kind::InitResponse)
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "init/initresponse cannot be encrypted"});

    return crypto_.EncryptPayload(payload);
}

std::expected<ByteArray, Error> IProtocol::maybe_decrypt_payload(Kind kind, bool encrypted_flag, std::span<const uint8_t> payload) {
    if (!encrypted_flag)
        return ByteArray(payload.begin(), payload.end());

    if (kind == Kind::Init || kind == Kind::InitResponse)
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "init/initresponse marked encrypted"});

    return crypto_.DecryptPayload(payload);
}

bool IProtocol::has_encryption_key() const { return crypto_.has_key(); }

std::unique_ptr<IProtocol> IProtocol::make(unsigned major_version, unsigned minor_version) {
    switch (major_version) {
    case 1:
        switch (minor_version) {
        case 6:
            return std::make_unique<GpBinaryV16>();
        case 8:
            return std::make_unique<GpBinaryV18>();
        }
    }

    return nullptr;
}
} // namespace luxon::ser
