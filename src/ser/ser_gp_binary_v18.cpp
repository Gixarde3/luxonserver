#include "ser_gp_binary_v18.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace luxon::ser {
namespace {
inline bool f32_is_pos_zero(float a) noexcept { return std::bit_cast<uint32_t>(a) == 0u; }
inline bool f64_is_pos_zero(double a) noexcept { return std::bit_cast<uint64_t>(a) == 0u; }
} // namespace

// -------------------- Type codes --------------------

enum : uint8_t {
    TC_Boolean = 2,
    TC_Byte = 3,
    TC_Int16 = 4,
    TC_Float = 5,
    TC_Double = 6,
    TC_String = 7,
    TC_Null = 8,
    TC_CompressedInt32 = 9,
    TC_CompressedInt64 = 10,
    TC_Int1_Pos = 11,
    TC_Int1_Neg = 12,
    TC_Int2_Pos = 13,
    TC_Int2_Neg = 14,
    TC_L1_Pos = 15,
    TC_L1_Neg = 16,
    TC_L2_Pos = 17,
    TC_L2_Neg = 18,
    TC_Custom_Explicit = 19,
    TC_Dictionary = 20,
    TC_Hashtable = 21,
    TC_ObjectArray = 23,

    TC_BoolFalse = 27,
    TC_BoolTrue = 28,
    TC_Int16Zero = 29,
    TC_Int32Zero = 30,
    TC_Int64Zero = 31,
    TC_FloatZero = 32,
    TC_DoubleZero = 33,
    TC_ByteZero = 34,

    TC_ArrayRecursive = 64,

    TC_BooleanArray = 66,
    TC_ByteArray = 67,
    TC_Int16Array = 68,
    TC_FloatArray = 69,
    TC_DoubleArray = 70,
    TC_StringArray = 71,
    TC_CompressedInt32Array = 73,
    TC_CompressedInt64Array = 74,
};

// -------------------- Parameters --------------------

std::expected<void, Error> GpBinaryV18::encode_parameters(ByteWriter& w, const ParameterList& params, int depth) const {
    if (depth > MAX_DEPTH)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "depth limit in parameters"});

    if (params.size() > 255)
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "parameter count >255"});

    w.write_u8(static_cast<uint8_t>(params.size()));
    for (const auto& [key, value] : params) {
        w.write_u8(key);
        auto enc = encode_value(w, value, depth + 1);
        if (!enc)
            return std::unexpected(enc.error());
    }
    return {};
}

std::expected<ParameterList, Error> GpBinaryV18::decode_parameters(ByteReader& r, int depth) const {
    if (depth > MAX_DEPTH)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "depth limit in parameters"});

    auto cnt_b = r.read_u8();
    if (!cnt_b)
        return std::unexpected(cnt_b.error());

    uint8_t cnt = *cnt_b;
    ParameterList out;
    out.reserve(cnt);

    for (uint32_t i = 0; i < cnt; ++i) {
        auto key = r.read_u8();
        if (!key)
            return std::unexpected(key.error());

        auto val = decode_value(r, depth + 1);
        if (!val)
            return std::unexpected(val.error());

        out.emplace(*key, std::move(*val));
    }

    return out;
}

// -------------------- Typed values --------------------

std::expected<void, Error> GpBinaryV18::encode_value(ByteWriter& w, const Value& v, int depth) const {
    if (depth > MAX_DEPTH)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "value depth limit"});

    return std::visit(
        [&](const auto& a) -> std::expected<void, Error> {
            using T = std::decay_t<decltype(a)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                w.write_u8(TC_Null);
                return {};
            } else if constexpr (std::is_same_v<T, bool>) {
                w.write_u8(a ? TC_BoolTrue : TC_BoolFalse);
                return {};
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                if (a == 0) {
                    w.write_u8(TC_ByteZero);
                    return {};
                }
                w.write_u8(TC_Byte);
                w.write_u8(a);
                return {};
            } else if constexpr (std::is_same_v<T, int16_t>) {
                if (a == 0) {
                    w.write_u8(TC_Int16Zero);
                    return {};
                }
                w.write_u8(TC_Int16);
                w.write_i16_le(a);
                return {};
            } else if constexpr (std::is_same_v<T, int32_t>) {
                if (a == 0) {
                    w.write_u8(TC_Int32Zero);
                    return {};
                }

                int64_t mag64 = std::llabs(static_cast<long long>(a));
                if (mag64 <= 255) {
                    w.write_u8(a >= 0 ? TC_Int1_Pos : TC_Int1_Neg);
                    w.write_u8(static_cast<uint8_t>(mag64));
                    return {};
                }
                if (mag64 <= 65535) {
                    w.write_u8(a >= 0 ? TC_Int2_Pos : TC_Int2_Neg);
                    w.write_u16_le(static_cast<uint16_t>(mag64));
                    return {};
                }

                w.write_u8(TC_CompressedInt32);
                w.write_varuint(zigzag_encode_32(a));
                return {};
            } else if constexpr (std::is_same_v<T, int64_t>) {
                if (a == 0) {
                    w.write_u8(TC_Int64Zero);
                    return {};
                }

                uint64_t ua = static_cast<uint64_t>(a);
                uint64_t mag = (a < 0) ? (uint64_t{0} - ua) : ua;

                if (mag <= 255) {
                    w.write_u8(a >= 0 ? TC_L1_Pos : TC_L1_Neg);
                    w.write_u8(static_cast<uint8_t>(mag));
                    return {};
                }
                if (mag <= 65535) {
                    w.write_u8(a >= 0 ? TC_L2_Pos : TC_L2_Neg);
                    w.write_u16_le(static_cast<uint16_t>(mag));
                    return {};
                }

                w.write_u8(TC_CompressedInt64);
                w.write_varuint(zigzag_encode_64(a));
                return {};
            } else if constexpr (std::is_same_v<T, float>) {
                // Preserve -0.0f
                if (f32_is_pos_zero(a)) {
                    w.write_u8(TC_FloatZero);
                    return {};
                }
                w.write_u8(TC_Float);
                w.write_f32_le(a);
                return {};
            } else if constexpr (std::is_same_v<T, double>) {
                // Preserve -0.0. Only +0.0 uses the compact zero code
                if (f64_is_pos_zero(a)) {
                    w.write_u8(TC_DoubleZero);
                    return {};
                }
                w.write_u8(TC_Double);
                w.write_f64_le(a);
                return {};
            } else if constexpr (std::is_same_v<T, std::string>) {
                w.write_u8(TC_String);
                w.write_string(a);
                return {};
            } else if constexpr (std::is_same_v<T, ByteArray>) {
                w.write_u8(TC_ByteArray);
                w.write_varuint(a.size());
                w.write_bytes(a);
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
                w.write_u8(TC_BooleanArray);
                w.write_varuint(a.size());

                std::size_t nbytes = (a.size() + 7) / 8;
                for (std::size_t bi = 0; bi < nbytes; ++bi) {
                    uint8_t byte = 0;
                    for (int bit = 0; bit < 8; ++bit) {
                        std::size_t idx = bi * 8 + static_cast<std::size_t>(bit);
                        if (idx >= a.size())
                            break;
                        if (a[idx])
                            byte |= static_cast<uint8_t>(1u << bit);
                    }
                    w.write_u8(byte);
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<int16_t>>) {
                w.write_u8(TC_Int16Array);
                w.write_varuint(a.size());
                for (int16_t x : a)
                    w.write_i16_le(x);
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
                w.write_u8(TC_CompressedInt32Array);
                w.write_varuint(a.size());
                for (int32_t x : a)
                    w.write_varuint(zigzag_encode_32(x));
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                w.write_u8(TC_CompressedInt64Array);
                w.write_varuint(a.size());
                for (int64_t x : a)
                    w.write_varuint(zigzag_encode_64(x));
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<float>>) {
                w.write_u8(TC_FloatArray);
                w.write_varuint(a.size());
                for (float x : a)
                    w.write_f32_le(x);
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                w.write_u8(TC_DoubleArray);
                w.write_varuint(a.size());
                for (double x : a)
                    w.write_f64_le(x);
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                w.write_u8(TC_StringArray);
                w.write_varuint(a.size());
                for (const auto& s : a)
                    w.write_string(s);
                return {};
            } else if constexpr (std::is_same_v<T, ObjectArray>) {
                w.write_u8(TC_ObjectArray);
                w.write_varuint(a.size());
                for (const auto& e : a) {
                    auto enc = encode_value(w, e, depth + 1);
                    if (!enc)
                        return std::unexpected(enc.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, Dictionary>) {
                w.write_u8(TC_Dictionary);
                w.write_u8(TC_Byte); // key-type byte (preserved for compatibility)
                w.write_u8(0x00);    // value-type "unknown/0"
                w.write_varuint(a.size());
                for (const auto& [k, val] : a) {
                    w.write_u8(k);
                    auto enc = encode_value(w, val, depth + 1);
                    if (!enc)
                        return std::unexpected(enc.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Hashtable>>) {
                w.write_u8(TC_Hashtable);
                if (!a) {
                    w.write_varuint(0);
                    return {};
                }

                w.write_varuint(a->size());
                for (const auto& [k, val] : *a) {
                    auto ek = encode_value(w, k, depth + 1);
                    if (!ek)
                        return std::unexpected(ek.error());
                    auto ev = encode_value(w, val, depth + 1);
                    if (!ev)
                        return std::unexpected(ev.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, RawCustomValue>) {
                if (a.custom_code < 100) {
                    w.write_u8(static_cast<uint8_t>(128 + a.custom_code));
                    w.write_varuint(a.data.size());
                    w.write_bytes(a.data);
                    return {};
                }

                w.write_u8(TC_Custom_Explicit);
                w.write_u8(a.custom_code);
                w.write_varuint(a.data.size());
                w.write_bytes(a.data);
                return {};
            } else {
                return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "unsupported variant member"});
            }
        },
        v.value);
}

std::expected<Value, Error> GpBinaryV18::decode_value(ByteReader& r, int depth) const {
    if (depth > MAX_DEPTH)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "value depth limit"});

    auto tc = r.read_u8();
    if (!tc)
        return std::unexpected(tc.error());

    uint8_t t = *tc;

    // optimized custom range
    if (t >= 128 && t <= 227) {
        uint8_t custom_code = static_cast<uint8_t>(t - 128);
        auto size = r.read_varuint();
        if (!size)
            return std::unexpected(size.error());

        auto bytes = r.read_span(static_cast<std::size_t>(*size));
        if (!bytes)
            return std::unexpected(bytes.error());

        RawCustomValue cv{.custom_code = custom_code, .data = ByteArray(bytes->begin(), bytes->end())};
        return Value(std::move(cv));
    }

    switch (t) {
    case TC_Boolean: {
        auto b = r.read_u8();
        if (!b)
            return std::unexpected(b.error());
        if (*b != 0 && *b != 1)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "boolean not 0/1"});
        return Value(*b == 1);
    }
    case TC_BoolFalse:
        return Value(false);
    case TC_BoolTrue:
        return Value(true);

    case TC_Byte: {
        auto b = r.read_u8();
        if (!b)
            return std::unexpected(b.error());
        return Value(*b);
    }
    case TC_ByteZero:
        return Value(static_cast<uint8_t>(0));

    case TC_Int16: {
        auto v = r.read_i16_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case TC_Int16Zero:
        return Value(static_cast<int16_t>(0));

    case TC_Float: {
        auto v = r.read_f32_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case TC_FloatZero:
        return Value(0.0f);

    case TC_Double: {
        auto v = r.read_f64_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case TC_DoubleZero:
        return Value(0.0);

    case TC_String: {
        auto s = r.read_string();
        if (!s)
            return std::unexpected(s.error());
        return Value(std::move(*s));
    }

    case TC_Null:
        return Value(std::monostate{});

    case TC_CompressedInt32: {
        auto u = r.read_varuint();
        if (!u)
            return std::unexpected(u.error());
        return Value(zigzag_decode_32(*u));
    }
    case TC_CompressedInt64: {
        auto u = r.read_varuint();
        if (!u)
            return std::unexpected(u.error());
        return Value(zigzag_decode_64(*u));
    }

    case TC_Int1_Pos:
    case TC_Int1_Neg: {
        auto mag = r.read_u8();
        if (!mag)
            return std::unexpected(mag.error());
        int32_t v = static_cast<int32_t>(*mag);
        if (t == TC_Int1_Neg)
            v = -v;
        return Value(v);
    }
    case TC_Int2_Pos:
    case TC_Int2_Neg: {
        auto mag = r.read_u16_le();
        if (!mag)
            return std::unexpected(mag.error());
        int32_t v = static_cast<int32_t>(*mag);
        if (t == TC_Int2_Neg)
            v = -v;
        return Value(v);
    }

    case TC_L1_Pos:
    case TC_L1_Neg: {
        auto mag = r.read_u8();
        if (!mag)
            return std::unexpected(mag.error());
        int64_t v = static_cast<int64_t>(*mag);
        if (t == TC_L1_Neg)
            v = -v;
        return Value(v);
    }
    case TC_L2_Pos:
    case TC_L2_Neg: {
        auto mag = r.read_u16_le();
        if (!mag)
            return std::unexpected(mag.error());
        int64_t v = static_cast<int64_t>(*mag);
        if (t == TC_L2_Neg)
            v = -v;
        return Value(v);
    }

    case TC_Int32Zero:
        return Value(static_cast<int32_t>(0));
    case TC_Int64Zero:
        return Value(static_cast<int64_t>(0));

    case TC_Custom_Explicit: {
        auto code = r.read_u8();
        if (!code)
            return std::unexpected(code.error());

        auto size = r.read_varuint();
        if (!size)
            return std::unexpected(size.error());

        auto bytes = r.read_span(static_cast<std::size_t>(*size));
        if (!bytes)
            return std::unexpected(bytes.error());

        RawCustomValue cv{.custom_code = *code, .data = ByteArray(bytes->begin(), bytes->end())};
        return Value(std::move(cv));
    }

    case TC_ObjectArray:
    case TC_ArrayRecursive: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        ObjectArray out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint64_t i = 0; i < *count; ++i) {
            auto v = decode_value(r, depth + 1);
            if (!v)
                return std::unexpected(v.error());
            out.push_back(std::move(*v));
        }
        return Value(std::move(out));
    }

    case TC_Dictionary: {
        auto kt = r.read_u8();
        if (!kt)
            return std::unexpected(kt.error());
        auto vt = r.read_u8();
        if (!vt)
            return std::unexpected(vt.error());

        // This implementation only supports Dictionary<byte, object>
        if (*kt != TC_Byte)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "only dictionary<byte,*> supported"});
        (void)vt; // ignored; values are decoded as typed objects

        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        Dictionary out;
        for (uint64_t i = 0; i < *count; ++i) {
            auto key = r.read_u8();
            if (!key)
                return std::unexpected(key.error());
            auto val = decode_value(r, depth + 1);
            if (!val)
                return std::unexpected(val.error());
            out[*key] = std::move(*val);
        }
        return Value(std::move(out));
    }

    case TC_Hashtable: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        auto ht = std::make_shared<Hashtable>();
        for (uint64_t i = 0; i < *count; ++i) {
            auto k = decode_value(r, depth + 1);
            if (!k)
                return std::unexpected(k.error());
            auto v = decode_value(r, depth + 1);
            if (!v)
                return std::unexpected(v.error());
            ht->emplace(std::move(*k), std::move(*v));
        }
        return Value(ht);
    }

    case TC_BooleanArray: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        std::size_t n = static_cast<std::size_t>(*count);
        std::size_t nbytes = (n + 7) / 8;

        auto raw = r.read_span(nbytes);
        if (!raw)
            return std::unexpected(raw.error());

        std::vector<bool> out;
        out.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t bi = i / 8;
            int bit = static_cast<int>(i % 8);
            out[i] = (((*raw)[bi] >> bit) & 1u) != 0;
        }
        return Value(std::move(out));
    }

    case TC_ByteArray: {
        auto len = r.read_varuint();
        if (!len)
            return std::unexpected(len.error());
        auto s = r.read_span(static_cast<std::size_t>(*len));
        if (!s)
            return std::unexpected(s.error());
        return Value(ByteArray(s->begin(), s->end()));
    }

    case TC_Int16Array: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        std::vector<int16_t> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint64_t i = 0; i < *count; ++i) {
            auto v = r.read_i16_le();
            if (!v)
                return std::unexpected(v.error());
            out.push_back(*v);
        }
        return Value(std::move(out));
    }

    case TC_CompressedInt32Array: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        std::vector<int32_t> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint64_t i = 0; i < *count; ++i) {
            auto u = r.read_varuint();
            if (!u)
                return std::unexpected(u.error());
            out.push_back(zigzag_decode_32(*u));
        }
        return Value(std::move(out));
    }

    case TC_CompressedInt64Array: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        std::vector<int64_t> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint64_t i = 0; i < *count; ++i) {
            auto u = r.read_varuint();
            if (!u)
                return std::unexpected(u.error());
            out.push_back(zigzag_decode_64(*u));
        }
        return Value(std::move(out));
    }

    case TC_FloatArray: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint64_t i = 0; i < *count; ++i) {
            auto v = r.read_f32_le();
            if (!v)
                return std::unexpected(v.error());
            out.push_back(*v);
        }
        return Value(std::move(out));
    }

    case TC_DoubleArray: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        std::vector<double> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint64_t i = 0; i < *count; ++i) {
            auto v = r.read_f64_le();
            if (!v)
                return std::unexpected(v.error());
            out.push_back(*v);
        }
        return Value(std::move(out));
    }

    case TC_StringArray: {
        auto count = r.read_varuint();
        if (!count)
            return std::unexpected(count.error());

        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint64_t i = 0; i < *count; ++i) {
            auto s = r.read_string();
            if (!s)
                return std::unexpected(s.error());
            out.push_back(std::move(*s));
        }
        return Value(std::move(out));
    }

    default:
        return std::unexpected(Error{.code = Error::Code::UnsupportedTypeCode, .message = "unknown type code"});
    }
}

// -------------------- Serialize --------------------

std::expected<ByteArray, Error> GpBinaryV18::Serialize(const Message& message) {
    Kind kind{};
    ByteWriter payload;

    auto enc = std::visit(
        [&](const auto& m) -> std::expected<void, Error> {
            using T = std::decay_t<decltype(m)>;

            if constexpr (std::is_same_v<T, InitMessage>) {
                kind = Kind::Init;

                // fixed 39 bytes payload
                payload.write_u8(m.protocol_major);
                payload.write_u8(m.protocol_minor);
                payload.write_u8(static_cast<uint8_t>((m.client_sdk_id << 1) & 0xFE));

                uint8_t vcombined = 0;
                if (m.ipv6)
                    vcombined |= 0x80;
                vcombined |= static_cast<uint8_t>((m.version_major & 0x07) << 4);
                vcombined |= static_cast<uint8_t>((m.version_minor & 0x0F));
                payload.write_u8(vcombined);

                payload.write_u8(m.version_patch);
                payload.write_u8(m.version_revision);
                payload.write_u8(0x00);

                std::array<uint8_t, 32> app{};
                std::memset(app.data(), 0, app.size());
                std::size_t n = std::min<std::size_t>(app.size(), m.app_id.size());
                std::memcpy(app.data(), m.app_id.data(), n);
                payload.write_bytes(app);

                return {};
            } else if constexpr (std::is_same_v<T, InitResponseMessage>) {
                kind = Kind::InitResponse;
                return {};
            } else if constexpr (std::is_same_v<T, OperationRequestMessage>) {
                kind = Kind::Operation;
                payload.write_u8(m.operation_code);
                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, OperationResponseMessage>) {
                kind = Kind::OperationResponse;
                payload.write_u8(m.operation_code);
                payload.write_i16_le(m.return_code);

                if (m.debug_message.has_value()) {
                    payload.write_u8(TC_String);
                    payload.write_string(*m.debug_message);
                } else {
                    payload.write_u8(TC_Null);
                }

                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, EventMessage>) {
                kind = Kind::Event;
                payload.write_u8(m.event_code);
                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, DisconnectMessage>) {
                kind = Kind::DisconnectMessage;
                payload.write_i16_le(m.code);

                if (m.message.has_value()) {
                    payload.write_u8(TC_String);
                    payload.write_string(*m.message);
                } else {
                    payload.write_u8(TC_Null);
                }

                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, InternalOperationRequestMessage>) {
                kind = Kind::InternalOperationRequest;
                payload.write_u8(m.operation_code);
                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, InternalOperationResponseMessage>) {
                kind = Kind::InternalOperationResponse;
                payload.write_u8(m.operation_code);
                payload.write_i16_le(m.return_code);

                if (m.debug_message.has_value()) {
                    payload.write_u8(TC_String);
                    payload.write_string(*m.debug_message);
                } else {
                    payload.write_u8(TC_Null);
                }

                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, GenericValueMessage>) {
                kind = Kind::Message;
                return encode_value(payload, m.value, 0);
            } else if constexpr (std::is_same_v<T, RawMessage>) {
                kind = Kind::RawMessage;
                payload.write_bytes(m.bytes);
                return {};
            } else {
                return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "unknown message variant"});
            }
        },
        message);

    if (!enc)
        return std::unexpected(enc.error());

    bool allow_encrypt = message.encrypted;
    if (kind == Kind::Init || kind == Kind::InitResponse || kind == Kind::DisconnectMessage)
        allow_encrypt = false;

    auto final_payload = maybe_encrypt_payload(kind, allow_encrypt, payload.bytes());
    if (!final_payload)
        return std::unexpected(final_payload.error());

    ByteWriter packet;
    packet.write_u8(GP_MAGIC);

    uint8_t b1 = static_cast<uint8_t>(kind) & 0x7F;
    if (allow_encrypt)
        b1 |= 0x80;
    packet.write_u8(b1);

    packet.write_bytes(*final_payload);
    return packet.take();
}

// -------------------- Deserialize --------------------

std::expected<Message, Error> GpBinaryV18::Deserialize(std::span<const uint8_t> packet_bytes) {
    if (packet_bytes.size() < 2)
        return std::unexpected(Error{.code = Error::Code::BadPacket, .message = "packet too short"});

    if (packet_bytes[0] != GP_MAGIC)
        return std::unexpected(Error{.code = Error::Code::BadMagic, .message = "bad magic"});

    uint8_t b1 = packet_bytes[1];
    bool encrypted = (b1 & 0x80) != 0;
    uint8_t kind_u = (b1 & 0x7F);

    Kind kind = static_cast<Kind>(kind_u);

    std::span<const uint8_t> payload = packet_bytes.subspan(2);

    auto plaintext = maybe_decrypt_payload(kind, encrypted, payload);
    if (!plaintext)
        return std::unexpected(plaintext.error());

    ByteReader r(*plaintext);

    switch (kind) {
    case Kind::Init: {
        if (encrypted)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "init marked encrypted"});

        if (plaintext->size() != 39)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "init payload must be 39 bytes"});

        auto protocol_major = r.read_u8();
        auto protocol_minor = r.read_u8();
        auto sdk_enc = r.read_u8();
        auto vcombined = r.read_u8();
        auto patch = r.read_u8();
        auto rev = r.read_u8();
        auto pad = r.read_u8();
        (void)pad;

        if (!protocol_major || !protocol_minor || !sdk_enc || !vcombined || !patch || !rev)
            return std::unexpected(Error{.code = Error::Code::Truncated, .message = "init truncated"});

        auto app_span = r.read_span(32);
        if (!app_span)
            return std::unexpected(app_span.error());

        std::size_t end = 0;
        while (end < 32 && (*app_span)[end] != 0x00)
            ++end;

        std::string app_id(reinterpret_cast<const char *>(app_span->data()), end);

        InitMessage m{
            .protocol_major = *protocol_major,
            .protocol_minor = *protocol_minor,
            .client_sdk_id = static_cast<uint8_t>((*sdk_enc) >> 1),
            .ipv6 = ((*vcombined) & 0x80) != 0,
            .version_major = static_cast<uint8_t>(((*vcombined) >> 4) & 0x07),
            .version_minor = static_cast<uint8_t>((*vcombined) & 0x0F),
            .version_patch = *patch,
            .version_revision = *rev,
            .app_id = std::move(app_id),
        };

        if (r.remaining() != 0)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "init extra bytes"});

        return Message(std::move(m));
    }

    case Kind::InitResponse: {
        if (encrypted)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "initresponse marked encrypted"});
        if (plaintext->size() > 1 || (plaintext->size() == 1 && plaintext->front() != '\0'))
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "initresponse must have no payload"});
        return Message(InitResponseMessage{});
    }

    case Kind::Event: {
        auto code = r.read_u8();
        if (!code)
            return std::unexpected(code.error());

        auto params = decode_parameters(r, 0);
        if (!params)
            return std::unexpected(params.error());

        if (r.remaining() != 0)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "event extra bytes"});

        return Message(EventMessage{.event_code = *code, .parameters = std::move(*params)}, encrypted);
    }

    case Kind::Operation:
    case Kind::InternalOperationRequest: {
        auto op = r.read_u8();
        if (!op)
            return std::unexpected(op.error());

        auto params = decode_parameters(r, 0);
        if (!params)
            return std::unexpected(params.error());

        if (r.remaining() != 0)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "operation extra bytes"});

        if (kind == Kind::Operation)
            return Message(OperationRequestMessage{.operation_code = *op, .parameters = std::move(*params)}, encrypted);
        return Message(InternalOperationRequestMessage{.operation_code = *op, .parameters = std::move(*params)}, encrypted);
    }

    case Kind::OperationResponse:
    case Kind::InternalOperationResponse: {
        auto op = r.read_u8();
        if (!op)
            return std::unexpected(op.error());

        auto rc = r.read_i16_le();
        if (!rc)
            return std::unexpected(rc.error());

        auto debug_type = r.read_u8();
        if (!debug_type)
            return std::unexpected(debug_type.error());

        std::optional<std::string> debug_message;
        if (*debug_type == TC_String) {
            auto s = r.read_string();
            if (!s)
                return std::unexpected(s.error());
            debug_message = std::move(*s);
        } else if (*debug_type == TC_Null) {
            debug_message.reset();
        } else {
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "invalid debug type"});
        }

        auto params = decode_parameters(r, 0);
        if (!params)
            return std::unexpected(params.error());

        if (r.remaining() != 0)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "operation response extra bytes"});

        if (kind == Kind::OperationResponse) {
            return Message(
                OperationResponseMessage{
                    .operation_code = *op,
                    .return_code = *rc,
                    .debug_message = std::move(debug_message),
                    .parameters = std::move(*params),
                },
                encrypted);
        }

        return Message(
            InternalOperationResponseMessage{
                .operation_code = *op,
                .return_code = *rc,
                .debug_message = std::move(debug_message),
                .parameters = std::move(*params),
            },
            encrypted);
    }

    case Kind::DisconnectMessage: {
        auto code = r.read_i16_le();
        if (!code)
            return std::unexpected(code.error());

        auto msg_type = r.read_u8();
        if (!msg_type)
            return std::unexpected(msg_type.error());

        std::optional<std::string> msg;
        if (*msg_type == TC_String) {
            auto s = r.read_string();
            if (!s)
                return std::unexpected(s.error());
            msg = std::move(*s);
        } else if (*msg_type == TC_Null) {
            msg.reset();
        } else {
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "disconnect invalid message type"});
        }

        auto params = decode_parameters(r, 0);
        if (!params)
            return std::unexpected(params.error());

        if (r.remaining() != 0)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "disconnect extra bytes"});

        return Message(DisconnectMessage{.code = *code, .message = std::move(msg), .parameters = std::move(*params)});
    }

    case Kind::Message: {
        auto v = decode_value(r, 0);
        if (!v)
            return std::unexpected(v.error());
        if (r.remaining() != 0)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "message extra bytes"});
        return Message(GenericValueMessage{.value = std::move(*v)}, encrypted);
    }

    case Kind::RawMessage: {
        // payload is raw bytes
        if (r.remaining() != 0)
            return Message(RawMessage{.bytes = ByteArray(plaintext->begin(), plaintext->end())}, encrypted);
        return Message(RawMessage{.bytes = ByteArray{}}, encrypted);
    }

    default:
        return std::unexpected(Error{.code = Error::Code::UnsupportedKind, .message = "unsupported message kind"});
    }
}
} // namespace luxon::ser
