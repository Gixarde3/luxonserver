// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "ser_gp_binary_v16.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace luxon::ser {
namespace {
// Protocol 16 uses ASCII-ish type codes
enum : uint8_t {
    TC16_Unknown = 0,        // also used as "unknown type" in containers
    TC16_Null = 42,          // '*'
    TC16_Boolean = 111,      // 'o'
    TC16_Byte = 98,          // 'b'
    TC16_Int16 = 107,        // 'k'
    TC16_Int32 = 105,        // 'i'
    TC16_Int64 = 108,        // 'l'
    TC16_Float = 102,        // 'f'
    TC16_Double = 100,       // 'd'
    TC16_String = 115,       // 's'
    TC16_ByteArray = 120,    // 'x'
    TC16_IntArray = 110,     // 'n'
    TC16_StringArray = 97,   // 'a'
    TC16_GenericArray = 121, // 'y'
    TC16_ObjectArray = 122,  // 'z'
    TC16_Hashtable = 104,    // 'h'
    TC16_Dictionary = 68,    // 'D'
    TC16_Custom = 99,        // 'c'
};

inline std::expected<void, Error> ensure_u16_size(std::size_t n, const char *what) {
    if (n > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max()))
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = std::string(what) + " too large for u16"});
    return {};
}

inline std::expected<void, Error> ensure_u32_size(std::size_t n, const char *what) {
    if (n > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max()))
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = std::string(what) + " too large for u32"});
    return {};
}
} // namespace

// -------------------- V16 u16-string helpers --------------------

std::expected<void, Error> GpBinaryV16::write_string_u16(ByteWriter& w, const std::string& s) const {
    auto ok = ensure_u16_size(s.size(), "string length");
    if (!ok)
        return std::unexpected(ok.error());
    w.write_u16_be(static_cast<uint16_t>(s.size()));
    w.write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
    return {};
}

std::expected<std::string, Error> GpBinaryV16::read_string_u16(ByteReader& r) const {
    auto len = r.read_u16_be();
    if (!len)
        return std::unexpected(len.error());

    auto s = r.read_span(static_cast<std::size_t>(*len));
    if (!s)
        return std::unexpected(s.error());

    return std::string(reinterpret_cast<const char *>(s->data()), s->size());
}

// -------------------- Parameters --------------------

std::expected<void, Error> GpBinaryV16::encode_parameters(ByteWriter& w, const ParameterList& params, int depth) const {
    if (depth > MAX_DEPTH)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "depth limit in parameters"});

    auto ok = ensure_u16_size(params.size(), "parameter count");
    if (!ok)
        return std::unexpected(ok.error());

    w.write_u16_be(static_cast<uint16_t>(params.size()));
    for (const auto& [key, value] : params) {
        w.write_u8(key);
        auto enc = EncodeValue(w, value, depth + 1);
        if (!enc)
            return std::unexpected(enc.error());
    }
    return {};
}

std::expected<ParameterList, Error> GpBinaryV16::decode_parameters(ByteReader& r, int depth) const {
    if (depth > MAX_DEPTH)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "depth limit in parameters"});

    auto cnt_be = r.read_u16_be();
    if (!cnt_be)
        return std::unexpected(cnt_be.error());

    uint16_t cnt = *cnt_be;

    ParameterList out;
    out.reserve(cnt);

    for (uint32_t i = 0; i < cnt; ++i) {
        auto key = r.read_u8();
        if (!key)
            return std::unexpected(key.error());

        auto val = DecodeValue(r, depth + 1);
        if (!val)
            return std::unexpected(val.error());

        out.emplace(*key, std::move(*val));
    }

    return out;
}

// -------------------- Typed values --------------------

std::expected<void, Error> GpBinaryV16::EncodeValue(ByteWriter& w, const Value& v, int depth) const {
    if (depth > MAX_DEPTH)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "value depth limit"});

    return std::visit(
        [&](const auto& a) -> std::expected<void, Error> {
            using T = std::decay_t<decltype(a)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                w.write_u8(TC16_Null);
                return {};
            } else if constexpr (std::is_same_v<T, bool>) {
                w.write_u8(TC16_Boolean);
                w.write_u8(a ? 1 : 0);
                return {};
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                w.write_u8(TC16_Byte);
                w.write_u8(a);
                return {};
            } else if constexpr (std::is_same_v<T, int16_t>) {
                w.write_u8(TC16_Int16);
                w.write_i16_be(a);
                return {};
            } else if constexpr (std::is_same_v<T, int32_t>) {
                w.write_u8(TC16_Int32);
                w.write_i32_be(a);
                return {};
            } else if constexpr (std::is_same_v<T, int64_t>) {
                w.write_u8(TC16_Int64);
                w.write_i64_be(a);
                return {};
            } else if constexpr (std::is_same_v<T, float>) {
                w.write_u8(TC16_Float);
                w.write_f32_be(a);
                return {};
            } else if constexpr (std::is_same_v<T, double>) {
                w.write_u8(TC16_Double);
                w.write_f64_be(a);
                return {};
            } else if constexpr (std::is_same_v<T, std::string>) {
                w.write_u8(TC16_String);
                return write_string_u16(w, a);
            } else if constexpr (std::is_same_v<T, ByteArray>) {
                // optimized byte[]: 'x' + i32 length + raw bytes
                auto ok = ensure_u32_size(a.size(), "byte[] length");
                if (!ok)
                    return std::unexpected(ok.error());

                w.write_u8(TC16_ByteArray);
                w.write_u32_be(static_cast<uint32_t>(a.size()));
                w.write_bytes(a);
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                // optimized string[]: 'a' + u16 count + (u16 len + bytes)...
                auto ok = ensure_u16_size(a.size(), "string[] count");
                if (!ok)
                    return std::unexpected(ok.error());

                w.write_u8(TC16_StringArray);
                w.write_u16_be(static_cast<uint16_t>(a.size()));
                for (const auto& s : a) {
                    auto encs = write_string_u16(w, s);
                    if (!encs)
                        return std::unexpected(encs.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, ObjectArray>) {
                // object[]: 'z' + u16 count + each element (typed)
                auto ok = ensure_u16_size(a.size(), "object[] count");
                if (!ok)
                    return std::unexpected(ok.error());

                w.write_u8(TC16_ObjectArray);
                w.write_u16_be(static_cast<uint16_t>(a.size()));
                for (const auto& e : a) {
                    auto enc = EncodeValue(w, e, depth + 1);
                    if (!enc)
                        return std::unexpected(enc.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, Dictionary>) {
                // dictionary: 'D' + keyType + valueType + u16 count + entries...
                // We encode keyType=byte, valueType=unknown (0) to allow heterogenous values.
                auto ok = ensure_u16_size(a.size(), "dictionary count");
                if (!ok)
                    return std::unexpected(ok.error());

                w.write_u8(TC16_Dictionary);
                w.write_u8(TC16_Byte);
                w.write_u8(TC16_Unknown);
                w.write_u16_be(static_cast<uint16_t>(a.size()));
                for (const auto& [k, val] : a) {
                    w.write_u8(k);
                    auto enc = EncodeValue(w, val, depth + 1);
                    if (!enc)
                        return std::unexpected(enc.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Hashtable>>) {
                // hashtable: 'h' + u16 count + (key,value) objects
                w.write_u8(TC16_Hashtable);
                if (!a) {
                    w.write_u16_be(0);
                    return {};
                }

                auto ok = ensure_u16_size(a->size(), "hashtable count");
                if (!ok)
                    return std::unexpected(ok.error());

                w.write_u16_be(static_cast<uint16_t>(a->size()));
                for (const auto& [k, val] : *a) {
                    auto ek = EncodeValue(w, k, depth + 1);
                    if (!ek)
                        return std::unexpected(ek.error());
                    auto ev = EncodeValue(w, val, depth + 1);
                    if (!ev)
                        return std::unexpected(ev.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, RawCustomValue>) {
                // custom: 'c' + code + u16 length + bytes
                auto ok = ensure_u16_size(a.data.size(), "custom payload length");
                if (!ok)
                    return std::unexpected(ok.error());

                w.write_u8(TC16_Custom);
                w.write_u8(a.custom_code);
                w.write_u16_be(static_cast<uint16_t>(a.data.size()));
                w.write_bytes(a.data);
                return {};
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<bool>> || std::is_same_v<T, std::vector<int16_t>> ||
                                 std::is_same_v<T, std::vector<int32_t>> || std::is_same_v<T, std::vector<int64_t>> || std::is_same_v<T, std::vector<float>> ||
                                 std::is_same_v<T, std::vector<double>>) {
                // Generic array: 'y' + u16 count + elementType + raw element payloads (no per-element headers)
                auto ok = ensure_u16_size(a.size(), "generic array count");
                if (!ok)
                    return std::unexpected(ok.error());

                w.write_u8(TC16_GenericArray);
                w.write_u16_be(static_cast<uint16_t>(a.size()));

                if constexpr (std::is_same_v<T, std::string>) {
                    w.write_u8(TC16_String);
                    for (const auto& s : a) {
                        auto encs = write_string_u16(w, s);
                        if (!encs)
                            return std::unexpected(encs.error());
                    }
                } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
                    w.write_u8(TC16_Boolean);
                    for (bool b : a)
                        w.write_u8(b ? 1 : 0);
                } else if constexpr (std::is_same_v<T, std::vector<int16_t>>) {
                    w.write_u8(TC16_Int16);
                    for (int16_t x : a)
                        w.write_i16_be(x);
                } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
                    w.write_u8(TC16_Int32);
                    for (int32_t x : a)
                        w.write_i32_be(x);
                } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                    w.write_u8(TC16_Int64);
                    for (int64_t x : a)
                        w.write_i64_be(x);
                } else if constexpr (std::is_same_v<T, std::vector<float>>) {
                    w.write_u8(TC16_Float);
                    for (float x : a)
                        w.write_f32_be(x);
                } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                    w.write_u8(TC16_Double);
                    for (double x : a)
                        w.write_f64_be(x);

                } else if constexpr (std::is_same_v<T, PreSerializedValue>) {
                    w.write_bytes(a.data);
                }
                return {};
            } else {
                return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "unsupported variant member for GpBinaryV16"});
            }
        },
        v.value);
}

std::expected<Value, Error> GpBinaryV16::DecodeValue(ByteReader& r, int depth) const {
    if (depth > MAX_DEPTH)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "value depth limit"});

    auto tc = r.read_u8();
    if (!tc)
        return std::unexpected(tc.error());

    uint8_t t = *tc;

    switch (t) {
    case TC16_Unknown:
    case TC16_Null:
        return Value(std::monostate{});

    case TC16_Boolean: {
        auto b = r.read_u8();
        if (!b)
            return std::unexpected(b.error());
        if (*b != 0 && *b != 1)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "boolean not 0/1"});
        return Value(*b == 1);
    }

    case TC16_Byte: {
        auto b = r.read_u8();
        if (!b)
            return std::unexpected(b.error());
        return Value(*b);
    }

    case TC16_Int16: {
        auto v = r.read_i16_be();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC16_Int32: {
        auto v = r.read_i32_be();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC16_Int64: {
        auto v = r.read_i64_be();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC16_Float: {
        auto v = r.read_f32_be();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC16_Double: {
        auto v = r.read_f64_be();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC16_String: {
        auto s = read_string_u16(r);
        if (!s)
            return std::unexpected(s.error());
        return Value(std::move(*s));
    }

    case TC16_ByteArray: {
        auto len = r.read_u32_be();
        if (!len)
            return std::unexpected(len.error());

        auto bytes = r.read_span(static_cast<std::size_t>(*len));
        if (!bytes)
            return std::unexpected(bytes.error());

        return Value(ByteArray(bytes->begin(), bytes->end()));
    }

    case TC16_IntArray: {
        auto count = r.read_u16_be();
        if (!count)
            return std::unexpected(count.error());

        std::vector<int32_t> out;
        out.reserve(*count);
        for (uint32_t i = 0; i < *count; ++i) {
            auto v = r.read_i32_be();
            if (!v)
                return std::unexpected(v.error());
            out.push_back(*v);
        }
        return Value(std::move(out));
    }

    case TC16_StringArray: {
        auto count = r.read_u16_be();
        if (!count)
            return std::unexpected(count.error());

        std::vector<std::string> out;
        out.reserve(*count);
        for (uint32_t i = 0; i < *count; ++i) {
            auto s = read_string_u16(r);
            if (!s)
                return std::unexpected(s.error());
            out.push_back(std::move(*s));
        }
        return Value(std::move(out));
    }

    case TC16_ObjectArray: {
        auto count = r.read_u16_be();
        if (!count)
            return std::unexpected(count.error());

        ObjectArray out;
        out.reserve(*count);
        for (uint32_t i = 0; i < *count; ++i) {
            auto v = DecodeValue(r, depth + 1);
            if (!v)
                return std::unexpected(v.error());
            out.push_back(std::move(*v));
        }
        return Value(std::move(out));
    }

    case TC16_GenericArray: {
        auto count = r.read_u16_be();
        if (!count)
            return std::unexpected(count.error());

        auto et = r.read_u8();
        if (!et)
            return std::unexpected(et.error());

        uint16_t n = *count;
        uint8_t elem_tc = *et;

        // If element type is unknown/null, elements are individually typed.
        if (elem_tc == TC16_Unknown || elem_tc == TC16_Null) {
            ObjectArray out;
            out.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto v = DecodeValue(r, depth + 1);
                if (!v)
                    return std::unexpected(v.error());
                out.push_back(std::move(*v));
            }
            return Value(std::move(out));
        }

        switch (elem_tc) {
        case TC16_Boolean: {
            std::vector<bool> out;
            out.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto b = r.read_u8();
                if (!b)
                    return std::unexpected(b.error());
                if (*b != 0 && *b != 1)
                    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "boolean array element not 0/1"});
                out[i] = (*b == 1);
            }
            return Value(std::move(out));
        }
        case TC16_Int16: {
            std::vector<int16_t> out;
            out.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto v = r.read_i16_be();
                if (!v)
                    return std::unexpected(v.error());
                out.push_back(*v);
            }
            return Value(std::move(out));
        }
        case TC16_Int32: {
            std::vector<int32_t> out;
            out.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto v = r.read_i32_be();
                if (!v)
                    return std::unexpected(v.error());
                out.push_back(*v);
            }
            return Value(std::move(out));
        }
        case TC16_Int64: {
            std::vector<int64_t> out;
            out.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto v = r.read_i64_be();
                if (!v)
                    return std::unexpected(v.error());
                out.push_back(*v);
            }
            return Value(std::move(out));
        }
        case TC16_Float: {
            std::vector<float> out;
            out.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto v = r.read_f32_be();
                if (!v)
                    return std::unexpected(v.error());
                out.push_back(*v);
            }
            return Value(std::move(out));
        }
        case TC16_Double: {
            std::vector<double> out;
            out.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto v = r.read_f64_be();
                if (!v)
                    return std::unexpected(v.error());
                out.push_back(*v);
            }
            return Value(std::move(out));
        }
        case TC16_String: {
            std::vector<std::string> out;
            out.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                auto s = read_string_u16(r);
                if (!s)
                    return std::unexpected(s.error());
                out.push_back(std::move(*s));
            }
            return Value(std::move(out));
        }
        default:
            return std::unexpected(Error{.code = Error::Code::UnsupportedTypeCode, .message = "unsupported generic array element type"});
        }
    }

    case TC16_Dictionary: {
        // Dictionary header: keyType + valueType
        auto kt = r.read_u8();
        if (!kt)
            return std::unexpected(kt.error());
        auto vt = r.read_u8();
        if (!vt)
            return std::unexpected(vt.error());

        auto count = r.read_u16_be();
        if (!count)
            return std::unexpected(count.error());

        // This implementation only supports Dictionary<byte, object>
        if (*kt != TC16_Byte)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "only dictionary<byte,*> supported"});
        (void)vt; // ignored; values are decoded as typed objects

        Dictionary out;
        out.reserve(*count);
        for (uint32_t i = 0; i < *count; ++i) {
            auto key = r.read_u8();
            if (!key)
                return std::unexpected(key.error());
            auto val = DecodeValue(r, depth + 1);
            if (!val)
                return std::unexpected(val.error());
            out[*key] = std::move(*val);
        }
        return Value(std::move(out));
    }

    case TC16_Hashtable: {
        auto count = r.read_u16_be();
        if (!count)
            return std::unexpected(count.error());

        auto ht = std::make_shared<Hashtable>();
        for (uint32_t i = 0; i < *count; ++i) {
            auto k = DecodeValue(r, depth + 1);
            if (!k)
                return std::unexpected(k.error());
            auto v = DecodeValue(r, depth + 1);
            if (!v)
                return std::unexpected(v.error());
            ht->emplace(std::move(*k), std::move(*v));
        }
        return Value(ht);
    }

    case TC16_Custom: {
        auto code = r.read_u8();
        if (!code)
            return std::unexpected(code.error());

        auto size = r.read_u16_be();
        if (!size)
            return std::unexpected(size.error());

        auto bytes = r.read_span(static_cast<std::size_t>(*size));
        if (!bytes)
            return std::unexpected(bytes.error());

        RawCustomValue cv{.custom_code = *code, .data = ByteArray(bytes->begin(), bytes->end())};
        return Value(std::move(cv));
    }

    default:
        return std::unexpected(Error{.code = Error::Code::UnsupportedTypeCode, .message = "unknown type code"});
    }
}

// -------------------- Serialize --------------------

std::expected<ByteArray, Error> GpBinaryV16::Serialize(const Message& message) {
    Kind kind{};
    ByteWriter payload;

    auto enc = std::visit(
        [&](const auto& m) -> std::expected<void, Error> {
            using T = std::decay_t<decltype(m)>;

            if constexpr (std::is_same_v<T, InitMessage>) {
                kind = Kind::Init;

                // fixed 39 bytes payload (matches existing V18 layout used by this library)
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
                payload.write_i16_be(m.return_code);

                if (m.debug_message.has_value()) {
                    payload.write_u8(TC16_String);
                    auto ws = write_string_u16(payload, *m.debug_message);
                    if (!ws)
                        return std::unexpected(ws.error());
                } else {
                    payload.write_u8(TC16_Null);
                }

                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, EventMessage>) {
                kind = Kind::Event;
                payload.write_u8(m.event_code);
                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, DisconnectMessage>) {
                kind = Kind::DisconnectMessage;
                payload.write_i16_be(m.code);

                if (m.message.has_value()) {
                    payload.write_u8(TC16_String);
                    auto ws = write_string_u16(payload, *m.message);
                    if (!ws)
                        return std::unexpected(ws.error());
                } else {
                    payload.write_u8(TC16_Null);
                }

                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, InternalOperationRequestMessage>) {
                kind = Kind::InternalOperationRequest;
                payload.write_u8(m.operation_code);
                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, InternalOperationResponseMessage>) {
                kind = Kind::InternalOperationResponse;
                payload.write_u8(m.operation_code);
                payload.write_i16_be(m.return_code);

                if (m.debug_message.has_value()) {
                    payload.write_u8(TC16_String);
                    auto ws = write_string_u16(payload, *m.debug_message);
                    if (!ws)
                        return std::unexpected(ws.error());
                } else {
                    payload.write_u8(TC16_Null);
                }

                return encode_parameters(payload, m.parameters, 0);
            } else if constexpr (std::is_same_v<T, GenericValueMessage>) {
                kind = Kind::Message;
                return EncodeValue(payload, m.value, 0);
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

    // Outer packet wrapper matches the library's GP framing:
    // [magic=0xF3][kind|encflag][payload...]
    ByteWriter packet;
    packet.write_u8(GP_MAGIC);

    uint8_t b1 = static_cast<uint8_t>(kind) & 0x7F;
    // Set the high bit if encrypted
    if (allow_encrypt)
        b1 |= 0x80;
    packet.write_u8(b1);

    packet.write_bytes(*final_payload);
    return packet.take();
}

// -------------------- Deserialize --------------------

std::expected<Message, Error> GpBinaryV16::Deserialize(std::span<const uint8_t> packet_bytes) {
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

        if (payload.size() != 39)
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
        if (payload.size() > 1 || (payload.size() == 1 && payload.front() != '\0'))
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

        auto rc = r.read_i16_be();
        if (!rc)
            return std::unexpected(rc.error());

        auto debug_type = r.read_u8();
        if (!debug_type)
            return std::unexpected(debug_type.error());

        std::optional<std::string> debug_message;
        if (*debug_type == TC16_String) {
            auto s = read_string_u16(r);
            if (!s)
                return std::unexpected(s.error());
            debug_message = std::move(*s);
        } else if (*debug_type == TC16_Null || *debug_type == TC16_Unknown) {
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
        auto code = r.read_i16_be();
        if (!code)
            return std::unexpected(code.error());

        auto msg_type = r.read_u8();
        if (!msg_type)
            return std::unexpected(msg_type.error());

        std::optional<std::string> msg;
        if (*msg_type == TC16_String) {
            auto s = read_string_u16(r);
            if (!s)
                return std::unexpected(s.error());
            msg = std::move(*s);
        } else if (*msg_type == TC16_Null || *msg_type == TC16_Unknown) {
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
        auto v = DecodeValue(r, 0);
        if (!v)
            return std::unexpected(v.error());
        if (r.remaining() != 0)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "message extra bytes"});
        return Message(GenericValueMessage{.value = std::move(*v)}, encrypted);
    }

    case Kind::RawMessage: {
        return Message(RawMessage{.bytes = ByteArray(payload.begin(), payload.end())}, encrypted);
    }

    default:
        return std::unexpected(Error{.code = Error::Code::UnsupportedKind, .message = "unsupported message kind"});
    }
}
} // namespace luxon::ser
