// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "ser_types.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <limits>

namespace luxon::ser {
class ByteWriter {
public:
    void write_u8(uint8_t v) { bytes_.push_back(v); }

    void write_bytes(std::span<const uint8_t> s) { bytes_.insert(bytes_.end(), s.begin(), s.end()); }

    // Little-endian POD writers
    void write_i16_le(int16_t v) { write_pod_le(v); }
    void write_u16_le(uint16_t v) { write_pod_le(v); }
    void write_i32_le(int32_t v) { write_pod_le(v); }
    void write_u32_le(uint32_t v) { write_pod_le(v); }
    void write_i64_le(int64_t v) { write_pod_le(v); }
    void write_u64_le(uint64_t v) { write_pod_le(v); }
    void write_f32_le(float v) { write_pod_le(v); }
    void write_f64_le(double v) { write_pod_le(v); }

    // Big-endian POD writers (needed by GpBinaryV16)
    void write_i16_be(int16_t v) { write_pod_be(v); }
    void write_u16_be(uint16_t v) { write_pod_be(v); }
    void write_i32_be(int32_t v) { write_pod_be(v); }
    void write_u32_be(uint32_t v) { write_pod_be(v); }
    void write_i64_be(int64_t v) { write_pod_be(v); }
    void write_u64_be(uint64_t v) { write_pod_be(v); }
    void write_f32_be(float v) { write_pod_be(v); }
    void write_f64_be(double v) { write_pod_be(v); }

    void write_varuint32(uint32_t v) { write_varuint64(v); }

    void write_varuint64(uint64_t v) {
        while (v >= 0x80) {
            write_u8(static_cast<uint8_t>(v & 0x7F) | 0x80);
            v >>= 7;
        }
        write_u8(static_cast<uint8_t>(v));
    }

    // backwards-compatible alias
    void write_varuint(uint64_t v) { write_varuint64(v); }

    void write_string(const std::string& s) {
        write_varuint64(static_cast<uint64_t>(s.size()));
        write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
    }

    const ByteArray& bytes() const { return bytes_; }
    ByteArray take() { return std::move(bytes_); }

private:
    template <typename T> void write_pod_le(T v) {
        static_assert(std::is_trivially_copyable_v<T>);
        auto u = std::bit_cast<std::array<uint8_t, sizeof(T)>>(v);
        if constexpr (std::endian::native == std::endian::little) {
            write_bytes(u);
        } else {
            for (std::size_t i = 0; i < u.size(); ++i)
                bytes_.push_back(u[u.size() - 1 - i]);
        }
    }

    template <typename T> void write_pod_be(T v) {
        static_assert(std::is_trivially_copyable_v<T>);
        auto u = std::bit_cast<std::array<uint8_t, sizeof(T)>>(v);
        if constexpr (std::endian::native == std::endian::big) {
            write_bytes(u);
        } else {
            for (std::size_t i = 0; i < u.size(); ++i)
                bytes_.push_back(u[u.size() - 1 - i]);
        }
    }

    ByteArray bytes_{};
};

class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    std::size_t remaining() const { return bytes_.size() - offset_; }
    std::size_t offset() const { return offset_; }

    std::expected<uint8_t, Error> read_u8() {
        if (remaining() < 1)
            return std::unexpected(Error{.code = Error::Code::Truncated, .message = "read_u8 truncated"});
        return bytes_[offset_++];
    }

    std::expected<std::span<const uint8_t>, Error> read_span(std::size_t n) {
        if (remaining() < n)
            return std::unexpected(Error{.code = Error::Code::Truncated, .message = "read_span truncated"});
        auto s = bytes_.subspan(offset_, n);
        offset_ += n;
        return s;
    }

    // Little-endian POD readers
    std::expected<int16_t, Error> read_i16_le() { return read_pod_le<int16_t>(); }
    std::expected<uint16_t, Error> read_u16_le() { return read_pod_le<uint16_t>(); }
    std::expected<int32_t, Error> read_i32_le() { return read_pod_le<int32_t>(); }
    std::expected<uint32_t, Error> read_u32_le() { return read_pod_le<uint32_t>(); }
    std::expected<int64_t, Error> read_i64_le() { return read_pod_le<int64_t>(); }
    std::expected<uint64_t, Error> read_u64_le() { return read_pod_le<uint64_t>(); }
    std::expected<float, Error> read_f32_le() { return read_pod_le<float>(); }
    std::expected<double, Error> read_f64_le() { return read_pod_le<double>(); }

    // Big-endian POD readers
    std::expected<int16_t, Error> read_i16_be() { return read_pod_be<int16_t>(); }
    std::expected<uint16_t, Error> read_u16_be() { return read_pod_be<uint16_t>(); }
    std::expected<int32_t, Error> read_i32_be() { return read_pod_be<int32_t>(); }
    std::expected<uint32_t, Error> read_u32_be() { return read_pod_be<uint32_t>(); }
    std::expected<int64_t, Error> read_i64_be() { return read_pod_be<int64_t>(); }
    std::expected<uint64_t, Error> read_u64_be() { return read_pod_be<uint64_t>(); }
    std::expected<float, Error> read_f32_be() { return read_pod_be<float>(); }
    std::expected<double, Error> read_f64_be() { return read_pod_be<double>(); }

    std::expected<uint32_t, Error> read_varuint32() {
        uint32_t result = 0;
        int shift = 0;

        for (int i = 0; i < 5; ++i) {
            auto b = read_u8();
            if (!b)
                return std::unexpected(b.error());

            const uint8_t byte = *b;
            if (i == 4) {
                if ((byte & 0x80) != 0)
                    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "varuint32 too long"});
                if ((byte & 0xF0) != 0)
                    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "varuint32 overflow"});
            }

            result |= (static_cast<uint32_t>(byte & 0x7F) << shift);
            if ((byte & 0x80) == 0)
                return result;

            shift += 7;
        }

        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "varuint32 too long"});
    }

    std::expected<uint64_t, Error> read_varuint64() {
        uint64_t result = 0;
        int shift = 0;

        for (int i = 0; i < 10; ++i) {
            auto b = read_u8();
            if (!b)
                return std::unexpected(b.error());

            const uint8_t byte = *b;
            if (i == 9) {
                if ((byte & 0x80) != 0)
                    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "varuint64 too long"});
                if ((byte & 0xFE) != 0)
                    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "varuint64 overflow"});
            }

            result |= (static_cast<uint64_t>(byte & 0x7F) << shift);
            if ((byte & 0x80) == 0)
                return result;

            shift += 7;
        }

        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "varuint64 too long"});
    }

    // backwards-compatible alias
    std::expected<uint64_t, Error> read_varuint() { return read_varuint64(); }

    std::expected<std::string, Error> read_string() {
        auto len = read_varuint64();
        if (!len)
            return std::unexpected(len.error());
        if (*len > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "string length too large"});

        auto s = read_span(static_cast<std::size_t>(*len));
        if (!s)
            return std::unexpected(s.error());

        return std::string(reinterpret_cast<const char *>(s->data()), s->size());
    }

private:
    template <typename T> std::expected<T, Error> read_pod_le() {
        static_assert(std::is_trivially_copyable_v<T>);

        auto s = read_span(sizeof(T));
        if (!s)
            return std::unexpected(s.error());

        std::array<uint8_t, sizeof(T)> buf{};
        std::memcpy(buf.data(), s->data(), sizeof(T));

        if constexpr (std::endian::native == std::endian::little) {
            return std::bit_cast<T>(buf);
        } else {
            std::array<uint8_t, sizeof(T)> rev{};
            for (std::size_t i = 0; i < buf.size(); ++i)
                rev[i] = buf[buf.size() - 1 - i];
            return std::bit_cast<T>(rev);
        }
    }

    template <typename T> std::expected<T, Error> read_pod_be() {
        static_assert(std::is_trivially_copyable_v<T>);

        auto s = read_span(sizeof(T));
        if (!s)
            return std::unexpected(s.error());

        std::array<uint8_t, sizeof(T)> buf{};
        std::memcpy(buf.data(), s->data(), sizeof(T));

        if constexpr (std::endian::native == std::endian::big) {
            return std::bit_cast<T>(buf);
        } else {
            std::array<uint8_t, sizeof(T)> rev{};
            for (std::size_t i = 0; i < buf.size(); ++i)
                rev[i] = buf[buf.size() - 1 - i];
            return std::bit_cast<T>(rev);
        }
    }

    std::span<const uint8_t> bytes_{};
    std::size_t offset_{};
};

inline uint64_t zigzag_encode_32(int32_t n) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(n) << 1) ^ static_cast<uint64_t>(static_cast<uint32_t>(n >> 31)));
}
inline int32_t zigzag_decode_32(uint64_t u) { return static_cast<int32_t>((u >> 1) ^ (0ULL - (u & 1ULL))); }

inline uint64_t zigzag_encode_64(int64_t n) { return (static_cast<uint64_t>(n) << 1) ^ static_cast<uint64_t>(n >> 63); }
inline int64_t zigzag_decode_64(uint64_t u) { return static_cast<int64_t>((u >> 1) ^ (0ULL - (u & 1ULL))); }
} // namespace luxon::ser
