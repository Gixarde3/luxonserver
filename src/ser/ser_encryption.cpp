#include "ser_encryption.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <mbedtls/cipher.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/dhm.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

namespace luxon::ser {
static std::string mbedtls_err_msg(int ret, const char *context) {
    char buf[256];
    std::memset(buf, 0, sizeof(buf));
    mbedtls_strerror(ret, buf, sizeof(buf));
    return std::string(context) + ": " + buf + " (ret=" + std::to_string(ret) + ")";
}

// RFC 2409 Oakley Group 1 (768-bit)
static constexpr std::array<uint8_t, 96> Oakley768 = {255, 255, 255, 255, 255, 255, 255, 255, 201, 15,  218, 162, 33,  104, 194, 52,  196, 198, 98,  139,
                                                      128, 220, 28,  209, 41,  2,   78,  8,   138, 103, 204, 116, 2,   11,  190, 166, 59,  19,  155, 34,
                                                      81,  74,  8,   121, 142, 52,  4,   221, 239, 149, 25,  179, 205, 58,  67,  27,  48,  43,  10,  109,
                                                      242, 95,  20,  55,  79,  225, 53,  109, 109, 81,  194, 69,  228, 133, 181, 118, 98,  94,  126, 198,
                                                      244, 76,  66,  233, 166, 58,  54,  32,  255, 255, 255, 255, 255, 255, 255, 255};
static constexpr unsigned OakleyGen = 22;

struct CryptoContext::Impl {
    mbedtls_dhm_context dhm;
    bool dhm_ready = false;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    bool rng_ready = false;
    std::string rng_error;

    Impl() {
        mbedtls_dhm_init(&dhm);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
    }

    ~Impl() {
        mbedtls_dhm_free(&dhm);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
};

CryptoContext::CryptoContext() : impl_(std::make_unique<Impl>()) {}

CryptoContext::~CryptoContext() {
    if (aes_key_) {
        mbedtls_platform_zeroize(aes_key_->data(), aes_key_->size());
        aes_key_.reset();
    }
}

std::expected<void, Error> CryptoContext::ensure_rng_ready() {
    if (!impl_)
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Crypto context uninitialized"});

    if (impl_->rng_ready)
        return {};

    const char *pers = "luxon-ser-dhm";
    int ret = mbedtls_ctr_drbg_seed(&impl_->ctr_drbg, mbedtls_entropy_func, &impl_->entropy, reinterpret_cast<const unsigned char *>(pers), std::strlen(pers));
    if (ret != 0) {
        impl_->rng_ready = false;
        impl_->rng_error = mbedtls_err_msg(ret, "RNG seed failed");
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = impl_->rng_error});
    }

    impl_->rng_ready = true;
    impl_->rng_error.clear();
    return {};
}

std::expected<void, Error> CryptoContext::ensure_dh_keypair() {
    if (!impl_)
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Crypto context uninitialized"});

    if (impl_->dhm_ready)
        return {};

    auto rng = ensure_rng_ready();
    if (!rng)
        return std::unexpected(rng.error());

    // Clean reset to ensure fresh state on retry
    mbedtls_dhm_free(&impl_->dhm);
    mbedtls_dhm_init(&impl_->dhm);

    mbedtls_mpi P;
    mbedtls_mpi G;
    mbedtls_mpi_init(&P);
    mbedtls_mpi_init(&G);

    // Cleanup for MPIs
    auto cleanup_mpi = [&] {
        mbedtls_mpi_free(&G);
        mbedtls_mpi_free(&P);
    };

    int ret = mbedtls_mpi_read_binary(&P, Oakley768.data(), Oakley768.size());
    if (ret != 0) {
        cleanup_mpi();
        return std::unexpected(Error{.code = Error::Code::DhError, .message = mbedtls_err_msg(ret, "Failed to load DH param P")});
    }

    ret = mbedtls_mpi_lset(&G, static_cast<mbedtls_mpi_sint>(OakleyGen));
    if (ret != 0) {
        cleanup_mpi();
        return std::unexpected(Error{.code = Error::Code::DhError, .message = mbedtls_err_msg(ret, "Failed to load DH param G")});
    }

    ret = mbedtls_dhm_set_group(&impl_->dhm, &P, &G);
    if (ret != 0) {
        cleanup_mpi();
        return std::unexpected(Error{.code = Error::Code::DhError, .message = mbedtls_err_msg(ret, "Failed to set DH group")});
    }

    // Cleanup MPIs now that they are copied into dhm context
    cleanup_mpi();

    // Generate our keypair (X, GX)
    const size_t p_len = mbedtls_dhm_get_len(&impl_->dhm);
    std::vector<unsigned char> tmp_pub(p_len);

    ret = mbedtls_dhm_make_public(&impl_->dhm, static_cast<int>(p_len), tmp_pub.data(), tmp_pub.size(), mbedtls_ctr_drbg_random, &impl_->ctr_drbg);
    if (ret != 0) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = mbedtls_err_msg(ret, "Failed to generate DH public key")});
    }

    impl_->dhm_ready = true;
    return {};
}

std::expected<ByteArray, Error> CryptoContext::GetOrCreateDhPublicKey() {
    auto dh = ensure_dh_keypair();
    if (!dh)
        return std::unexpected(dh.error());

    mbedtls_mpi GX;
    mbedtls_mpi_init(&GX);

    int ret = mbedtls_dhm_get_value(&impl_->dhm, MBEDTLS_DHM_PARAM_GX, &GX);
    if (ret != 0) {
        mbedtls_mpi_free(&GX);
        return std::unexpected(Error{.code = Error::Code::DhError, .message = mbedtls_err_msg(ret, "Failed to retrieve DH public value")});
    }

    const size_t nbytes = mbedtls_mpi_size(&GX);
    if (nbytes == 0) {
        mbedtls_mpi_free(&GX);
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "DH public key size is invalid (0)"});
    }

    ByteArray out(nbytes);
    ret = mbedtls_mpi_write_binary(&GX, out.data(), out.size());
    mbedtls_mpi_free(&GX);

    if (ret != 0) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = mbedtls_err_msg(ret, "Failed to serialize DH public key")});
    }

    return out;
}

std::expected<void, Error> CryptoContext::DeriveFromPeerPublicKey(std::span<const uint8_t> peer_public_key) {
    if (!impl_)
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Crypto context uninitialized"});

    // Check RNG state explicitly
    if (!impl_->rng_ready) {
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "RNG not ready"});
    }

    if (peer_public_key.empty()) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "Peer public key is empty"});
    }

    // Ensure our side is initialized
    auto dh = ensure_dh_keypair();
    if (!dh)
        return std::unexpected(dh.error());

    int ret = mbedtls_dhm_read_public(&impl_->dhm, peer_public_key.data(), peer_public_key.size());
    if (ret != 0) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = mbedtls_err_msg(ret, "Failed to read peer public key")});
    }

    const size_t p_len = mbedtls_dhm_get_len(&impl_->dhm);
    if (p_len == 0) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "DH modulus length is 0"});
    }

    std::vector<unsigned char> secret_fixed(p_len);
    size_t olen = 0;

    ret = mbedtls_dhm_calc_secret(&impl_->dhm, secret_fixed.data(), secret_fixed.size(), &olen, mbedtls_ctr_drbg_random, &impl_->ctr_drbg);
    if (ret != 0) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = mbedtls_err_msg(ret, "Failed to calculate shared secret")});
    }

    if (olen == 0 || olen > secret_fixed.size()) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "Calculated secret length is invalid"});
    }

    // Trim leading zeros from Big Integer representation
    size_t first_nonzero = 0;
    while (first_nonzero < olen && secret_fixed[first_nonzero] == 0x00) {
        first_nonzero++;
    }

    const unsigned char *secret_ptr = secret_fixed.data() + first_nonzero;
    size_t secret_len = olen - first_nonzero;

    // Handle the mathematical edge case where secret is exactly 0
    unsigned char zero = 0x00;
    if (secret_len == 0) {
        secret_ptr = &zero;
        secret_len = 1;
    }

    std::array<uint8_t, 32> hash{};
    ret = mbedtls_sha256(secret_ptr, secret_len, hash.data(), 0); // 0 = SHA-256 (not 224)

    // Zeroize the raw shared secret immediately
    mbedtls_platform_zeroize(secret_fixed.data(), secret_fixed.size());

    if (ret != 0) {
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = mbedtls_err_msg(ret, "KDF (SHA256) failed")});
    }

    aes_key_ = hash;
    return {};
}

static std::expected<ByteArray, Error> aes_256_cbc_crypt(bool encrypt, std::span<const uint8_t> in, const std::array<uint8_t, 32>& key) {
    const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_CBC);
    if (!info) {
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = "MBEDTLS_CIPHER_AES_256_CBC algorithm unavailable"});
    }

    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);

    // RAII cleanup for cipher context
    auto cleanup_cipher = [&] { mbedtls_cipher_free(&ctx); };

    int ret = mbedtls_cipher_setup(&ctx, info);
    if (ret != 0) {
        cleanup_cipher();
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = mbedtls_err_msg(ret, "Cipher setup failed")});
    }

    // PKCS7 padding is standard
    ret = mbedtls_cipher_set_padding_mode(&ctx, MBEDTLS_PADDING_PKCS7);
    if (ret != 0) {
        cleanup_cipher();
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = mbedtls_err_msg(ret, "Failed to set padding mode")});
    }

    ret = mbedtls_cipher_setkey(&ctx, key.data(), 256, encrypt ? MBEDTLS_ENCRYPT : MBEDTLS_DECRYPT);
    if (ret != 0) {
        cleanup_cipher();
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = mbedtls_err_msg(ret, "Failed to set AES key")});
    }

    // This is insecure, but that's just how it's done
    unsigned char iv[16];
    std::memset(iv, 0, sizeof(iv));

    ret = mbedtls_cipher_set_iv(&ctx, iv, sizeof(iv));
    if (ret != 0) {
        cleanup_cipher();
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = mbedtls_err_msg(ret, "Failed to set IV")});
    }

    ret = mbedtls_cipher_reset(&ctx);
    if (ret != 0) {
        cleanup_cipher();
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = mbedtls_err_msg(ret, "Cipher reset failed")});
    }

    const size_t block_size = mbedtls_cipher_get_block_size(&ctx);
    // Allocate enough space for input + one block (padding)
    const size_t max_out = in.size() + block_size;

    ByteArray out;
    try {
        out.resize(max_out);
    } catch (...) {
        cleanup_cipher();
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = "Failed to allocate buffer for encryption/decryption"});
    }

    size_t outl1 = 0;
    ret = mbedtls_cipher_update(&ctx, in.data(), in.size(), out.data(), &outl1);
    if (ret != 0) {
        cleanup_cipher();
        return std::unexpected(
            Error{.code = Error::Code::CryptoError, .message = mbedtls_err_msg(ret, encrypt ? "Encrypt update failed" : "Decrypt update failed")});
    }

    size_t outl2 = 0;
    ret = mbedtls_cipher_finish(&ctx, out.data() + outl1, &outl2);
    if (ret != 0) {
        cleanup_cipher();
        // Distinguish between a library failure and likely bad data (padding error)
        std::string err_ctx = encrypt ? "Encrypt finish failed" : "Decrypt finish failed (possible bad key or corrupt data)";
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = mbedtls_err_msg(ret, err_ctx.c_str())});
    }

    out.resize(outl1 + outl2);
    cleanup_cipher();
    return out;
}

std::expected<ByteArray, Error> CryptoContext::EncryptPayload(std::span<const uint8_t> plaintext) const {
    if (!aes_key_)
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Encryption disabled: Shared key not derived"});

    return aes_256_cbc_crypt(true, plaintext, *aes_key_);
}

std::expected<ByteArray, Error> CryptoContext::DecryptPayload(std::span<const uint8_t> ciphertext) const {
    if (!aes_key_)
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Decryption disabled: Shared key not derived"});

    return aes_256_cbc_crypt(false, ciphertext, *aes_key_);
}
} // namespace luxon::ser
