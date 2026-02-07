#include "ser_encryption.hpp"

#include <array>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#ifdef __3DS__
#include <3ds.h>
#endif

extern "C" {
#include <tomcrypt.h>
#include <tommath.h>

// If you build LibTomCrypt with LibTomMath, you typically need this:
extern const ltc_math_descriptor ltm_desc;
extern ltc_math_descriptor ltc_mp;
}

namespace luxon::ser {
static std::string ltc_err_msg(int err, const char* context) {
    const char* s = error_to_string(err);
    std::string msg = (s && *s) ? s : "unknown LibTomCrypt error";
    return std::string(context) + ": " + msg + " (err=" + std::to_string(err) + ")";
}

static void secure_zero(void* p, size_t n) {
    if (p && n) zeromem(p, n);
}

// RFC 2409 Oakley Group 1 (768-bit)
static constexpr std::array<uint8_t, 96> Oakley768 = {
    255, 255, 255, 255, 255, 255, 255, 255, 201, 15,  218, 162, 33,  104, 194, 52,
    196, 198, 98,  139, 128, 220, 28,  209, 41,  2,   78,  8,   138, 103, 204, 116,
    2,   11,  190, 166, 59,  19,  155, 34,  81,  74,  8,   121, 142, 52,  4,   221,
    239, 149, 25,  179, 205, 58,  67,  27,  48,  43,  10,  109, 242, 95,  20,  55,
    79,  225, 53,  109, 109, 81,  194, 69,  228, 133, 181, 118, 98,  94,  126, 198,
    244, 76,  66,  233, 166, 58,  54,  32,  255, 255, 255, 255, 255, 255, 255, 255
};
static constexpr unsigned OakleyGen = 22;

static void ensure_ltc_registered() {
    [[maybe_unused]] static const bool once = []() {
        // Make sure LibTomCrypt uses LibTomMath (if that’s your build)
        ltc_mp = ltm_desc;

        // Register only what we need
        register_cipher(&aes_desc);
        register_hash(&sha256_desc);

#if defined(LTC_FORTUNA)
        register_prng(&fortuna_desc);
#elif defined(LTC_YARROW)
        register_prng(&yarrow_desc);
#elif defined(LTC_SPRNG)
        register_prng(&sprng_desc);
#endif
        return true;
    }();
}

static bool pkcs7_pad(const uint8_t* in, size_t in_len, size_t block, std::vector<uint8_t>& out) {
    if (block == 0 || block > 255) return false;
    const size_t pad = block - (in_len % block);
    out.resize(in_len + pad);
    if (in_len) std::memcpy(out.data(), in, in_len);
    std::memset(out.data() + in_len, static_cast<int>(pad), pad);
    return true;
}

static bool pkcs7_unpad(std::vector<uint8_t>& buf, size_t block) {
    if (block == 0 || block > 255) return false;
    if (buf.empty() || (buf.size() % block) != 0) return false;

    const uint8_t pad = buf.back();
    if (pad == 0 || pad > block || pad > buf.size()) return false;

    for (size_t i = 0; i < pad; ++i) {
        if (buf[buf.size() - 1 - i] != pad) return false;
    }
    buf.resize(buf.size() - pad);
    return true;
}

static std::expected<ByteArray, Error>
aes_256_cbc_crypt(bool encrypt, std::span<const uint8_t> in, const std::array<uint8_t, 32>& key) {
    const int cipher_idx = find_cipher("aes");
    if (cipher_idx < 0) {
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = "AES cipher not registered in LibTomCrypt"});
    }

    unsigned char iv[16];
    std::memset(iv, 0, sizeof(iv)); // compatibility: zero IV

    symmetric_CBC cbc{};
    int ret = cbc_start(cipher_idx, iv, key.data(), 32, 0, &cbc);
    if (ret != CRYPT_OK) {
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = ltc_err_msg(ret, "cbc_start failed")});
    }

    auto done = [&] { (void)cbc_done(&cbc); };

    if (encrypt) {
        std::vector<uint8_t> padded;
        if (!pkcs7_pad(in.data(), in.size(), 16, padded)) {
            done();
            return std::unexpected(Error{.code = Error::Code::CryptoError, .message = "PKCS7 pad failed"});
        }

        ByteArray out(padded.size());
        ret = cbc_encrypt(padded.data(), out.data(), static_cast<unsigned long>(padded.size()), &cbc);
        secure_zero(padded.data(), padded.size());
        done();

        if (ret != CRYPT_OK) {
            secure_zero(out.data(), out.size());
            return std::unexpected(Error{.code = Error::Code::CryptoError, .message = ltc_err_msg(ret, "cbc_encrypt failed")});
        }
        return out;
    } else {
        if ((in.size() % 16) != 0) {
            done();
            return std::unexpected(Error{.code = Error::Code::CryptoError, .message = "ciphertext length is not a multiple of AES block size"});
        }

        std::vector<uint8_t> tmp(in.size());
        if (!in.empty()) {
            ret = cbc_decrypt(in.data(), tmp.data(), static_cast<unsigned long>(in.size()), &cbc);
        }
        done();

        if (ret != CRYPT_OK) {
            secure_zero(tmp.data(), tmp.size());
            return std::unexpected(Error{.code = Error::Code::CryptoError, .message = ltc_err_msg(ret, "cbc_decrypt failed")});
        }

        if (!pkcs7_unpad(tmp, 16)) {
            secure_zero(tmp.data(), tmp.size());
            return std::unexpected(Error{.code = Error::Code::CryptoError, .message = "Decrypt failed (possible bad key or corrupt data): bad padding"});
        }

        ByteArray out(tmp.begin(), tmp.end());
        secure_zero(tmp.data(), tmp.size());
        return out;
    }
}

struct CryptoContext::Impl {
    dh_key dh{};
    bool dh_ready = false;

    prng_state prng{};
    int prng_idx = -1;
    bool rng_ready = false;
    std::string rng_error;

    Impl() { ensure_ltc_registered(); }

    ~Impl() {
        if (dh_ready) {
            dh_free(&dh);
            dh_ready = false;
        }
        if (rng_ready && prng_idx >= 0) {
            (void)prng_descriptor[prng_idx].done(&prng);
        }
        rng_ready = false;
    }
};

CryptoContext::CryptoContext() : impl_(std::make_unique<Impl>()) {}

CryptoContext::~CryptoContext() {
    if (aes_key_) {
        secure_zero(aes_key_->data(), aes_key_->size());
        aes_key_.reset();
    }
}

std::expected<void, Error> CryptoContext::ensure_rng_ready() {
    if (!impl_) {
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Crypto context uninitialized"});
    }
    if (impl_->rng_ready) return {};

    // Prefer Fortuna if available, else Yarrow, else sprng
    impl_->prng_idx = find_prng("fortuna");
    if (impl_->prng_idx < 0) impl_->prng_idx = find_prng("yarrow");
    if (impl_->prng_idx < 0) impl_->prng_idx = find_prng("sprng");

    if (impl_->prng_idx < 0) {
        impl_->rng_error = "no supported PRNG registered (need fortuna/yarrow/sprng enabled in LibTomCrypt)";
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = impl_->rng_error});
    }

    int ret = prng_descriptor[impl_->prng_idx].start(&impl_->prng);
    if (ret != CRYPT_OK) {
        impl_->rng_error = ltc_err_msg(ret, "prng.start failed");
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = impl_->rng_error});
    }

    unsigned char seed[64];

#ifdef __3DS__
    Result rc = psInit();
    if (R_FAILED(rc)) {
        (void)prng_descriptor[impl_->prng_idx].done(&impl_->prng);
        impl_->rng_error = "psInit failed";
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = impl_->rng_error});
    }

    rc = PS_GenerateRandomBytes(seed, sizeof(seed));
    psExit();

    if (R_FAILED(rc)) {
        secure_zero(seed, sizeof(seed));
        (void)prng_descriptor[impl_->prng_idx].done(&impl_->prng);
        impl_->rng_error = "PS_GenerateRandomBytes failed";
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = impl_->rng_error});
    }
#else
    const unsigned long got = rng_get_bytes(seed, sizeof(seed), nullptr);
    if (got != sizeof(seed)) {
        secure_zero(seed, sizeof(seed));
        (void)prng_descriptor[impl_->prng_idx].done(&impl_->prng);
        impl_->rng_error = "rng_get_bytes failed to provide sufficient entropy";
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = impl_->rng_error});
    }
#endif

    ret = prng_descriptor[impl_->prng_idx].add_entropy(seed, sizeof(seed), &impl_->prng);
    secure_zero(seed, sizeof(seed));
    if (ret != CRYPT_OK) {
        (void)prng_descriptor[impl_->prng_idx].done(&impl_->prng);
        impl_->rng_error = ltc_err_msg(ret, "prng.add_entropy failed");
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = impl_->rng_error});
    }

    ret = prng_descriptor[impl_->prng_idx].ready(&impl_->prng);
    if (ret != CRYPT_OK) {
        (void)prng_descriptor[impl_->prng_idx].done(&impl_->prng);
        impl_->rng_error = ltc_err_msg(ret, "prng.ready failed");
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = impl_->rng_error});
    }

    impl_->rng_ready = true;
    impl_->rng_error.clear();
    return {};
}

std::expected<void, Error> CryptoContext::ensure_dh_keypair() {
    if (!impl_) {
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Crypto context uninitialized"});
    }
    if (impl_->dh_ready) return {};

    auto rng = ensure_rng_ready();
    if (!rng) return std::unexpected(rng.error());

    // Clean reset to ensure fresh state on retry
    if (impl_->dh_ready) {
        dh_free(&impl_->dh);
        impl_->dh_ready = false;
    }
    std::memset(&impl_->dh, 0, sizeof(impl_->dh));

    const uint8_t g_bytes[1] = { static_cast<uint8_t>(OakleyGen) };

    int ret = dh_set_pg(Oakley768.data(),
                        static_cast<unsigned long>(Oakley768.size()),
                        g_bytes,
                        static_cast<unsigned long>(sizeof(g_bytes)),
                        &impl_->dh);
    if (ret != CRYPT_OK) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = ltc_err_msg(ret, "dh_set_pg failed")});
    }

    ret = dh_generate_key(&impl_->prng, impl_->prng_idx, &impl_->dh);
    if (ret != CRYPT_OK) {
        dh_free(&impl_->dh);
        impl_->dh_ready = false;
        return std::unexpected(Error{.code = Error::Code::DhError, .message = ltc_err_msg(ret, "dh_generate_key failed")});
    }

    impl_->dh_ready = true;
    return {};
}

std::expected<ByteArray, Error> CryptoContext::GetOrCreateDhPublicKey() {
    auto dh = ensure_dh_keypair();
    if (!dh) return std::unexpected(dh.error());

    // LibTomCrypt dh_key stores mp_int* as void* when using LibTomMath
    mp_int* y = reinterpret_cast<mp_int*>(impl_->dh.y);
    if (!y) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "DH public value missing"});
    }

    const int nbytes = mp_unsigned_bin_size(y);
    if (nbytes <= 0) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "DH public key size is invalid"});
    }

    ByteArray out(static_cast<size_t>(nbytes));
    if (mp_to_unsigned_bin(y, out.data()) != MP_OKAY) {
        secure_zero(out.data(), out.size());
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "Failed to serialize DH public key"});
    }

    return out;
}

std::expected<void, Error> CryptoContext::DeriveFromPeerPublicKey(std::span<const uint8_t> peer_public_key) {
    if (!impl_) {
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Crypto context uninitialized"});
    }
    if (!impl_->rng_ready) {
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "RNG not ready"});
    }
    if (peer_public_key.empty()) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "Peer public key is empty"});
    }

    auto dh = ensure_dh_keypair();
    if (!dh) return std::unexpected(dh.error());

    mp_int* p = reinterpret_cast<mp_int*>(impl_->dh.prime);
    mp_int* x = reinterpret_cast<mp_int*>(impl_->dh.x);
    if (!p || !x) {
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "DH internal state incomplete"});
    }

    mp_int peerY, shared, p_minus_1;
    mp_init(&peerY);
    mp_init(&shared);
    mp_init(&p_minus_1);

    auto cleanup = [&] {
        mp_clear(&p_minus_1);
        mp_clear(&shared);
        mp_clear(&peerY);
    };

    // Import peer public key
    if (mp_read_unsigned_bin(&peerY,
                            const_cast<unsigned char*>(peer_public_key.data()),
                            static_cast<int>(peer_public_key.size())) != MP_OKAY) {
        cleanup();
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "mp_read_unsigned_bin(peerY) failed"});
    }

    // Validate 2 <= peerY <= p-2 (equivalent to "peerY > 1 && peerY < p-1")
    if (mp_copy(p, &p_minus_1) != MP_OKAY || mp_sub_d(&p_minus_1, 1uL, &p_minus_1) != MP_OKAY) {
        cleanup();
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "Failed to compute p-1"});
    }

    if (mp_cmp_d(&peerY, 1uL) != MP_GT || mp_cmp(&peerY, &p_minus_1) != MP_LT) {
        cleanup();
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "Peer public key out of range"});
    }

    // shared = peerY^x mod p
    if (mp_exptmod(&peerY, x, p, &shared) != MP_OKAY) {
        cleanup();
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "mp_exptmod failed"});
    }

    // Export shared secret as fixed-size p_len bytes, then trim leading zeros
    const size_t p_len = Oakley768.size();
    std::vector<uint8_t> secret_fixed(p_len, 0x00);

    const int shared_len = mp_unsigned_bin_size(&shared);
    if (shared_len < 0) {
        secure_zero(secret_fixed.data(), secret_fixed.size());
        cleanup();
        return std::unexpected(Error{.code = Error::Code::DhError, .message = "mp_unsigned_bin_size(shared) failed"});
    }

    if (shared_len > 0) {
        std::vector<uint8_t> tmp_min(static_cast<size_t>(shared_len));
        if (mp_to_unsigned_bin(&shared, tmp_min.data()) != MP_OKAY) {
            secure_zero(tmp_min.data(), tmp_min.size());
            secure_zero(secret_fixed.data(), secret_fixed.size());
            cleanup();
            return std::unexpected(Error{.code = Error::Code::DhError, .message = "mp_to_unsigned_bin(shared) failed"});
        }

        if (tmp_min.size() > secret_fixed.size()) {
            secure_zero(tmp_min.data(), tmp_min.size());
            secure_zero(secret_fixed.data(), secret_fixed.size());
            cleanup();
            return std::unexpected(Error{.code = Error::Code::DhError, .message = "Shared secret longer than modulus"});
        }

        const size_t offset = secret_fixed.size() - tmp_min.size();
        std::memcpy(secret_fixed.data() + offset, tmp_min.data(), tmp_min.size());
        secure_zero(tmp_min.data(), tmp_min.size());
    }

    // Trim leading zeros
    size_t first_nonzero = 0;
    while (first_nonzero < secret_fixed.size() && secret_fixed[first_nonzero] == 0x00) {
        first_nonzero++;
    }

    const uint8_t* secret_ptr = secret_fixed.data() + first_nonzero;
    size_t secret_len = secret_fixed.size() - first_nonzero;

    uint8_t zero = 0x00;
    if (secret_len == 0) {
        secret_ptr = &zero;
        secret_len = 1;
    }

    // KDF: SHA-256(secret_trimmed)
    const int hash_idx = find_hash("sha256");
    if (hash_idx < 0) {
        secure_zero(secret_fixed.data(), secret_fixed.size());
        cleanup();
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = "SHA-256 not registered in LibTomCrypt"});
    }

    std::array<uint8_t, 32> hash{};
    unsigned long outlen = static_cast<unsigned long>(hash.size());
    const int hret = hash_memory(hash_idx,
                                secret_ptr,
                                static_cast<unsigned long>(secret_len),
                                hash.data(),
                                &outlen);

    secure_zero(secret_fixed.data(), secret_fixed.size());
    cleanup();

    if (hret != CRYPT_OK || outlen != hash.size()) {
        secure_zero(hash.data(), hash.size());
        return std::unexpected(Error{.code = Error::Code::CryptoError, .message = ltc_err_msg(hret, "KDF (SHA256) failed")});
    }

    aes_key_ = hash;
    return {};
}

std::expected<ByteArray, Error> CryptoContext::EncryptPayload(std::span<const uint8_t> plaintext) const {
    if (!aes_key_) {
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Encryption disabled: Shared key not derived"});
    }
    return aes_256_cbc_crypt(true, plaintext, *aes_key_);
}

std::expected<ByteArray, Error> CryptoContext::DecryptPayload(std::span<const uint8_t> ciphertext) const {
    if (!aes_key_) {
        return std::unexpected(Error{.code = Error::Code::CryptoNotReady, .message = "Decryption disabled: Shared key not derived"});
    }
    return aes_256_cbc_crypt(false, ciphertext, *aes_key_);
}
} // namespace luxon::ser
