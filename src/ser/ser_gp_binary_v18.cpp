// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "ser_gp_binary_v18.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <cmath>

namespace luxon::ser {
namespace {
enum TypeCode : uint8_t {
    TC18_Unknown = 0x00,
    TC18_Boolean = 0x02,
    TC18_Byte = 0x03,
    TC18_Short = 0x04,
    TC18_Float = 0x05,
    TC18_Double = 0x06,
    TC18_String = 0x07,
    TC18_Null = 0x08,
    TC18_CompressedInt = 0x09,
    TC18_CompressedLong = 0x0A,
    TC18_Int1 = 0x0B,
    TC18_Int1_ = 0x0C,
    TC18_Int2 = 0x0D,
    TC18_Int2_ = 0x0E,
    TC18_L1 = 0x0F,
    TC18_L1_ = 0x10,
    TC18_L2 = 0x11,
    TC18_L2_ = 0x12,
    TC18_Custom = 0x13,
    TC18_Dictionary = 0x14,
    TC18_Hashtable = 0x15,
    TC18_ObjectArray = 0x17,
    TC18_OperationRequest = 0x18,
    TC18_OperationResponse = 0x19,
    TC18_EventData = 0x1A,
    TC18_BooleanFalse = 0x1B,
    TC18_BooleanTrue = 0x1C,
    TC18_ShortZero = 0x1D,
    TC18_IntZero = 0x1E,
    TC18_LongZero = 0x1F,
    TC18_FloatZero = 0x20,
    TC18_DoubleZero = 0x21,
    TC18_ByteZero = 0x22,
    TC18_Array = 0x40,
    TC18_BooleanArray = 0x42,
    TC18_ByteArray = 0x43,
    TC18_ShortArray = 0x44,
    TC18_FloatArray = 0x45,
    TC18_DoubleArray = 0x46,
    TC18_StringArray = 0x47,
    TC18_CompressedIntArray = 0x49,
    TC18_CompressedLongArray = 0x4A,
    TC18_CustomTypeArray = 0x53,
    TC18_DictionaryArray = 0x54,
    TC18_HashtableArray = 0x55,
};

inline std::unexpected<Error> err(Error::Code code, std::string message) { return std::unexpected(Error{.code = code, .message = std::move(message)}); }

#define LUXON_TRY(expr)                                                                                                                                        \
    do {                                                                                                                                                       \
        auto _exp = (expr);                                                                                                                                    \
        if (!_exp)                                                                                                                                             \
            return std::unexpected(std::move(_exp).error());                                                                                                   \
    } while (false)

#define LUXON_TRY_ASSIGN(name, expr)                                                                                                                           \
    auto _exp_##name = (expr);                                                                                                                                 \
    if (!_exp_##name)                                                                                                                                          \
        return std::unexpected(std::move(_exp_##name).error());                                                                                                \
    auto name = std::move(*_exp_##name)

inline void write_byte_array(ByteWriter& w, const ByteArray& bytes) { w.write_bytes(std::span<const uint8_t>(bytes.data(), bytes.size())); }

inline std::expected<void, Error> write_string_payload(ByteWriter& w, const std::string& s) {
    if (s.size() > 32767)
        return err(Error::Code::InvalidValue, "string UTF-8 length exceeds 32767");

    w.write_varuint32(static_cast<uint32_t>(s.size()));
    w.write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
    return {};
}

inline std::expected<std::string, Error> read_string_payload(ByteReader& r) {
    LUXON_TRY_ASSIGN(len, r.read_varuint32());
    if (len > 32767)
        return err(Error::Code::InvalidValue, "string UTF-8 length exceeds 32767");
    LUXON_TRY_ASSIGN(bytes, r.read_span(static_cast<std::size_t>(len)));
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

inline void write_int32_payload(ByteWriter& w, int32_t v) { w.write_varuint32(static_cast<uint32_t>(zigzag_encode_32(v))); }

inline std::expected<int32_t, Error> read_int32_payload(ByteReader& r) {
    LUXON_TRY_ASSIGN(u, r.read_varuint32());
    return zigzag_decode_32(u);
}

inline void write_int64_payload(ByteWriter& w, int64_t v) { w.write_varuint64(zigzag_encode_64(v)); }

inline std::expected<int64_t, Error> read_int64_payload(ByteReader& r) {
    LUXON_TRY_ASSIGN(u, r.read_varuint64());
    return zigzag_decode_64(u);
}

inline bool is_supported_dict_key_code_for_encode(uint8_t code) {
    switch (code) {
    case TC18_Boolean:
    case TC18_Byte:
    case TC18_Short:
    case TC18_Float:
    case TC18_Double:
    case TC18_String:
    case TC18_CompressedInt:
    case TC18_CompressedLong:
        return true;
    default:
        return false;
    }
}

inline bool is_supported_dict_key_code_for_decode(uint8_t code) {
    switch (code) {
    case TC18_Byte:
    case TC18_Short:
    case TC18_Float:
    case TC18_Double:
    case TC18_String:
    case TC18_CompressedInt:
    case TC18_CompressedLong:
        return true;
    default:
        return false;
    }
}

inline bool is_supported_nonarray_dict_value_code(uint8_t code) {
    switch (code) {
    case TC18_Boolean:
    case TC18_Byte:
    case TC18_Short:
    case TC18_Float:
    case TC18_Double:
    case TC18_String:
    case TC18_CompressedInt:
    case TC18_CompressedLong:
    case TC18_Hashtable:
        return true;
    default:
        return false;
    }
}

inline bool is_supported_dict_array_base_code(uint8_t code) {
    switch (code) {
    case TC18_BooleanArray:
    case TC18_ByteArray:
    case TC18_ShortArray:
    case TC18_FloatArray:
    case TC18_DoubleArray:
    case TC18_StringArray:
    case TC18_CompressedIntArray:
    case TC18_CompressedLongArray:
    case TC18_ObjectArray:
    case TC18_HashtableArray:
        return true;
    default:
        return false;
    }
}

struct TypeDesc {
    enum class Kind : uint8_t { Object, Primitive, Dictionary, Array };

    Kind kind{Kind::Object};
    uint8_t code{};   // primitive code, or 1D array code when kind == Array
    int array_rank{}; // valid when kind == Array
    std::shared_ptr<TypeDesc> key{};
    std::shared_ptr<TypeDesc> value{};
    ByteArray header_raw{}; // valid when kind == Dictionary; excludes outer 0x14
};

struct DictHeaderDesc {
    TypeDesc key{};
    TypeDesc value{};
    ByteArray raw{};
};

inline DictHeaderDesc byte_object_header_desc() {
    DictHeaderDesc h{};
    h.raw = ByteArray{TC18_Byte, 0x00};

    h.key.kind = TypeDesc::Kind::Primitive;
    h.key.code = TC18_Byte;

    h.value.kind = TypeDesc::Kind::Object;
    return h;
}

inline bool is_byte_object_header(const DictHeaderDesc& h) {
    return h.key.kind == TypeDesc::Kind::Primitive && h.key.code == TC18_Byte && h.value.kind == TypeDesc::Kind::Object;
}

std::expected<TypeDesc, Error> parse_key_desc(ByteReader& r, ByteArray& raw, bool decode_mode);
std::expected<TypeDesc, Error> parse_value_desc(ByteReader& r, ByteArray& raw, bool decode_mode);

std::expected<TypeDesc, Error> parse_key_desc(ByteReader& r, ByteArray& raw, bool decode_mode) {
    LUXON_TRY_ASSIGN(code, r.read_u8());
    raw.push_back(code);

    if (code == 0x00) {
        TypeDesc d{};
        d.kind = TypeDesc::Kind::Object;
        return d;
    }

    const bool ok = decode_mode ? is_supported_dict_key_code_for_decode(code) : is_supported_dict_key_code_for_encode(code);
    if (!ok)
        return err(Error::Code::InvalidValue, "invalid dictionary key type descriptor");

    TypeDesc d{};
    d.kind = TypeDesc::Kind::Primitive;
    d.code = code;
    return d;
}

std::expected<TypeDesc, Error> parse_value_desc(ByteReader& r, ByteArray& raw, bool decode_mode) {
    LUXON_TRY_ASSIGN(code, r.read_u8());
    raw.push_back(code);

    if (code == 0x00) {
        TypeDesc d{};
        d.kind = TypeDesc::Kind::Object;
        return d;
    }

    if (code == TC18_Dictionary) {
        ByteArray nested_raw{};
        LUXON_TRY_ASSIGN(kd, parse_key_desc(r, nested_raw, decode_mode));
        LUXON_TRY_ASSIGN(vd, parse_value_desc(r, nested_raw, decode_mode));

        raw.insert(raw.end(), nested_raw.begin(), nested_raw.end());

        TypeDesc d{};
        d.kind = TypeDesc::Kind::Dictionary;
        d.key = std::make_shared<TypeDesc>(std::move(kd));
        d.value = std::make_shared<TypeDesc>(std::move(vd));
        d.header_raw = std::move(nested_raw);
        return d;
    }

    if (code == TC18_Array) {
        int rank = 2;
        uint8_t final_code = 0;

        for (;;) {
            LUXON_TRY_ASSIGN(next, r.read_u8());
            raw.push_back(next);
            if (next == TC18_Array) {
                ++rank;
                continue;
            }
            final_code = next;
            break;
        }

        if (!is_supported_dict_array_base_code(final_code))
            return err(Error::Code::InvalidValue, "invalid dictionary array type descriptor");

        TypeDesc d{};
        d.kind = TypeDesc::Kind::Array;
        d.code = final_code;
        d.array_rank = rank;
        return d;
    }

    if (is_supported_dict_array_base_code(code)) {
        TypeDesc d{};
        d.kind = TypeDesc::Kind::Array;
        d.code = code;
        d.array_rank = 1;
        return d;
    }

    if (is_supported_nonarray_dict_value_code(code)) {
        TypeDesc d{};
        d.kind = TypeDesc::Kind::Primitive;
        d.code = code;
        return d;
    }

    return err(Error::Code::InvalidValue, "invalid dictionary value type descriptor");
}

std::expected<DictHeaderDesc, Error> read_dict_header(ByteReader& r, bool decode_mode) {
    DictHeaderDesc h{};
    LUXON_TRY_ASSIGN(kd, parse_key_desc(r, h.raw, decode_mode));
    LUXON_TRY_ASSIGN(vd, parse_value_desc(r, h.raw, decode_mode));
    h.key = std::move(kd);
    h.value = std::move(vd);
    return h;
}

std::expected<DictHeaderDesc, Error> parse_dict_header_bytes(std::span<const uint8_t> header, bool decode_mode) {
    ByteReader r(header);
    LUXON_TRY_ASSIGN(h, read_dict_header(r, decode_mode));
    if (r.remaining() != 0)
        return err(Error::Code::InvalidValue, "dictionary header has trailing bytes");
    return h;
}

template <typename EncodeTypedFn> std::expected<void, Error> encode_bool_array_body(ByteWriter& w, const std::vector<bool>& v) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "boolean array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));

    uint8_t packed = 0;
    int bit = 0;
    for (bool b : v) {
        if (b)
            packed |= static_cast<uint8_t>(1u << bit);
        ++bit;
        if (bit == 8) {
            w.write_u8(packed);
            packed = 0;
            bit = 0;
        }
    }
    if (bit != 0)
        w.write_u8(packed);

    return {};
}

inline std::expected<std::vector<bool>, Error> decode_bool_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    std::vector<bool> out;
    out.resize(count);

    const std::size_t packed_bytes = (static_cast<std::size_t>(count) + 7u) / 8u;
    for (std::size_t i = 0, idx = 0; i < packed_bytes; ++i) {
        LUXON_TRY_ASSIGN(b, r.read_u8());
        for (int bit = 0; bit < 8 && idx < out.size(); ++bit, ++idx)
            out[idx] = ((b >> bit) & 1u) != 0;
    }

    return out;
}

inline std::expected<void, Error> encode_byte_array_body(ByteWriter& w, const ByteArray& v) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "byte array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    write_byte_array(w, v);
    return {};
}

inline std::expected<ByteArray, Error> decode_byte_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(len, r.read_varuint32());
    LUXON_TRY_ASSIGN(bytes, r.read_span(static_cast<std::size_t>(len)));
    return ByteArray(bytes.begin(), bytes.end());
}

inline std::expected<void, Error> encode_short_array_body(ByteWriter& w, const std::vector<int16_t>& v) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "short array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    for (int16_t x : v)
        w.write_i16_le(x);
    return {};
}

inline std::expected<std::vector<int16_t>, Error> decode_short_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    std::vector<int16_t> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(v, r.read_i16_le());
        out.push_back(v);
    }
    return out;
}

inline std::expected<void, Error> encode_float_array_body(ByteWriter& w, const std::vector<float>& v) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "float array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    for (float x : v)
        w.write_f32_le(x);
    return {};
}

inline std::expected<std::vector<float>, Error> decode_float_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    std::vector<float> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(v, r.read_f32_le());
        out.push_back(v);
    }
    return out;
}

inline std::expected<void, Error> encode_double_array_body(ByteWriter& w, const std::vector<double>& v) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "double array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    for (double x : v)
        w.write_f64_le(x);
    return {};
}

inline std::expected<std::vector<double>, Error> decode_double_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    std::vector<double> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(v, r.read_f64_le());
        out.push_back(v);
    }
    return out;
}

inline std::expected<void, Error> encode_string_array_body(ByteWriter& w, const std::vector<std::string>& v) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "string array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    for (const auto& s : v)
        LUXON_TRY(write_string_payload(w, s));
    return {};
}

inline std::expected<std::vector<std::string>, Error> decode_string_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    std::vector<std::string> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(s, read_string_payload(r));
        out.push_back(std::move(s));
    }
    return out;
}

inline std::expected<void, Error> encode_int_array_body(ByteWriter& w, const std::vector<int32_t>& v) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "int array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    for (int32_t x : v)
        write_int32_payload(w, x);
    return {};
}

inline std::expected<std::vector<int32_t>, Error> decode_int_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    std::vector<int32_t> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(v, read_int32_payload(r));
        out.push_back(v);
    }
    return out;
}

inline std::expected<void, Error> encode_long_array_body(ByteWriter& w, const std::vector<int64_t>& v) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "long array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    for (int64_t x : v)
        write_int64_payload(w, x);
    return {};
}

inline std::expected<std::vector<int64_t>, Error> decode_long_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    std::vector<int64_t> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(v, read_int64_payload(r));
        out.push_back(v);
    }
    return out;
}

template <typename EncodeTypedFn>
std::expected<void, Error> encode_object_array_body(ByteWriter& w, const ObjectArray& v, int depth, EncodeTypedFn& encode_typed) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "object array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    for (const auto& elem : v)
        LUXON_TRY(encode_typed(w, elem, depth + 1));
    return {};
}

template <typename DecodeTypedFn> std::expected<ObjectArray, Error> decode_object_array_body(ByteReader& r, int depth, DecodeTypedFn& decode_typed) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    ObjectArray out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(v, decode_typed(r, depth + 1));
        out.push_back(std::move(v));
    }
    return out;
}

template <typename EncodeTypedFn>
std::expected<void, Error> encode_jagged_array_body(ByteWriter& w, const JaggedArray& v, int depth, EncodeTypedFn& encode_typed) {
    if (v.elements.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "jagged array too large");

    w.write_varuint32(static_cast<uint32_t>(v.elements.size()));
    for (const auto& elem : v.elements)
        LUXON_TRY(encode_typed(w, elem, depth + 1));
    return {};
}

template <typename DecodeTypedFn> std::expected<JaggedArray, Error> decode_jagged_array_body(ByteReader& r, int depth, DecodeTypedFn& decode_typed) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    JaggedArray out;
    out.elements.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(v, decode_typed(r, depth + 1));
        out.elements.push_back(std::move(v));
    }
    return out;
}

template <typename EncodeTypedFn>
std::expected<void, Error> encode_hashtable_body(ByteWriter& w, const HashtablePtr& h, int depth, EncodeTypedFn& encode_typed) {
    if (!h)
        return err(Error::Code::InvalidValue, "null hashtable pointer");

    if (h->size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "hashtable too large");

    struct Item {
        ByteArray key_bytes;
        const Value *value{};
    };

    std::vector<Item> items;
    items.reserve(h->size());

    for (const auto& [k, v] : *h) {
        ByteWriter kw;
        LUXON_TRY(encode_typed(kw, k, depth + 1));
        items.push_back(Item{kw.take(), &v});
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key_bytes < b.key_bytes; });

    w.write_varuint32(static_cast<uint32_t>(items.size()));
    for (const auto& item : items) {
        write_byte_array(w, item.key_bytes);
        LUXON_TRY(encode_typed(w, *item.value, depth + 1));
    }

    return {};
}

template <typename DecodeTypedFn> std::expected<HashtablePtr, Error> decode_hashtable_body(ByteReader& r, int depth, DecodeTypedFn& decode_typed) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    auto out = std::make_shared<Hashtable>();
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(k, decode_typed(r, depth + 1));
        LUXON_TRY_ASSIGN(v, decode_typed(r, depth + 1));
        if (!k.is_null())
            (*out)[k] = std::move(v);
    }
    return out;
}

template <typename EncodeTypedFn>
std::expected<void, Error> encode_hashtable_array_body(ByteWriter& w, const std::vector<HashtablePtr>& v, int depth, EncodeTypedFn& encode_typed) {
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "hashtable array too large");

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    for (const auto& elem : v)
        LUXON_TRY(encode_hashtable_body(w, elem, depth + 1, encode_typed));
    return {};
}

template <typename DecodeTypedFn>
std::expected<std::vector<HashtablePtr>, Error> decode_hashtable_array_body(ByteReader& r, int depth, DecodeTypedFn& decode_typed) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    std::vector<HashtablePtr> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(v, decode_hashtable_body(r, depth + 1, decode_typed));
        out.push_back(std::move(v));
    }
    return out;
}

inline std::expected<void, Error> encode_custom_typed(ByteWriter& w, const RawCustomValue& v) {
    if (v.data.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "custom payload too large");

    if (v.custom_code < 100) {
        w.write_u8(static_cast<uint8_t>(0x80u + v.custom_code));
    } else {
        w.write_u8(TC18_Custom);
        w.write_u8(v.custom_code);
    }

    w.write_varuint32(static_cast<uint32_t>(v.data.size()));
    write_byte_array(w, v.data);
    return {};
}

inline std::expected<RawCustomValue, Error> decode_custom_typed(ByteReader& r, uint8_t type_code) {
    uint8_t custom_code = 0;
    if (type_code == TC18_Custom) {
        LUXON_TRY_ASSIGN(code, r.read_u8());
        custom_code = code;
    } else {
        custom_code = static_cast<uint8_t>(type_code - 0x80u);
    }

    LUXON_TRY_ASSIGN(len, r.read_varuint32());
    LUXON_TRY_ASSIGN(bytes, r.read_span(static_cast<std::size_t>(len)));
    return RawCustomValue{.custom_code = custom_code, .data = ByteArray(bytes.begin(), bytes.end())};
}

inline std::expected<void, Error> encode_custom_array_body(ByteWriter& w, const std::vector<RawCustomValue>& v) {
    if (v.empty())
        return err(Error::Code::InvalidValue, "cannot encode empty custom type array without custom code");
    if (v.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "custom type array too large");

    const uint8_t code = v.front().custom_code;
    for (const auto& elem : v) {
        if (elem.custom_code != code)
            return err(Error::Code::InvalidValue, "custom type array contains mixed custom codes");
        if (elem.data.size() > std::numeric_limits<uint32_t>::max())
            return err(Error::Code::InvalidValue, "custom type array element too large");
    }

    w.write_varuint32(static_cast<uint32_t>(v.size()));
    w.write_u8(code);
    for (const auto& elem : v) {
        w.write_varuint32(static_cast<uint32_t>(elem.data.size()));
        write_byte_array(w, elem.data);
    }
    return {};
}

inline std::expected<std::vector<RawCustomValue>, Error> decode_custom_array_body(ByteReader& r) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());
    LUXON_TRY_ASSIGN(code, r.read_u8());

    std::vector<RawCustomValue> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(len, r.read_varuint32());
        LUXON_TRY_ASSIGN(bytes, r.read_span(static_cast<std::size_t>(len)));
        out.push_back(RawCustomValue{.custom_code = code, .data = ByteArray(bytes.begin(), bytes.end())});
    }
    return out;
}

template <typename EncodeTypedFn>
std::expected<void, Error> encode_payload_by_desc(ByteWriter& w, const Value& v, const TypeDesc& desc, int depth, EncodeTypedFn& encode_typed);

template <typename DecodeTypedFn>
std::expected<Value, Error> decode_payload_by_desc(ByteReader& r, const TypeDesc& desc, int depth, DecodeTypedFn& decode_typed);

template <typename EncodeTypedFn>
std::expected<void, Error> encode_dictionary_body_from_header(ByteWriter& w, const DictHeaderDesc& header, const GenericDictionary& gd, int depth,
                                                              EncodeTypedFn& encode_typed) {
    if (gd.entries.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "dictionary too large");

    if (!gd.header.empty() && gd.header != header.raw)
        return err(Error::Code::InvalidValue, "generic dictionary header mismatch");

    w.write_varuint32(static_cast<uint32_t>(gd.entries.size()));
    for (const auto& [k, v] : gd.entries) {
        LUXON_TRY(encode_payload_by_desc(w, k, header.key, depth + 1, encode_typed));
        LUXON_TRY(encode_payload_by_desc(w, v, header.value, depth + 1, encode_typed));
    }
    return {};
}

template <typename EncodeTypedFn>
std::expected<void, Error> encode_dictionary_body_from_header(ByteWriter& w, const DictHeaderDesc& header, const Dictionary& d, int depth,
                                                              EncodeTypedFn& encode_typed) {
    if (!is_byte_object_header(header))
        return err(Error::Code::InvalidValue, "convenience Dictionary only supports Dictionary<byte,object>");
    if (d.size() > std::numeric_limits<uint32_t>::max())
        return err(Error::Code::InvalidValue, "dictionary too large");

    std::vector<std::pair<uint8_t, const Value *>> items;
    items.reserve(d.size());
    for (const auto& [k, v] : d)
        items.emplace_back(k, &v);
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    w.write_varuint32(static_cast<uint32_t>(items.size()));
    for (const auto& [k, v] : items) {
        Value key_value{k};
        LUXON_TRY(encode_payload_by_desc(w, key_value, header.key, depth + 1, encode_typed));
        LUXON_TRY(encode_payload_by_desc(w, *v, header.value, depth + 1, encode_typed));
    }
    return {};
}

template <typename DecodeTypedFn>
std::expected<Value, Error> decode_dictionary_body_from_header(ByteReader& r, const DictHeaderDesc& header, int depth, DecodeTypedFn& decode_typed) {
    LUXON_TRY_ASSIGN(count, r.read_varuint32());

    if (is_byte_object_header(header)) {
        Dictionary d{};
        d.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            LUXON_TRY_ASSIGN(kv, decode_payload_by_desc(r, header.key, depth + 1, decode_typed));
            const auto *key_ptr = kv.template get_ptr<uint8_t>();
            if (!key_ptr)
                return err(Error::Code::InvalidValue, "Dictionary<byte,object> key did not decode as byte");

            LUXON_TRY_ASSIGN(vv, decode_payload_by_desc(r, header.value, depth + 1, decode_typed));
            d[*key_ptr] = std::move(vv);
        }
        return Value(std::move(d));
    }

    GenericDictionary gd{};
    gd.header = header.raw;
    gd.entries.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(k, decode_payload_by_desc(r, header.key, depth + 1, decode_typed));
        LUXON_TRY_ASSIGN(v, decode_payload_by_desc(r, header.value, depth + 1, decode_typed));
        gd.entries.emplace_back(std::move(k), std::move(v));
    }

    return Value(std::move(gd));
}

template <typename EncodeTypedFn>
std::expected<void, Error> encode_payload_by_desc(ByteWriter& w, const Value& v, const TypeDesc& desc, int depth, EncodeTypedFn& encode_typed) {
    switch (desc.kind) {
    case TypeDesc::Kind::Object:
        return encode_typed(w, v, depth + 1);

    case TypeDesc::Kind::Primitive:
        switch (desc.code) {
        case TC18_Boolean: {
            const auto *p = v.get_ptr<bool>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected bool payload");
            w.write_u8(*p ? 1 : 0);
            return {};
        }
        case TC18_Byte: {
            const auto *p = v.get_ptr<uint8_t>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected byte payload");
            w.write_u8(*p);
            return {};
        }
        case TC18_Short: {
            const auto *p = v.get_ptr<int16_t>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected short payload");
            w.write_i16_le(*p);
            return {};
        }
        case TC18_Float: {
            const auto *p = v.get_ptr<float>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected float payload");
            w.write_f32_le(*p);
            return {};
        }
        case TC18_Double: {
            const auto *p = v.get_ptr<double>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected double payload");
            w.write_f64_le(*p);
            return {};
        }
        case TC18_String: {
            const auto *p = v.get_ptr<std::string>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected string payload");
            return write_string_payload(w, *p);
        }
        case TC18_CompressedInt: {
            const auto *p = v.get_ptr<int32_t>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected int payload");
            write_int32_payload(w, *p);
            return {};
        }
        case TC18_CompressedLong: {
            const auto *p = v.get_ptr<int64_t>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected long payload");
            write_int64_payload(w, *p);
            return {};
        }
        case TC18_Hashtable: {
            const auto *p = v.get_ptr<HashtablePtr>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected hashtable payload");
            return encode_hashtable_body(w, *p, depth + 1, encode_typed);
        }
        default:
            return err(Error::Code::UnsupportedTypeCode, "unsupported primitive payload descriptor");
        }

    case TypeDesc::Kind::Dictionary:
        if (const auto *p = v.get_ptr<Dictionary>())
            return encode_dictionary_body_from_header(w, DictHeaderDesc{.key = *desc.key, .value = *desc.value, .raw = desc.header_raw}, *p, depth + 1,
                                                      encode_typed);
        if (const auto *p = v.get_ptr<GenericDictionary>())
            return encode_dictionary_body_from_header(w, DictHeaderDesc{.key = *desc.key, .value = *desc.value, .raw = desc.header_raw}, *p, depth + 1,
                                                      encode_typed);
        return err(Error::Code::InvalidValue, "expected dictionary payload");

    case TypeDesc::Kind::Array:
        if (desc.array_rank > 1) {
            const auto *p = v.get_ptr<JaggedArray>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected jagged array payload");
            return encode_jagged_array_body(w, *p, depth + 1, encode_typed);
        }

        switch (desc.code) {
        case TC18_BooleanArray: {
            const auto *p = v.get_ptr<std::vector<bool>>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected bool[] payload");
            return encode_bool_array_body<EncodeTypedFn>(w, *p);
        }
        case TC18_ByteArray: {
            const auto *p = v.get_ptr<ByteArray>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected byte[] payload");
            return encode_byte_array_body(w, *p);
        }
        case TC18_ShortArray: {
            const auto *p = v.get_ptr<std::vector<int16_t>>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected short[] payload");
            return encode_short_array_body(w, *p);
        }
        case TC18_FloatArray: {
            const auto *p = v.get_ptr<std::vector<float>>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected float[] payload");
            return encode_float_array_body(w, *p);
        }
        case TC18_DoubleArray: {
            const auto *p = v.get_ptr<std::vector<double>>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected double[] payload");
            return encode_double_array_body(w, *p);
        }
        case TC18_StringArray: {
            const auto *p = v.get_ptr<std::vector<std::string>>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected string[] payload");
            return encode_string_array_body(w, *p);
        }
        case TC18_CompressedIntArray: {
            const auto *p = v.get_ptr<std::vector<int32_t>>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected int[] payload");
            return encode_int_array_body(w, *p);
        }
        case TC18_CompressedLongArray: {
            const auto *p = v.get_ptr<std::vector<int64_t>>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected long[] payload");
            return encode_long_array_body(w, *p);
        }
        case TC18_ObjectArray: {
            const auto *p = v.get_ptr<ObjectArray>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected object[] payload");
            return encode_object_array_body(w, *p, depth + 1, encode_typed);
        }
        case TC18_HashtableArray: {
            const auto *p = v.get_ptr<std::vector<HashtablePtr>>();
            if (!p)
                return err(Error::Code::InvalidValue, "expected Hashtable[] payload");
            return encode_hashtable_array_body(w, *p, depth + 1, encode_typed);
        }
        default:
            return err(Error::Code::UnsupportedTypeCode, "unsupported array payload descriptor");
        }
    }

    return err(Error::Code::UnsupportedTypeCode, "unsupported payload descriptor");
}

template <typename DecodeTypedFn>
std::expected<Value, Error> decode_payload_by_desc(ByteReader& r, const TypeDesc& desc, int depth, DecodeTypedFn& decode_typed) {
    switch (desc.kind) {
    case TypeDesc::Kind::Object:
        return decode_typed(r, depth + 1);

    case TypeDesc::Kind::Primitive:
        switch (desc.code) {
        case TC18_Boolean: {
            LUXON_TRY_ASSIGN(b, r.read_u8());
            return Value(static_cast<bool>(b != 0));
        }
        case TC18_Byte: {
            LUXON_TRY_ASSIGN(v, r.read_u8());
            return Value(v);
        }
        case TC18_Short: {
            LUXON_TRY_ASSIGN(v, r.read_i16_le());
            return Value(v);
        }
        case TC18_Float: {
            LUXON_TRY_ASSIGN(v, r.read_f32_le());
            return Value(v);
        }
        case TC18_Double: {
            LUXON_TRY_ASSIGN(v, r.read_f64_le());
            return Value(v);
        }
        case TC18_String: {
            LUXON_TRY_ASSIGN(v, read_string_payload(r));
            return Value(std::move(v));
        }
        case TC18_CompressedInt: {
            LUXON_TRY_ASSIGN(v, read_int32_payload(r));
            return Value(v);
        }
        case TC18_CompressedLong: {
            LUXON_TRY_ASSIGN(v, read_int64_payload(r));
            return Value(v);
        }
        case TC18_Hashtable: {
            LUXON_TRY_ASSIGN(v, decode_hashtable_body(r, depth + 1, decode_typed));
            return Value(std::move(v));
        }
        default:
            return err(Error::Code::UnsupportedTypeCode, "unsupported primitive payload descriptor");
        }

    case TypeDesc::Kind::Dictionary: {
        LUXON_TRY_ASSIGN(
            v, decode_dictionary_body_from_header(r, DictHeaderDesc{.key = *desc.key, .value = *desc.value, .raw = desc.header_raw}, depth + 1, decode_typed));
        return v;
    }

    case TypeDesc::Kind::Array:
        if (desc.array_rank > 1) {
            LUXON_TRY_ASSIGN(v, decode_jagged_array_body(r, depth + 1, decode_typed));
            return Value(std::move(v));
        }

        switch (desc.code) {
        case TC18_BooleanArray: {
            LUXON_TRY_ASSIGN(v, decode_bool_array_body(r));
            return Value(std::move(v));
        }
        case TC18_ByteArray: {
            LUXON_TRY_ASSIGN(v, decode_byte_array_body(r));
            return Value(std::move(v));
        }
        case TC18_ShortArray: {
            LUXON_TRY_ASSIGN(v, decode_short_array_body(r));
            return Value(std::move(v));
        }
        case TC18_FloatArray: {
            LUXON_TRY_ASSIGN(v, decode_float_array_body(r));
            return Value(std::move(v));
        }
        case TC18_DoubleArray: {
            LUXON_TRY_ASSIGN(v, decode_double_array_body(r));
            return Value(std::move(v));
        }
        case TC18_StringArray: {
            LUXON_TRY_ASSIGN(v, decode_string_array_body(r));
            return Value(std::move(v));
        }
        case TC18_CompressedIntArray: {
            LUXON_TRY_ASSIGN(v, decode_int_array_body(r));
            return Value(std::move(v));
        }
        case TC18_CompressedLongArray: {
            LUXON_TRY_ASSIGN(v, decode_long_array_body(r));
            return Value(std::move(v));
        }
        case TC18_ObjectArray: {
            LUXON_TRY_ASSIGN(v, decode_object_array_body(r, depth + 1, decode_typed));
            return Value(std::move(v));
        }
        case TC18_HashtableArray: {
            LUXON_TRY_ASSIGN(v, decode_hashtable_array_body(r, depth + 1, decode_typed));
            return Value(std::move(v));
        }
        default:
            return err(Error::Code::UnsupportedTypeCode, "unsupported array payload descriptor");
        }
    }

    return err(Error::Code::UnsupportedTypeCode, "unsupported payload descriptor");
}

inline std::expected<std::optional<std::string>, Error> decode_typed_string_or_null(ByteReader& r) {
    LUXON_TRY_ASSIGN(tc, r.read_u8());
    switch (tc) {
    case TC18_Null:
        return std::optional<std::string>{};
    case TC18_String: {
        LUXON_TRY_ASSIGN(s, read_string_payload(r));
        return std::optional<std::string>{std::move(s)};
    }
    default:
        return err(Error::Code::InvalidValue, "expected typed string-or-null");
    }
}

inline std::expected<void, Error> encode_typed_string_or_null(ByteWriter& w, const std::optional<std::string>& s) {
    if (!s) {
        w.write_u8(TC18_Null);
        return {};
    }
    w.write_u8(TC18_String);
    return write_string_payload(w, *s);
}

} // namespace

std::expected<void, Error> GpBinaryV18::encode_parameters(ByteWriter& w, const ParameterList& params, int depth) const {
    if (params.size() > 255)
        return err(Error::Code::InvalidValue, "parameter count exceeds 255");

    w.write_u8(static_cast<uint8_t>(params.size()));
    for (const auto& [k, v] : params) {
        w.write_u8(k);
        LUXON_TRY(EncodeValue(w, v, depth + 1));
    }

    return {};
}

std::expected<ParameterList, Error> GpBinaryV18::decode_parameters(ByteReader& r, int depth) const {
    LUXON_TRY_ASSIGN(count, r.read_u8());

    ParameterList params{};
    params.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        LUXON_TRY_ASSIGN(k, r.read_u8());
        LUXON_TRY_ASSIGN(v, DecodeValue(r, depth + 1));
        params[k] = std::move(v);
    }
    return params;
}

std::expected<void, Error> GpBinaryV18::EncodeValue(ByteWriter& w, const Value& v, int depth) const {
    if (depth > MAX_DEPTH)
        return err(Error::Code::DepthLimit, "GpBinaryV18 encode depth limit exceeded");

    auto encode_typed = [this](ByteWriter& out, const Value& value, int d) { return EncodeValue(out, value, d); };

    if (v.is_null()) {
        w.write_u8(TC18_Null);
        return {};
    }

    return v.value.visit([&]<typename T>(const T& val) -> std::expected<void, Error> {
        if constexpr (std::is_same_v<T, bool>) {
            w.write_u8(val ? TC18_BooleanTrue : TC18_BooleanFalse);
            return {};
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            if (val == 0) {
                w.write_u8(TC18_ByteZero);
            } else {
                w.write_u8(TC18_Byte);
                w.write_u8(val);
            }
            return {};
        } else if constexpr (std::is_same_v<T, int16_t>) {
            if (val == 0) {
                w.write_u8(TC18_ShortZero);
            } else {
                w.write_u8(TC18_Short);
                w.write_i16_le(val);
            }
            return {};
        } else if constexpr (std::is_same_v<T, int32_t>) {
            const int32_t x = val;
            if (x == 0) {
                w.write_u8(TC18_IntZero);
            } else if (x > 0 && x <= 255) {
                w.write_u8(TC18_Int1);
                w.write_u8(static_cast<uint8_t>(x));
            } else if (x > 255 && x <= 65535) {
                w.write_u8(TC18_Int2);
                w.write_u16_le(static_cast<uint16_t>(x));
            } else if (x < 0 && x >= -255) {
                w.write_u8(TC18_Int1_);
                w.write_u8(static_cast<uint8_t>(-x));
            } else if (x < -255 && x >= -65535) {
                w.write_u8(TC18_Int2_);
                w.write_u16_le(static_cast<uint16_t>(-x));
            } else {
                w.write_u8(TC18_CompressedInt);
                write_int32_payload(w, x);
            }
            return {};
        } else if constexpr (std::is_same_v<T, int64_t>) {
            const int64_t x = val;
            if (x == 0) {
                w.write_u8(TC18_LongZero);
            } else if (x > 0 && x <= 255) {
                w.write_u8(TC18_L1);
                w.write_u8(static_cast<uint8_t>(x));
            } else if (x > 255 && x <= 65535) {
                w.write_u8(TC18_L2);
                w.write_u16_le(static_cast<uint16_t>(x));
            } else if (x < 0 && x >= -255) {
                w.write_u8(TC18_L1_);
                w.write_u8(static_cast<uint8_t>(-x));
            } else if (x < -255 && x >= -65535) {
                w.write_u8(TC18_L2_);
                w.write_u16_le(static_cast<uint16_t>(-x));
            } else {
                w.write_u8(TC18_CompressedLong);
                write_int64_payload(w, x);
            }
            return {};
        } else if constexpr (std::is_same_v<T, float>) {
            if (val == 0.0f && !std::signbit(val)) {
                w.write_u8(TC18_FloatZero);
            } else {
                w.write_u8(TC18_Float);
                w.write_f32_le(val);
            }
            return {};
        } else if constexpr (std::is_same_v<T, double>) {
            if (val == 0.0 && !std::signbit(val)) {
                w.write_u8(TC18_DoubleZero);
            } else {
                w.write_u8(TC18_Double);
                w.write_f64_le(val);
            }
            return {};
        } else if constexpr (std::is_same_v<T, std::string>) {
            w.write_u8(TC18_String);
            return write_string_payload(w, val);
        } else if constexpr (std::is_same_v<T, ByteArray>) {
            w.write_u8(TC18_ByteArray);
            return encode_byte_array_body(w, val);
        } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
            w.write_u8(TC18_BooleanArray);
            return encode_bool_array_body<decltype(encode_typed)>(w, val);
        } else if constexpr (std::is_same_v<T, std::vector<int16_t>>) {
            w.write_u8(TC18_ShortArray);
            return encode_short_array_body(w, val);
        } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
            w.write_u8(TC18_CompressedIntArray);
            return encode_int_array_body(w, val);
        } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
            w.write_u8(TC18_CompressedLongArray);
            return encode_long_array_body(w, val);
        } else if constexpr (std::is_same_v<T, std::vector<float>>) {
            w.write_u8(TC18_FloatArray);
            return encode_float_array_body(w, val);
        } else if constexpr (std::is_same_v<T, std::vector<double>>) {
            w.write_u8(TC18_DoubleArray);
            return encode_double_array_body(w, val);
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            w.write_u8(TC18_StringArray);
            return encode_string_array_body(w, val);
        } else if constexpr (std::is_same_v<T, ObjectArray>) {
            w.write_u8(TC18_ObjectArray);
            return encode_object_array_body(w, val, depth, encode_typed);
        } else if constexpr (std::is_same_v<T, JaggedArray>) {
            // quirk: always writes type code 0x40
            w.write_u8(TC18_Array);
            return encode_jagged_array_body(w, val, depth, encode_typed);
        } else if constexpr (std::is_same_v<T, Dictionary>) {
            const auto hdr = byte_object_header_desc();
            w.write_u8(TC18_Dictionary);
            write_byte_array(w, hdr.raw);
            return encode_dictionary_body_from_header(w, hdr, val, depth, encode_typed);
        } else if constexpr (std::is_same_v<T, GenericDictionary>) {
            if (val.header.empty())
                return err(Error::Code::InvalidValue, "generic dictionary missing header");

            LUXON_TRY_ASSIGN(hdr, parse_dict_header_bytes(std::span<const uint8_t>(val.header.data(), val.header.size()), false));
            w.write_u8(TC18_Dictionary);
            write_byte_array(w, val.header);
            return encode_dictionary_body_from_header(w, hdr, val, depth, encode_typed);
        } else if constexpr (std::is_same_v<T, HashtablePtr>) {
            w.write_u8(TC18_Hashtable);
            return encode_hashtable_body(w, val, depth, encode_typed);
        } else if constexpr (std::is_same_v<T, RawCustomValue>) {
            return encode_custom_typed(w, val);
        } else if constexpr (std::is_same_v<T, EventMessage>) {
            w.write_u8(TC18_EventData);
            w.write_u8(val.event_code);
            return encode_parameters(w, val.parameters, depth);
        } else if constexpr (std::is_same_v<T, OperationRequestMessage>) {
            w.write_u8(TC18_OperationRequest);
            w.write_u8(val.operation_code);
            return encode_parameters(w, val.parameters, depth);
        } else if constexpr (std::is_same_v<T, OperationResponseMessage>) {
            w.write_u8(TC18_OperationResponse);
            w.write_u8(val.operation_code);
            w.write_i16_le(val.return_code);
            if (!val.debug_message || val.debug_message->empty()) {
                w.write_u8(TC18_Null);
            } else {
                w.write_u8(TC18_String);
                LUXON_TRY(write_string_payload(w, *val.debug_message));
            }
            return encode_parameters(w, val.parameters, depth);
        } else if constexpr (std::is_same_v<T, std::vector<Dictionary>>) {
            // quirk: always writes type code 0x54
            const auto hdr = byte_object_header_desc();
            w.write_u8(TC18_DictionaryArray);
            write_byte_array(w, hdr.raw);

            if (val.size() > std::numeric_limits<uint32_t>::max())
                return err(Error::Code::InvalidValue, "dictionary array too large");

            w.write_varuint32(static_cast<uint32_t>(val.size()));
            for (const auto& elem : val)
                LUXON_TRY(encode_dictionary_body_from_header(w, hdr, elem, depth, encode_typed));
            return {};
        } else if constexpr (std::is_same_v<T, std::vector<GenericDictionary>>) {
            if (val.empty())
                return err(Error::Code::InvalidValue, "cannot encode empty exact dictionary array without header");

            const auto& first = val.front();
            if (first.header.empty())
                return err(Error::Code::InvalidValue, "generic dictionary array element missing header");

            LUXON_TRY_ASSIGN(hdr, parse_dict_header_bytes(std::span<const uint8_t>(first.header.data(), first.header.size()), false));

            for (const auto& elem : val) {
                if (elem.header != first.header)
                    return err(Error::Code::InvalidValue, "dictionary array elements have mismatched headers");
            }

            w.write_u8(TC18_DictionaryArray);
            write_byte_array(w, first.header);

            if (val.size() > std::numeric_limits<uint32_t>::max())
                return err(Error::Code::InvalidValue, "dictionary array too large");

            w.write_varuint32(static_cast<uint32_t>(val.size()));
            for (const auto& elem : val)
                LUXON_TRY(encode_dictionary_body_from_header(w, hdr, elem, depth, encode_typed));
            return {};
        } else if constexpr (std::is_same_v<T, std::vector<HashtablePtr>>) {
            w.write_u8(TC18_HashtableArray);
            return encode_hashtable_array_body(w, val, depth, encode_typed);
        } else if constexpr (std::is_same_v<T, std::vector<RawCustomValue>>) {
            w.write_u8(TC18_CustomTypeArray);
            return encode_custom_array_body(w, val);
        } else if constexpr (std::is_same_v<T, PreSerializedValue>) {
            write_byte_array(w, val.data);
            return {};
        } else {
            return err(Error::Code::InvalidValue, "unsupported Value alternative for GpBinaryV18");
        }
    });
}

std::expected<Value, Error> GpBinaryV18::DecodeValue(ByteReader& r, int depth) const {
    if (depth > MAX_DEPTH)
        return err(Error::Code::DepthLimit, "GpBinaryV18 decode depth limit exceeded");

    auto decode_typed = [this](ByteReader& in, int d) { return DecodeValue(in, d); };

    LUXON_TRY_ASSIGN(tc, r.read_u8());

    if (tc >= 0x80 && tc <= 0xE4) {
        LUXON_TRY_ASSIGN(v, decode_custom_typed(r, tc));
        return Value(std::move(v));
    }

    switch (tc) {
    case TC18_Null:
        return Value{};

    case TC18_BooleanFalse:
        return Value(false);

    case TC18_BooleanTrue:
        return Value(true);

    case TC18_Boolean: {
        LUXON_TRY_ASSIGN(v, r.read_u8());
        return Value(static_cast<bool>(v != 0));
    }

    case TC18_ByteZero:
        return Value(static_cast<uint8_t>(0));

    case TC18_Byte: {
        LUXON_TRY_ASSIGN(v, r.read_u8());
        return Value(v);
    }

    case TC18_ShortZero:
        return Value(static_cast<int16_t>(0));

    case TC18_Short: {
        LUXON_TRY_ASSIGN(v, r.read_i16_le());
        return Value(v);
    }

    case TC18_IntZero:
        return Value(static_cast<int32_t>(0));

    case TC18_Int1: {
        LUXON_TRY_ASSIGN(v, r.read_u8());
        return Value(static_cast<int32_t>(v));
    }

    case TC18_Int1_: {
        LUXON_TRY_ASSIGN(v, r.read_u8());
        return Value(-static_cast<int32_t>(v));
    }

    case TC18_Int2: {
        LUXON_TRY_ASSIGN(v, r.read_u16_le());
        return Value(static_cast<int32_t>(v));
    }

    case TC18_Int2_: {
        LUXON_TRY_ASSIGN(v, r.read_u16_le());
        return Value(-static_cast<int32_t>(v));
    }

    case TC18_CompressedInt: {
        LUXON_TRY_ASSIGN(v, read_int32_payload(r));
        return Value(v);
    }

    case TC18_LongZero:
        return Value(static_cast<int64_t>(0));

    case TC18_L1: {
        LUXON_TRY_ASSIGN(v, r.read_u8());
        return Value(static_cast<int64_t>(v));
    }

    case TC18_L1_: {
        LUXON_TRY_ASSIGN(v, r.read_u8());
        return Value(-static_cast<int64_t>(v));
    }

    case TC18_L2: {
        LUXON_TRY_ASSIGN(v, r.read_u16_le());
        return Value(static_cast<int64_t>(v));
    }

    case TC18_L2_: {
        LUXON_TRY_ASSIGN(v, r.read_u16_le());
        return Value(-static_cast<int64_t>(v));
    }

    case TC18_CompressedLong: {
        LUXON_TRY_ASSIGN(v, read_int64_payload(r));
        return Value(v);
    }

    case TC18_FloatZero:
        return Value(0.0f);

    case TC18_Float: {
        LUXON_TRY_ASSIGN(v, r.read_f32_le());
        return Value(v);
    }

    case TC18_DoubleZero:
        return Value(0.0);

    case TC18_Double: {
        LUXON_TRY_ASSIGN(v, r.read_f64_le());
        return Value(v);
    }

    case TC18_String: {
        LUXON_TRY_ASSIGN(v, read_string_payload(r));
        return Value(std::move(v));
    }

    case TC18_ByteArray: {
        LUXON_TRY_ASSIGN(v, decode_byte_array_body(r));
        return Value(std::move(v));
    }

    case TC18_BooleanArray: {
        LUXON_TRY_ASSIGN(v, decode_bool_array_body(r));
        return Value(std::move(v));
    }

    case TC18_ShortArray: {
        LUXON_TRY_ASSIGN(v, decode_short_array_body(r));
        return Value(std::move(v));
    }

    case TC18_CompressedIntArray: {
        LUXON_TRY_ASSIGN(v, decode_int_array_body(r));
        return Value(std::move(v));
    }

    case TC18_CompressedLongArray: {
        LUXON_TRY_ASSIGN(v, decode_long_array_body(r));
        return Value(std::move(v));
    }

    case TC18_FloatArray: {
        LUXON_TRY_ASSIGN(v, decode_float_array_body(r));
        return Value(std::move(v));
    }

    case TC18_DoubleArray: {
        LUXON_TRY_ASSIGN(v, decode_double_array_body(r));
        return Value(std::move(v));
    }

    case TC18_StringArray: {
        LUXON_TRY_ASSIGN(v, decode_string_array_body(r));
        return Value(std::move(v));
    }

    case TC18_ObjectArray: {
        LUXON_TRY_ASSIGN(v, decode_object_array_body(r, depth, decode_typed));
        return Value(std::move(v));
    }

    case TC18_Array: {
        LUXON_TRY_ASSIGN(v, decode_jagged_array_body(r, depth, decode_typed));
        return Value(std::move(v));
    }

    case TC18_Hashtable: {
        LUXON_TRY_ASSIGN(v, decode_hashtable_body(r, depth, decode_typed));
        return Value(std::move(v));
    }

    case TC18_Dictionary: {
        LUXON_TRY_ASSIGN(hdr, read_dict_header(r, true));
        return decode_dictionary_body_from_header(r, hdr, depth, decode_typed);
    }

    case TC18_Custom: {
        LUXON_TRY_ASSIGN(v, decode_custom_typed(r, tc));
        return Value(std::move(v));
    }

    case TC18_CustomTypeArray: {
        LUXON_TRY_ASSIGN(v, decode_custom_array_body(r));
        return Value(std::move(v));
    }

    case TC18_HashtableArray: {
        LUXON_TRY_ASSIGN(v, decode_hashtable_array_body(r, depth, decode_typed));
        return Value(std::move(v));
    }

    case TC18_DictionaryArray: {
        LUXON_TRY_ASSIGN(hdr, read_dict_header(r, true));
        LUXON_TRY_ASSIGN(count, r.read_varuint32());

        if (is_byte_object_header(hdr)) {
            std::vector<Dictionary> out;
            out.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                LUXON_TRY_ASSIGN(v, decode_dictionary_body_from_header(r, hdr, depth, decode_typed));
                const auto *ptr = v.get_ptr<Dictionary>();
                if (!ptr)
                    return err(Error::Code::InvalidValue, "dictionary array element did not decode as Dictionary<byte,object>");
                out.push_back(*ptr);
            }
            return Value(std::move(out));
        }

        std::vector<GenericDictionary> out;
        out.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            LUXON_TRY_ASSIGN(v, decode_dictionary_body_from_header(r, hdr, depth, decode_typed));
            const auto *ptr = v.get_ptr<GenericDictionary>();
            if (!ptr)
                return err(Error::Code::InvalidValue, "dictionary array element did not decode as GenericDictionary");
            out.push_back(*ptr);
        }
        return Value(std::move(out));
    }

    case TC18_EventData: {
        EventMessage msg{};
        LUXON_TRY_ASSIGN(ec, r.read_u8());
        msg.event_code = ec;
        LUXON_TRY_ASSIGN(params, decode_parameters(r, depth));
        msg.parameters = std::move(params);
        return Value(std::move(msg));
    }

    case TC18_OperationRequest: {
        OperationRequestMessage msg{};
        LUXON_TRY_ASSIGN(op, r.read_u8());
        msg.operation_code = op;
        LUXON_TRY_ASSIGN(params, decode_parameters(r, depth));
        msg.parameters = std::move(params);
        return Value(std::move(msg));
    }

    case TC18_OperationResponse: {
        OperationResponseMessage msg{};
        LUXON_TRY_ASSIGN(op, r.read_u8());
        LUXON_TRY_ASSIGN(rc, r.read_i16_le());
        msg.operation_code = op;
        msg.return_code = rc;
        LUXON_TRY_ASSIGN(dm, decode_typed_string_or_null(r));
        msg.debug_message = std::move(dm);
        LUXON_TRY_ASSIGN(params, decode_parameters(r, depth));
        msg.parameters = std::move(params);
        return Value(std::move(msg));
    }

    default:
        return err(Error::Code::UnsupportedTypeCode, "unsupported GpBinaryV18 type code");
    }
}

std::expected<ByteArray, Error> GpBinaryV18::Serialize(const Message& message) {
    ByteWriter payload;
    Kind kind{};

    LUXON_TRY(static_cast<const MessageVariant&>(message).visit([&](const auto& m) -> std::expected<void, Error> {
        using T = std::decay_t<decltype(m)>;

        if constexpr (std::is_same_v<T, InitMessage>) {
            kind = Kind::Init;
            payload.write_u8(m.protocol_major);
            payload.write_u8(m.protocol_minor);

            // Repack sdk_id
            uint8_t sdk_enc = static_cast<uint8_t>(m.client_sdk_id << 1);
            payload.write_u8(sdk_enc);

            // Repack version/ipv6 flags
            uint8_t vcombined = static_cast<uint8_t>((m.ipv6 ? 0x80 : 0x00) | ((m.version_major & 0x07) << 4) | (m.version_minor & 0x0F));
            payload.write_u8(vcombined);

            payload.write_u8(m.version_patch);
            payload.write_u8(m.version_revision);

            // 1 byte of padding
            payload.write_u8(0x00);

            // Fixed 32-byte app_id, padded with nulls
            ByteArray appid_bytes(32, 0);
            if (!m.app_id.empty()) {
                std::memcpy(appid_bytes.data(), m.app_id.data(), std::min<std::size_t>(32, m.app_id.size()));
            }
            payload.write_bytes(appid_bytes);
            return {};
        } else if constexpr (std::is_same_v<T, InitResponseMessage>) {
            kind = Kind::InitResponse;
            payload.write_u8(0);
            return {};
        } else if constexpr (std::is_same_v<T, OperationRequestMessage>) {
            kind = Kind::Operation;
            payload.write_u8(m.operation_code);
            LUXON_TRY(encode_parameters(payload, m.parameters, 0));
            return {};
        } else if constexpr (std::is_same_v<T, OperationResponseMessage>) {
            kind = Kind::OperationResponse;
            payload.write_u8(m.operation_code);
            payload.write_i16_le(m.return_code);
            if (!m.debug_message || m.debug_message->empty()) {
                payload.write_u8(TC18_Null);
            } else {
                payload.write_u8(TC18_String);
                LUXON_TRY(write_string_payload(payload, *m.debug_message));
            }
            LUXON_TRY(encode_parameters(payload, m.parameters, 0));
            return {};
        } else if constexpr (std::is_same_v<T, EventMessage>) {
            kind = Kind::Event;
            payload.write_u8(m.event_code);
            LUXON_TRY(encode_parameters(payload, m.parameters, 0));
            return {};
        } else if constexpr (std::is_same_v<T, DisconnectMessage>) {
            kind = Kind::DisconnectMessage;
            payload.write_i16_le(m.code);
            LUXON_TRY(encode_typed_string_or_null(payload, m.message));
            LUXON_TRY(encode_parameters(payload, m.parameters, 0));
            return {};
        } else if constexpr (std::is_same_v<T, InternalOperationRequestMessage>) {
            kind = Kind::InternalOperationRequest;
            payload.write_u8(m.operation_code);
            LUXON_TRY(encode_parameters(payload, m.parameters, 0));
            return {};
        } else if constexpr (std::is_same_v<T, InternalOperationResponseMessage>) {
            kind = Kind::InternalOperationResponse;
            payload.write_u8(m.operation_code);
            payload.write_i16_le(m.return_code);
            if (!m.debug_message || m.debug_message->empty()) {
                payload.write_u8(TC18_Null);
            } else {
                payload.write_u8(TC18_String);
                LUXON_TRY(write_string_payload(payload, *m.debug_message));
            }
            LUXON_TRY(encode_parameters(payload, m.parameters, 0));
            return {};
        } else if constexpr (std::is_same_v<T, GenericValueMessage>) {
            kind = Kind::Message;
            LUXON_TRY(EncodeValue(payload, m.value, 0));
            return {};
        } else if constexpr (std::is_same_v<T, RawMessage>) {
            kind = Kind::RawMessage;
            write_byte_array(payload, m.bytes);
            return {};
        } else {
            return err(Error::Code::UnsupportedKind, "unsupported Message variant for GpBinaryV18");
        }
    }));

    LUXON_TRY_ASSIGN(maybe_payload, maybe_encrypt_payload(kind, message.encrypted, payload.bytes()));

    ByteArray out;
    out.reserve(2 + maybe_payload.size());
    out.push_back(GP_MAGIC);
    out.push_back(static_cast<uint8_t>(static_cast<uint8_t>(kind) | (message.encrypted ? 0x80u : 0x00u)));
    out.insert(out.end(), maybe_payload.begin(), maybe_payload.end());
    return out;
}

std::expected<Message, Error> GpBinaryV18::Deserialize(std::span<const uint8_t> packet_bytes) {
    if (packet_bytes.size() < 2)
        return err(Error::Code::BadPacket, "GpBinaryV18 packet too short");
    if (packet_bytes[0] != GP_MAGIC)
        return err(Error::Code::BadMagic, "GpBinaryV18 bad magic");

    const uint8_t kind_byte = packet_bytes[1];
    const bool encrypted = (kind_byte & 0x80u) != 0;
    const uint8_t kind_raw = static_cast<uint8_t>(kind_byte & 0x7Fu);

    Kind kind{};
    switch (kind_raw) {
    case static_cast<uint8_t>(Kind::Init):
        kind = Kind::Init;
        break;
    case static_cast<uint8_t>(Kind::InitResponse):
        kind = Kind::InitResponse;
        break;
    case static_cast<uint8_t>(Kind::Operation):
        kind = Kind::Operation;
        break;
    case static_cast<uint8_t>(Kind::OperationResponse):
        kind = Kind::OperationResponse;
        break;
    case static_cast<uint8_t>(Kind::Event):
        kind = Kind::Event;
        break;
    case static_cast<uint8_t>(Kind::DisconnectMessage):
        kind = Kind::DisconnectMessage;
        break;
    case static_cast<uint8_t>(Kind::InternalOperationRequest):
        kind = Kind::InternalOperationRequest;
        break;
    case static_cast<uint8_t>(Kind::InternalOperationResponse):
        kind = Kind::InternalOperationResponse;
        break;
    case static_cast<uint8_t>(Kind::Message):
        kind = Kind::Message;
        break;
    case static_cast<uint8_t>(Kind::RawMessage):
        kind = Kind::RawMessage;
        break;
    default:
        return err(Error::Code::UnsupportedKind, "unsupported GpBinaryV18 kind");
    }

    LUXON_TRY_ASSIGN(payload_bytes, maybe_decrypt_payload(kind, encrypted, packet_bytes.subspan(2)));
    ByteReader r(std::span<const uint8_t>(payload_bytes.data(), payload_bytes.size()));

    switch (kind) {
    case Kind::Init: {
        if (payload_bytes.size() != 39)
            return err(Error::Code::InvalidValue, "init payload must be 39 bytes");

        InitMessage msg{};
        LUXON_TRY_ASSIGN(protocol_major, r.read_u8());
        LUXON_TRY_ASSIGN(protocol_minor, r.read_u8());
        LUXON_TRY_ASSIGN(sdk_enc, r.read_u8());
        LUXON_TRY_ASSIGN(vcombined, r.read_u8());
        LUXON_TRY_ASSIGN(patch, r.read_u8());
        LUXON_TRY_ASSIGN(rev, r.read_u8());
        LUXON_TRY_ASSIGN(pad, r.read_u8());
        (void)pad; // discarded

        LUXON_TRY_ASSIGN(app_span, r.read_span(32));

        msg.protocol_major = protocol_major;
        msg.protocol_minor = protocol_minor;
        msg.client_sdk_id = static_cast<uint8_t>(sdk_enc >> 1);
        msg.ipv6 = (vcombined & 0x80) != 0;
        msg.version_major = static_cast<uint8_t>((vcombined >> 4) & 0x07);
        msg.version_minor = static_cast<uint8_t>(vcombined & 0x0F);
        msg.version_patch = patch;
        msg.version_revision = rev;

        std::size_t end = 0;
        while (end < 32 && app_span[end] != 0x00)
            ++end;
        msg.app_id = std::string(reinterpret_cast<const char *>(app_span.data()), end);

        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "Init packet has trailing bytes");

        return Message(std::move(msg), encrypted);
    }

    case Kind::InitResponse: {
        (void)r.read_u8(); // discarded
        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "InitResponse packet has trailing bytes");
        return Message(InitResponseMessage{}, encrypted);
    }

    case Kind::Operation: {
        OperationRequestMessage msg{};
        LUXON_TRY_ASSIGN(op, r.read_u8());
        msg.operation_code = op;
        LUXON_TRY_ASSIGN(params, decode_parameters(r, 0));
        msg.parameters = std::move(params);
        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "Operation packet has trailing bytes");
        return Message(std::move(msg), encrypted);
    }

    case Kind::OperationResponse: {
        OperationResponseMessage msg{};
        LUXON_TRY_ASSIGN(op, r.read_u8());
        LUXON_TRY_ASSIGN(rc, r.read_i16_le());
        LUXON_TRY_ASSIGN(dm, decode_typed_string_or_null(r));
        LUXON_TRY_ASSIGN(params, decode_parameters(r, 0));

        msg.operation_code = op;
        msg.return_code = rc;
        msg.debug_message = std::move(dm);
        msg.parameters = std::move(params);

        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "OperationResponse packet has trailing bytes");
        return Message(std::move(msg), encrypted);
    }

    case Kind::Event: {
        EventMessage msg{};
        LUXON_TRY_ASSIGN(ec, r.read_u8());
        LUXON_TRY_ASSIGN(params, decode_parameters(r, 0));
        msg.event_code = ec;
        msg.parameters = std::move(params);
        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "Event packet has trailing bytes");
        return Message(std::move(msg), encrypted);
    }

    case Kind::DisconnectMessage: {
        DisconnectMessage msg{};
        LUXON_TRY_ASSIGN(code, r.read_i16_le());
        LUXON_TRY_ASSIGN(text, decode_typed_string_or_null(r));
        LUXON_TRY_ASSIGN(params, decode_parameters(r, 0));
        msg.code = code;
        msg.message = std::move(text);
        msg.parameters = std::move(params);
        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "DisconnectMessage packet has trailing bytes");
        return Message(std::move(msg), encrypted);
    }

    case Kind::InternalOperationRequest: {
        InternalOperationRequestMessage msg{};
        LUXON_TRY_ASSIGN(op, r.read_u8());
        LUXON_TRY_ASSIGN(params, decode_parameters(r, 0));
        msg.operation_code = op;
        msg.parameters = std::move(params);
        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "InternalOperationRequest packet has trailing bytes");
        return Message(std::move(msg), encrypted);
    }

    case Kind::InternalOperationResponse: {
        InternalOperationResponseMessage msg{};
        LUXON_TRY_ASSIGN(op, r.read_u8());
        LUXON_TRY_ASSIGN(rc, r.read_i16_le());
        LUXON_TRY_ASSIGN(dm, decode_typed_string_or_null(r));
        LUXON_TRY_ASSIGN(params, decode_parameters(r, 0));
        msg.operation_code = op;
        msg.return_code = rc;
        msg.debug_message = std::move(dm);
        msg.parameters = std::move(params);
        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "InternalOperationResponse packet has trailing bytes");
        return Message(std::move(msg), encrypted);
    }

    case Kind::Message: {
        LUXON_TRY_ASSIGN(v, DecodeValue(r, 0));
        if (r.remaining() != 0)
            return err(Error::Code::InvalidValue, "Message packet has trailing bytes");
        return Message(GenericValueMessage{.value = std::move(v)}, encrypted);
    }

    case Kind::RawMessage: {
        if (r.remaining() != 0)
            return Message(RawMessage{.bytes = ByteArray(payload_bytes.begin(), payload_bytes.end())}, encrypted);
        return Message(RawMessage{}, encrypted);
    }
    }

    return err(Error::Code::UnsupportedKind, "unsupported GpBinaryV18 kind");
}

#undef LUXON_TRY
#undef LUXON_TRY_ASSIGN

} // namespace luxon::ser
