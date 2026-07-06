// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "ser_types.hpp"

#include <array>
#include <span>
#include <expected>
#include <optional>

namespace luxon::ser {
class CryptoContext {
public:
    CryptoContext();
    ~CryptoContext();

    CryptoContext(const CryptoContext&) = delete;
    CryptoContext& operator=(const CryptoContext&) = delete;

    CryptoContext(CryptoContext&&) = delete;
    CryptoContext& operator=(CryptoContext&&) = delete;

    bool has_key() const { return aes_key_.has_value(); }

    std::expected<ByteArray, Error> EncryptPayload(std::span<const uint8_t> plaintext) const;
    std::expected<ByteArray, Error> DecryptPayload(std::span<const uint8_t> ciphertext) const;

    // DH
    std::expected<ByteArray, Error> GetOrCreateDhPublicKey();
    std::expected<void, Error> DeriveFromPeerPublicKey(std::span<const uint8_t> peer_public_key);

private:
    std::expected<void, Error> ensure_rng_ready();
    std::expected<void, Error> ensure_dh_keypair();

    std::expected<std::array<uint8_t, 32>, Error> derive_aes_key_from_shared_secret(std::span<const uint8_t> shared_secret_96) const;

    std::optional<std::array<uint8_t, 32>> aes_key_{};

    // mbedtls state (opaque pointers to avoid heavy headers in users)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace luxon::ser
