// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "ser_gp_binary_v18.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>

namespace luxon::ser {
namespace {

// -------------------- Type codes --------------------

enum : uint8_t {
    TC_Unknown = 0,
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
    TC_OperationRequest = 24,
    TC_OperationResponse = 25,
    TC_EventData = 26,

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

    TC_CustomTypeArray = 83,
    TC_DictionaryArray = 84,
    TC_HashtableArray = 85,
};

struct DictHeaderDesc;

struct TypeDesc {
    enum class Kind : uint8_t {
        Object,
        Boolean,
        Byte,
        Short,
        Float,
        Double,
        String,
        Int,
        Long,
        Hashtable,
        Dictionary,
        ObjectArray,
        BooleanArray,
        ByteArray,
        ShortArray,
        FloatArray,
        DoubleArray,
        StringArray,
        IntArray,
        LongArray,
        HashtableArray,
        JaggedArray,
    };

    Kind kind{};
    std::shared_ptr<DictHeaderDesc> dict{};
    std::shared_ptr<TypeDesc> element{};
};

struct DictHeaderDesc {
    uint8_t key_code{};
    TypeDesc value{};
    ByteArray raw{};
};

inline bool is_array_value(const Value& v) {
    return v.is<ByteArray>() || v.is<std::vector<bool>>() || v.is<std::vector<int16_t>>() || v.is<std::vector<int32_t>>() || v.is<std::vector<int64_t>>() ||
           v.is<std::vector<float>>() || v.is<std::vector<double>>() || v.is<std::vector<std::string>>() || v.is<ObjectArray>() || v.is<JaggedArray>() ||
           v.is<std::vector<Dictionary>>() || v.is<std::vector<GenericDictionary>>() || v.is<std::vector<HashtablePtr>>() ||
           v.is<std::vector<RawCustomValue>>();
}

std::expected<void, Error> write_size32(ByteWriter& w, std::size_t n, std::string_view what) {
    if (n > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
        return std::unexpected(Error{
            .code = Error::Code::InvalidValue,
            .message = std::string(what) + " too large",
        });
    }
    w.write_varuint32(static_cast<uint32_t>(n));
    return {};
}

std::expected<void, Error> write_string_v18(ByteWriter& w, const std::string& s) {
    if (s.size() > 32767) {
        return std::unexpected(Error{
            .code = Error::Code::InvalidValue,
            .message = "string UTF-8 byte length exceeds 32767",
        });
    }
    auto rc = write_size32(w, s.size(), "string length");
    if (!rc)
        return std::unexpected(rc.error());

    w.write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
    return {};
}

std::expected<std::string, Error> read_string_v18(ByteReader& r) {
    auto len = r.read_varuint32();
    if (!len)
        return std::unexpected(len.error());

    auto s = r.read_span(static_cast<std::size_t>(*len));
    if (!s)
        return std::unexpected(s.error());

    return std::string(reinterpret_cast<const char *>(s->data()), s->size());
}

std::expected<uint8_t, Error> read_dict_key_desc(ByteReader& r, ByteArray *raw) {
    auto b = r.read_u8();
    if (!b)
        return std::unexpected(b.error());
    if (raw)
        raw->push_back(*b);

    switch (*b) {
    case 0x00:
    case TC_Boolean: // writer quirk: can emit, reader later rejects during dictionary body decode
    case TC_Byte:
    case TC_Int16:
    case TC_Float:
    case TC_Double:
    case TC_String:
    case TC_CompressedInt32:
    case TC_CompressedInt64:
        return *b;
    default:
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "invalid dictionary key descriptor"});
    }
}

std::expected<TypeDesc, Error> read_dict_value_desc(ByteReader& r, ByteArray *raw);

std::expected<DictHeaderDesc, Error> read_dict_header(ByteReader& r) {
    DictHeaderDesc out;
    auto key = read_dict_key_desc(r, &out.raw);
    if (!key)
        return std::unexpected(key.error());
    out.key_code = *key;

    auto value = read_dict_value_desc(r, &out.raw);
    if (!value)
        return std::unexpected(value.error());
    out.value = std::move(*value);

    return out;
}

inline bool type_desc_is_1d_array(TypeDesc::Kind k) {
    switch (k) {
    case TypeDesc::Kind::ObjectArray:
    case TypeDesc::Kind::BooleanArray:
    case TypeDesc::Kind::ByteArray:
    case TypeDesc::Kind::ShortArray:
    case TypeDesc::Kind::FloatArray:
    case TypeDesc::Kind::DoubleArray:
    case TypeDesc::Kind::StringArray:
    case TypeDesc::Kind::IntArray:
    case TypeDesc::Kind::LongArray:
    case TypeDesc::Kind::HashtableArray:
        return true;
    default:
        return false;
    }
}

std::expected<TypeDesc, Error> read_dict_value_desc(ByteReader& r, ByteArray *raw) {
    auto b = r.read_u8();
    if (!b)
        return std::unexpected(b.error());
    if (raw)
        raw->push_back(*b);

    switch (*b) {
    case 0x00:
        return TypeDesc{.kind = TypeDesc::Kind::Object};
    case TC_Boolean:
        return TypeDesc{.kind = TypeDesc::Kind::Boolean};
    case TC_Byte:
        return TypeDesc{.kind = TypeDesc::Kind::Byte};
    case TC_Int16:
        return TypeDesc{.kind = TypeDesc::Kind::Short};
    case TC_Float:
        return TypeDesc{.kind = TypeDesc::Kind::Float};
    case TC_Double:
        return TypeDesc{.kind = TypeDesc::Kind::Double};
    case TC_String:
        return TypeDesc{.kind = TypeDesc::Kind::String};
    case TC_CompressedInt32:
        return TypeDesc{.kind = TypeDesc::Kind::Int};
    case TC_CompressedInt64:
        return TypeDesc{.kind = TypeDesc::Kind::Long};
    case TC_Hashtable:
        return TypeDesc{.kind = TypeDesc::Kind::Hashtable};
    case TC_ObjectArray:
        return TypeDesc{.kind = TypeDesc::Kind::ObjectArray};
    case TC_BooleanArray:
        return TypeDesc{.kind = TypeDesc::Kind::BooleanArray};
    case TC_ByteArray:
        return TypeDesc{.kind = TypeDesc::Kind::ByteArray};
    case TC_Int16Array:
        return TypeDesc{.kind = TypeDesc::Kind::ShortArray};
    case TC_FloatArray:
        return TypeDesc{.kind = TypeDesc::Kind::FloatArray};
    case TC_DoubleArray:
        return TypeDesc{.kind = TypeDesc::Kind::DoubleArray};
    case TC_StringArray:
        return TypeDesc{.kind = TypeDesc::Kind::StringArray};
    case TC_CompressedInt32Array:
        return TypeDesc{.kind = TypeDesc::Kind::IntArray};
    case TC_CompressedInt64Array:
        return TypeDesc{.kind = TypeDesc::Kind::LongArray};
    case TC_HashtableArray:
        return TypeDesc{.kind = TypeDesc::Kind::HashtableArray};

    case TC_Dictionary: {
        auto nested = read_dict_header(r);
        if (!nested)
            return std::unexpected(nested.error());
        if (raw)
            raw->insert(raw->end(), nested->raw.begin(), nested->raw.end());
        return TypeDesc{
            .kind = TypeDesc::Kind::Dictionary,
            .dict = std::make_shared<DictHeaderDesc>(std::move(*nested)),
        };
    }

    case TC_ArrayRecursive: {
        auto child = read_dict_value_desc(r, raw);
        if (!child)
            return std::unexpected(child.error());
        if (!(type_desc_is_1d_array(child->kind) || child->kind == TypeDesc::Kind::JaggedArray)) {
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "invalid jagged array descriptor"});
        }
        return TypeDesc{
            .kind = TypeDesc::Kind::JaggedArray,
            .element = std::make_shared<TypeDesc>(std::move(*child)),
        };
    }

    default:
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "invalid dictionary value descriptor"});
    }
}

std::expected<DictHeaderDesc, Error> parse_dict_header_bytes(std::span<const uint8_t> bytes) {
    ByteReader r(bytes);
    auto hdr = read_dict_header(r);
    if (!hdr)
        return std::unexpected(hdr.error());
    if (r.remaining() != 0) {
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary header has trailing bytes"});
    }
    return hdr;
}

inline bool is_byte_object_dict_header(const DictHeaderDesc& hdr) { return hdr.raw.size() == 2 && hdr.raw[0] == TC_Byte && hdr.raw[1] == 0x00; }

std::expected<void, Error> encode_debug_field_null_or_string(ByteWriter& w, const std::optional<std::string>& s) {
    if (s.has_value() && !s->empty()) {
        w.write_u8(TC_String);
        return write_string_v18(w, *s);
    }
    w.write_u8(TC_Null);
    return {};
}

// forward decls
std::expected<void, Error> encode_payload_by_desc(ByteWriter& w, const GpBinaryV18& self, const TypeDesc& desc, const Value& v, int depth);
std::expected<Value, Error> decode_payload_by_desc(ByteReader& r, const GpBinaryV18& self, const TypeDesc& desc, int depth);

std::expected<void, Error> encode_hashtable_payload(ByteWriter& w, const GpBinaryV18& self, const HashtablePtr& ht, int depth) {
    const std::size_t count = ht ? ht->size() : 0;
    auto rc = write_size32(w, count, "hashtable entry count");
    if (!rc)
        return std::unexpected(rc.error());

    if (!ht)
        return {};

    for (const auto& [k, v] : *ht) {
        auto ek = self.encode_value(w, k, depth + 1);
        if (!ek)
            return std::unexpected(ek.error());
        auto ev = self.encode_value(w, v, depth + 1);
        if (!ev)
            return std::unexpected(ev.error());
    }
    return {};
}

std::expected<HashtablePtr, Error> decode_hashtable_payload(ByteReader& r, const GpBinaryV18& self, int depth) {
    auto count = r.read_varuint32();
    if (!count)
        return std::unexpected(count.error());

    auto ht = std::make_shared<Hashtable>();
    for (uint32_t i = 0; i < *count; ++i) {
        auto k = self.decode_value(r, depth + 1);
        if (!k)
            return std::unexpected(k.error());
        auto v = self.decode_value(r, depth + 1);
        if (!v)
            return std::unexpected(v.error());

        if (!k->is_null())
            ht->emplace(std::move(*k), std::move(*v));
    }
    return ht;
}

std::expected<void, Error> encode_object_array_payload(ByteWriter& w, const GpBinaryV18& self, const ObjectArray& a, int depth) {
    auto rc = write_size32(w, a.size(), "object array count");
    if (!rc)
        return std::unexpected(rc.error());

    for (const auto& e : a) {
        auto enc = self.encode_value(w, e, depth + 1);
        if (!enc)
            return std::unexpected(enc.error());
    }
    return {};
}

std::expected<ObjectArray, Error> decode_object_array_payload(ByteReader& r, const GpBinaryV18& self, int depth) {
    auto count = r.read_varuint32();
    if (!count)
        return std::unexpected(count.error());

    ObjectArray out;
    out.reserve(static_cast<std::size_t>(*count));
    for (uint32_t i = 0; i < *count; ++i) {
        auto v = self.decode_value(r, depth + 1);
        if (!v)
            return std::unexpected(v.error());
        out.push_back(std::move(*v));
    }
    return out;
}

std::expected<void, Error> encode_jagged_array_payload(ByteWriter& w, const GpBinaryV18& self, const JaggedArray& a, int depth) {
    auto rc = write_size32(w, a.elements.size(), "jagged array count");
    if (!rc)
        return std::unexpected(rc.error());

    for (const auto& e : a.elements) {
        if (!is_array_value(e)) {
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "jagged array element is not an array value"});
        }
        auto enc = self.encode_value(w, e, depth + 1);
        if (!enc)
            return std::unexpected(enc.error());
    }
    return {};
}

std::expected<JaggedArray, Error> decode_jagged_array_payload(ByteReader& r, const GpBinaryV18& self, int depth) {
    auto count = r.read_varuint32();
    if (!count)
        return std::unexpected(count.error());

    JaggedArray out;
    out.elements.reserve(static_cast<std::size_t>(*count));
    for (uint32_t i = 0; i < *count; ++i) {
        auto v = self.decode_value(r, depth + 1);
        if (!v)
            return std::unexpected(v.error());
        out.elements.push_back(std::move(*v));
    }
    return out;
}

std::expected<void, Error> encode_hashtable_array_payload(ByteWriter& w, const GpBinaryV18& self, const std::vector<HashtablePtr>& a, int depth) {
    auto rc = write_size32(w, a.size(), "hashtable array count");
    if (!rc)
        return std::unexpected(rc.error());

    for (const auto& ht : a) {
        auto eh = encode_hashtable_payload(w, self, ht, depth + 1);
        if (!eh)
            return std::unexpected(eh.error());
    }
    return {};
}

std::expected<std::vector<HashtablePtr>, Error> decode_hashtable_array_payload(ByteReader& r, const GpBinaryV18& self, int depth) {
    auto count = r.read_varuint32();
    if (!count)
        return std::unexpected(count.error());

    std::vector<HashtablePtr> out;
    out.reserve(static_cast<std::size_t>(*count));
    for (uint32_t i = 0; i < *count; ++i) {
        auto ht = decode_hashtable_payload(r, self, depth + 1);
        if (!ht)
            return std::unexpected(ht.error());
        out.push_back(std::move(*ht));
    }
    return out;
}

std::expected<void, Error> encode_key_payload(ByteWriter& w, const GpBinaryV18& self, uint8_t key_desc, const Value& key, int depth) {
    switch (key_desc) {
    case 0x00:
        return self.encode_value(w, key, depth + 1);

    case TC_Boolean:
        if (!key.is<bool>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary key is not bool"});
        w.write_u8(key.get<bool>() ? 1 : 0);
        return {};

    case TC_Byte:
        if (!key.is<uint8_t>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary key is not byte"});
        w.write_u8(key.get<uint8_t>());
        return {};

    case TC_Int16:
        if (!key.is<int16_t>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary key is not short"});
        w.write_i16_le(key.get<int16_t>());
        return {};

    case TC_Float:
        if (!key.is<float>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary key is not float"});
        w.write_f32_le(key.get<float>());
        return {};

    case TC_Double:
        if (!key.is<double>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary key is not double"});
        w.write_f64_le(key.get<double>());
        return {};

    case TC_String:
        if (!key.is<std::string>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary key is not string"});
        return write_string_v18(w, key.get<std::string>());

    case TC_CompressedInt32:
        if (!key.is<int32_t>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary key is not int"});
        w.write_varuint32(static_cast<uint32_t>(zigzag_encode_32(key.get<int32_t>())));
        return {};

    case TC_CompressedInt64:
        if (!key.is<int64_t>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary key is not long"});
        w.write_varuint64(zigzag_encode_64(key.get<int64_t>()));
        return {};

    default:
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "unsupported dictionary key descriptor"});
    }
}

std::expected<Value, Error> decode_key_payload(ByteReader& r, const GpBinaryV18& self, uint8_t key_desc, int depth) {
    switch (key_desc) {
    case 0x00:
        return self.decode_value(r, depth + 1);

    case TC_Byte: {
        auto v = r.read_u8();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC_Int16: {
        auto v = r.read_i16_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC_Float: {
        auto v = r.read_f32_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC_Double: {
        auto v = r.read_f64_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TC_String: {
        auto s = read_string_v18(r);
        if (!s)
            return std::unexpected(s.error());
        return Value(std::move(*s));
    }

    case TC_CompressedInt32: {
        auto u = r.read_varuint32();
        if (!u)
            return std::unexpected(u.error());
        return Value(zigzag_decode_32(*u));
    }

    case TC_CompressedInt64: {
        auto u = r.read_varuint64();
        if (!u)
            return std::unexpected(u.error());
        return Value(zigzag_decode_64(*u));
    }

    case TC_Boolean:
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "bool dictionary keys are rejected by V18 reader"});

    default:
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "unsupported dictionary key descriptor"});
    }
}

std::expected<void, Error> encode_generic_dictionary_payload(ByteWriter& w, const GpBinaryV18& self, const DictHeaderDesc& hdr, const GenericDictionary& gd,
                                                             int depth) {
    if (gd.header != hdr.raw) {
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "generic dictionary header mismatch"});
    }

    auto rc = write_size32(w, gd.entries.size(), "dictionary entry count");
    if (!rc)
        return std::unexpected(rc.error());

    for (const auto& [k, v] : gd.entries) {
        auto ek = encode_key_payload(w, self, hdr.key_code, k, depth + 1);
        if (!ek)
            return std::unexpected(ek.error());

        auto ev = encode_payload_by_desc(w, self, hdr.value, v, depth + 1);
        if (!ev)
            return std::unexpected(ev.error());
    }

    return {};
}

std::expected<GenericDictionary, Error> decode_generic_dictionary_payload(ByteReader& r, const GpBinaryV18& self, const DictHeaderDesc& hdr, int depth) {
    if (hdr.key_code == TC_Boolean) {
        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "bool dictionary keys are rejected by V18 reader"});
    }

    auto count = r.read_varuint32();
    if (!count)
        return std::unexpected(count.error());

    GenericDictionary out;
    out.header = hdr.raw;
    out.entries.reserve(static_cast<std::size_t>(*count));

    for (uint32_t i = 0; i < *count; ++i) {
        auto k = decode_key_payload(r, self, hdr.key_code, depth + 1);
        if (!k)
            return std::unexpected(k.error());

        auto v = decode_payload_by_desc(r, self, hdr.value, depth + 1);
        if (!v)
            return std::unexpected(v.error());

        out.entries.emplace_back(std::move(*k), std::move(*v));
    }

    return out;
}

std::expected<void, Error> encode_convenience_dictionary_payload(ByteWriter& w, const GpBinaryV18& self, const Dictionary& d, int depth) {
    auto rc = write_size32(w, d.size(), "dictionary entry count");
    if (!rc)
        return std::unexpected(rc.error());

    for (const auto& [k, v] : d) {
        w.write_u8(k);
        auto ev = self.encode_value(w, v, depth + 1);
        if (!ev)
            return std::unexpected(ev.error());
    }

    return {};
}

std::expected<Dictionary, Error> decode_convenience_dictionary_payload(ByteReader& r, const GpBinaryV18& self, int depth) {
    auto count = r.read_varuint32();
    if (!count)
        return std::unexpected(count.error());

    Dictionary out;
    out.reserve(static_cast<std::size_t>(*count));

    for (uint32_t i = 0; i < *count; ++i) {
        auto k = r.read_u8();
        if (!k)
            return std::unexpected(k.error());

        auto v = self.decode_value(r, depth + 1);
        if (!v)
            return std::unexpected(v.error());

        out[*k] = std::move(*v);
    }

    return out;
}

std::expected<Value, Error> decode_dictionary_payload_as_value(ByteReader& r, const GpBinaryV18& self, const DictHeaderDesc& hdr, int depth) {
    if (is_byte_object_dict_header(hdr)) {
        auto d = decode_convenience_dictionary_payload(r, self, depth + 1);
        if (!d)
            return std::unexpected(d.error());
        return Value(std::move(*d));
    }

    auto gd = decode_generic_dictionary_payload(r, self, hdr, depth + 1);
    if (!gd)
        return std::unexpected(gd.error());
    return Value(std::move(*gd));
}

std::expected<void, Error> encode_payload_by_desc(ByteWriter& w, const GpBinaryV18& self, const TypeDesc& desc, const Value& v, int depth) {
    switch (desc.kind) {
    case TypeDesc::Kind::Object:
        return self.encode_value(w, v, depth + 1);

    case TypeDesc::Kind::Boolean:
        if (!v.is<bool>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not bool"});
        w.write_u8(v.get<bool>() ? 1 : 0);
        return {};

    case TypeDesc::Kind::Byte:
        if (!v.is<uint8_t>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not byte"});
        w.write_u8(v.get<uint8_t>());
        return {};

    case TypeDesc::Kind::Short:
        if (!v.is<int16_t>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not short"});
        w.write_i16_le(v.get<int16_t>());
        return {};

    case TypeDesc::Kind::Float:
        if (!v.is<float>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not float"});
        w.write_f32_le(v.get<float>());
        return {};

    case TypeDesc::Kind::Double:
        if (!v.is<double>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not double"});
        w.write_f64_le(v.get<double>());
        return {};

    case TypeDesc::Kind::String:
        if (!v.is<std::string>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not string"});
        return write_string_v18(w, v.get<std::string>());

    case TypeDesc::Kind::Int:
        if (!v.is<int32_t>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not int"});
        w.write_varuint32(static_cast<uint32_t>(zigzag_encode_32(v.get<int32_t>())));
        return {};

    case TypeDesc::Kind::Long:
        if (!v.is<int64_t>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not long"});
        w.write_varuint64(zigzag_encode_64(v.get<int64_t>()));
        return {};

    case TypeDesc::Kind::Hashtable:
        if (!v.is<HashtablePtr>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not hashtable"});
        return encode_hashtable_payload(w, self, v.get<HashtablePtr>(), depth + 1);

    case TypeDesc::Kind::Dictionary:
        if (!desc.dict)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "missing dictionary descriptor"});
        if (v.is<Dictionary>()) {
            if (!is_byte_object_dict_header(*desc.dict)) {
                return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "convenience Dictionary only matches Dictionary<byte,object>"});
            }
            return encode_convenience_dictionary_payload(w, self, v.get<Dictionary>(), depth + 1);
        }
        if (!v.is<GenericDictionary>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not generic dictionary"});
        return encode_generic_dictionary_payload(w, self, *desc.dict, v.get<GenericDictionary>(), depth + 1);

    case TypeDesc::Kind::ObjectArray:
        if (!v.is<ObjectArray>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not object array"});
        return encode_object_array_payload(w, self, v.get<ObjectArray>(), depth + 1);

    case TypeDesc::Kind::BooleanArray: {
        if (!v.is<std::vector<bool>>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not bool array"});
        const auto& a = v.get<std::vector<bool>>();
        auto rc = write_size32(w, a.size(), "bool array count");
        if (!rc)
            return std::unexpected(rc.error());
        const std::size_t nbytes = (a.size() + 7) / 8;
        for (std::size_t bi = 0; bi < nbytes; ++bi) {
            uint8_t b = 0;
            for (int bit = 0; bit < 8; ++bit) {
                const std::size_t idx = bi * 8 + static_cast<std::size_t>(bit);
                if (idx >= a.size())
                    break;
                if (a[idx])
                    b |= static_cast<uint8_t>(1u << bit);
            }
            w.write_u8(b);
        }
        return {};
    }

    case TypeDesc::Kind::ByteArray: {
        if (!v.is<ByteArray>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not byte array"});
        const auto& a = v.get<ByteArray>();
        auto rc = write_size32(w, a.size(), "byte array length");
        if (!rc)
            return std::unexpected(rc.error());
        w.write_bytes(a);
        return {};
    }

    case TypeDesc::Kind::ShortArray: {
        if (!v.is<std::vector<int16_t>>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not short array"});
        const auto& a = v.get<std::vector<int16_t>>();
        auto rc = write_size32(w, a.size(), "short array count");
        if (!rc)
            return std::unexpected(rc.error());
        for (int16_t x : a)
            w.write_i16_le(x);
        return {};
    }

    case TypeDesc::Kind::FloatArray: {
        if (!v.is<std::vector<float>>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not float array"});
        const auto& a = v.get<std::vector<float>>();
        auto rc = write_size32(w, a.size(), "float array count");
        if (!rc)
            return std::unexpected(rc.error());
        for (float x : a)
            w.write_f32_le(x);
        return {};
    }

    case TypeDesc::Kind::DoubleArray: {
        if (!v.is<std::vector<double>>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not double array"});
        const auto& a = v.get<std::vector<double>>();
        auto rc = write_size32(w, a.size(), "double array count");
        if (!rc)
            return std::unexpected(rc.error());
        for (double x : a)
            w.write_f64_le(x);
        return {};
    }

    case TypeDesc::Kind::StringArray: {
        if (!v.is<std::vector<std::string>>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not string array"});
        const auto& a = v.get<std::vector<std::string>>();
        auto rc = write_size32(w, a.size(), "string array count");
        if (!rc)
            return std::unexpected(rc.error());
        for (const auto& s : a) {
            auto rs = write_string_v18(w, s);
            if (!rs)
                return std::unexpected(rs.error());
        }
        return {};
    }

    case TypeDesc::Kind::IntArray: {
        if (!v.is<std::vector<int32_t>>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not int array"});
        const auto& a = v.get<std::vector<int32_t>>();
        auto rc = write_size32(w, a.size(), "int array count");
        if (!rc)
            return std::unexpected(rc.error());
        for (int32_t x : a)
            w.write_varuint32(static_cast<uint32_t>(zigzag_encode_32(x)));
        return {};
    }

    case TypeDesc::Kind::LongArray: {
        if (!v.is<std::vector<int64_t>>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not long array"});
        const auto& a = v.get<std::vector<int64_t>>();
        auto rc = write_size32(w, a.size(), "long array count");
        if (!rc)
            return std::unexpected(rc.error());
        for (int64_t x : a)
            w.write_varuint64(zigzag_encode_64(x));
        return {};
    }

    case TypeDesc::Kind::HashtableArray:
        if (!v.is<std::vector<HashtablePtr>>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not hashtable array"});
        return encode_hashtable_array_payload(w, self, v.get<std::vector<HashtablePtr>>(), depth + 1);

    case TypeDesc::Kind::JaggedArray:
        if (!v.is<JaggedArray>())
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "value is not jagged array"});
        return encode_jagged_array_payload(w, self, v.get<JaggedArray>(), depth + 1);
    }

    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "unsupported payload descriptor"});
}

std::expected<Value, Error> decode_payload_by_desc(ByteReader& r, const GpBinaryV18& self, const TypeDesc& desc, int depth) {
    switch (desc.kind) {
    case TypeDesc::Kind::Object:
        return self.decode_value(r, depth + 1);

    case TypeDesc::Kind::Boolean: {
        auto b = r.read_u8();
        if (!b)
            return std::unexpected(b.error());
        return Value(*b != 0);
    }

    case TypeDesc::Kind::Byte: {
        auto b = r.read_u8();
        if (!b)
            return std::unexpected(b.error());
        return Value(*b);
    }

    case TypeDesc::Kind::Short: {
        auto v = r.read_i16_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TypeDesc::Kind::Float: {
        auto v = r.read_f32_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TypeDesc::Kind::Double: {
        auto v = r.read_f64_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }

    case TypeDesc::Kind::String: {
        auto s = read_string_v18(r);
        if (!s)
            return std::unexpected(s.error());
        return Value(std::move(*s));
    }

    case TypeDesc::Kind::Int: {
        auto u = r.read_varuint32();
        if (!u)
            return std::unexpected(u.error());
        return Value(zigzag_decode_32(*u));
    }

    case TypeDesc::Kind::Long: {
        auto u = r.read_varuint64();
        if (!u)
            return std::unexpected(u.error());
        return Value(zigzag_decode_64(*u));
    }

    case TypeDesc::Kind::Hashtable: {
        auto ht = decode_hashtable_payload(r, self, depth + 1);
        if (!ht)
            return std::unexpected(ht.error());
        return Value(std::move(*ht));
    }

    case TypeDesc::Kind::Dictionary:
        if (!desc.dict)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "missing dictionary descriptor"});
        return decode_dictionary_payload_as_value(r, self, *desc.dict, depth + 1);

    case TypeDesc::Kind::ObjectArray: {
        auto a = decode_object_array_payload(r, self, depth + 1);
        if (!a)
            return std::unexpected(a.error());
        return Value(std::move(*a));
    }

    case TypeDesc::Kind::BooleanArray: {
        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());
        std::vector<bool> out;
        out.resize(static_cast<std::size_t>(*count));

        auto raw = r.read_span((static_cast<std::size_t>(*count) + 7) / 8);
        if (!raw)
            return std::unexpected(raw.error());

        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = (((*raw)[i / 8] >> (i % 8)) & 1u) != 0;

        return Value(std::move(out));
    }

    case TypeDesc::Kind::ByteArray: {
        auto len = r.read_varuint32();
        if (!len)
            return std::unexpected(len.error());
        auto s = r.read_span(static_cast<std::size_t>(*len));
        if (!s)
            return std::unexpected(s.error());
        return Value(ByteArray(s->begin(), s->end()));
    }

    case TypeDesc::Kind::ShortArray: {
        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());
        std::vector<int16_t> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint32_t i = 0; i < *count; ++i) {
            auto v = r.read_i16_le();
            if (!v)
                return std::unexpected(v.error());
            out.push_back(*v);
        }
        return Value(std::move(out));
    }

    case TypeDesc::Kind::FloatArray: {
        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());
        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint32_t i = 0; i < *count; ++i) {
            auto v = r.read_f32_le();
            if (!v)
                return std::unexpected(v.error());
            out.push_back(*v);
        }
        return Value(std::move(out));
    }

    case TypeDesc::Kind::DoubleArray: {
        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());
        std::vector<double> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint32_t i = 0; i < *count; ++i) {
            auto v = r.read_f64_le();
            if (!v)
                return std::unexpected(v.error());
            out.push_back(*v);
        }
        return Value(std::move(out));
    }

    case TypeDesc::Kind::StringArray: {
        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());
        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint32_t i = 0; i < *count; ++i) {
            auto s = read_string_v18(r);
            if (!s)
                return std::unexpected(s.error());
            out.push_back(std::move(*s));
        }
        return Value(std::move(out));
    }

    case TypeDesc::Kind::IntArray: {
        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());
        std::vector<int32_t> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint32_t i = 0; i < *count; ++i) {
            auto u = r.read_varuint32();
            if (!u)
                return std::unexpected(u.error());
            out.push_back(zigzag_decode_32(*u));
        }
        return Value(std::move(out));
    }

    case TypeDesc::Kind::LongArray: {
        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());
        std::vector<int64_t> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint32_t i = 0; i < *count; ++i) {
            auto u = r.read_varuint64();
            if (!u)
                return std::unexpected(u.error());
            out.push_back(zigzag_decode_64(*u));
        }
        return Value(std::move(out));
    }

    case TypeDesc::Kind::HashtableArray: {
        auto a = decode_hashtable_array_payload(r, self, depth + 1);
        if (!a)
            return std::unexpected(a.error());
        return Value(std::move(*a));
    }

    case TypeDesc::Kind::JaggedArray: {
        auto a = decode_jagged_array_payload(r, self, depth + 1);
        if (!a)
            return std::unexpected(a.error());
        return Value(std::move(*a));
    }
    }

    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "unsupported payload descriptor"});
}

} // namespace

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

    ParameterList out;
    out.reserve(*cnt_b);

    for (uint32_t i = 0; i < *cnt_b; ++i) {
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

                const int64_t mag = (a < 0) ? -static_cast<int64_t>(a) : static_cast<int64_t>(a);
                if (a > 0 && mag <= 255) {
                    w.write_u8(TC_Int1_Pos);
                    w.write_u8(static_cast<uint8_t>(mag));
                    return {};
                }
                if (a < 0 && mag <= 255) {
                    w.write_u8(TC_Int1_Neg);
                    w.write_u8(static_cast<uint8_t>(mag));
                    return {};
                }
                if (a > 0 && mag <= 65535) {
                    w.write_u8(TC_Int2_Pos);
                    w.write_u16_le(static_cast<uint16_t>(mag));
                    return {};
                }
                if (a < 0 && mag <= 65535) {
                    w.write_u8(TC_Int2_Neg);
                    w.write_u16_le(static_cast<uint16_t>(mag));
                    return {};
                }

                w.write_u8(TC_CompressedInt32);
                w.write_varuint32(static_cast<uint32_t>(zigzag_encode_32(a)));
                return {};
            } else if constexpr (std::is_same_v<T, int64_t>) {
                if (a == 0) {
                    w.write_u8(TC_Int64Zero);
                    return {};
                }

                const uint64_t ua = static_cast<uint64_t>(a);
                const uint64_t mag = (a < 0) ? (uint64_t{0} - ua) : ua;

                if (a > 0 && mag <= 255) {
                    w.write_u8(TC_L1_Pos);
                    w.write_u8(static_cast<uint8_t>(mag));
                    return {};
                }
                if (a < 0 && mag <= 255) {
                    w.write_u8(TC_L1_Neg);
                    w.write_u8(static_cast<uint8_t>(mag));
                    return {};
                }
                if (a > 0 && mag <= 65535) {
                    w.write_u8(TC_L2_Pos);
                    w.write_u16_le(static_cast<uint16_t>(mag));
                    return {};
                }
                if (a < 0 && mag <= 65535) {
                    w.write_u8(TC_L2_Neg);
                    w.write_u16_le(static_cast<uint16_t>(mag));
                    return {};
                }

                w.write_u8(TC_CompressedInt64);
                w.write_varuint64(zigzag_encode_64(a));
                return {};
            } else if constexpr (std::is_same_v<T, float>) {
                w.write_u8(TC_Float);
                w.write_f32_le(a);
                return {};
            } else if constexpr (std::is_same_v<T, double>) {
                w.write_u8(TC_Double);
                w.write_f64_le(a);
                return {};
            } else if constexpr (std::is_same_v<T, std::string>) {
                w.write_u8(TC_String);
                return write_string_v18(w, a);
            } else if constexpr (std::is_same_v<T, ByteArray>) {
                w.write_u8(TC_ByteArray);
                auto rc = write_size32(w, a.size(), "byte array length");
                if (!rc)
                    return std::unexpected(rc.error());
                w.write_bytes(a);
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
                w.write_u8(TC_BooleanArray);
                TypeDesc desc{.kind = TypeDesc::Kind::BooleanArray};
                return encode_payload_by_desc(w, *this, desc, Value(a), depth);
            } else if constexpr (std::is_same_v<T, std::vector<int16_t>>) {
                w.write_u8(TC_Int16Array);
                TypeDesc desc{.kind = TypeDesc::Kind::ShortArray};
                return encode_payload_by_desc(w, *this, desc, Value(a), depth);
            } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
                w.write_u8(TC_CompressedInt32Array);
                TypeDesc desc{.kind = TypeDesc::Kind::IntArray};
                return encode_payload_by_desc(w, *this, desc, Value(a), depth);
            } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                w.write_u8(TC_CompressedInt64Array);
                TypeDesc desc{.kind = TypeDesc::Kind::LongArray};
                return encode_payload_by_desc(w, *this, desc, Value(a), depth);
            } else if constexpr (std::is_same_v<T, std::vector<float>>) {
                w.write_u8(TC_FloatArray);
                TypeDesc desc{.kind = TypeDesc::Kind::FloatArray};
                return encode_payload_by_desc(w, *this, desc, Value(a), depth);
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                w.write_u8(TC_DoubleArray);
                TypeDesc desc{.kind = TypeDesc::Kind::DoubleArray};
                return encode_payload_by_desc(w, *this, desc, Value(a), depth);
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                w.write_u8(TC_StringArray);
                TypeDesc desc{.kind = TypeDesc::Kind::StringArray};
                return encode_payload_by_desc(w, *this, desc, Value(a), depth);
            } else if constexpr (std::is_same_v<T, ObjectArray>) {
                w.write_u8(TC_ObjectArray);
                return encode_object_array_payload(w, *this, a, depth);
            } else if constexpr (std::is_same_v<T, JaggedArray>) {
                w.write_u8(TC_ArrayRecursive);
                return encode_jagged_array_payload(w, *this, a, depth);
            } else if constexpr (std::is_same_v<T, Dictionary>) {
                w.write_u8(TC_Dictionary);
                w.write_u8(TC_Byte);
                w.write_u8(0x00);
                return encode_convenience_dictionary_payload(w, *this, a, depth);
            } else if constexpr (std::is_same_v<T, GenericDictionary>) {
                auto hdr = parse_dict_header_bytes(a.header);
                if (!hdr)
                    return std::unexpected(hdr.error());

                w.write_u8(TC_Dictionary);
                w.write_bytes(a.header);
                return encode_generic_dictionary_payload(w, *this, *hdr, a, depth);
            } else if constexpr (std::is_same_v<T, HashtablePtr>) {
                w.write_u8(TC_Hashtable);
                return encode_hashtable_payload(w, *this, a, depth);
            } else if constexpr (std::is_same_v<T, RawCustomValue>) {
                if (a.custom_code < 100) {
                    w.write_u8(static_cast<uint8_t>(128 + a.custom_code));
                    auto rc = write_size32(w, a.data.size(), "custom payload length");
                    if (!rc)
                        return std::unexpected(rc.error());
                    w.write_bytes(a.data);
                    return {};
                }

                w.write_u8(TC_Custom_Explicit);
                w.write_u8(a.custom_code);
                auto rc = write_size32(w, a.data.size(), "custom payload length");
                if (!rc)
                    return std::unexpected(rc.error());
                w.write_bytes(a.data);
                return {};
            } else if constexpr (std::is_same_v<T, EventMessage>) {
                w.write_u8(TC_EventData);
                w.write_u8(a.event_code);
                return encode_parameters(w, a.parameters, depth + 1);
            } else if constexpr (std::is_same_v<T, OperationRequestMessage>) {
                w.write_u8(TC_OperationRequest);
                w.write_u8(a.operation_code);
                return encode_parameters(w, a.parameters, depth + 1);
            } else if constexpr (std::is_same_v<T, OperationResponseMessage>) {
                w.write_u8(TC_OperationResponse);
                w.write_u8(a.operation_code);
                w.write_i16_le(a.return_code);

                auto dbg = encode_debug_field_null_or_string(w, a.debug_message);
                if (!dbg)
                    return std::unexpected(dbg.error());

                return encode_parameters(w, a.parameters, depth + 1);
            } else if constexpr (std::is_same_v<T, std::vector<Dictionary>>) {
                w.write_u8(TC_DictionaryArray);
                w.write_u8(TC_Byte);
                w.write_u8(0x00);

                auto rc = write_size32(w, a.size(), "dictionary array count");
                if (!rc)
                    return std::unexpected(rc.error());

                for (const auto& d : a) {
                    auto ed = encode_convenience_dictionary_payload(w, *this, d, depth + 1);
                    if (!ed)
                        return std::unexpected(ed.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<GenericDictionary>>) {
                if (a.empty()) {
                    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "cannot encode empty generic dictionary array without header"});
                }

                auto hdr = parse_dict_header_bytes(a.front().header);
                if (!hdr)
                    return std::unexpected(hdr.error());

                for (const auto& d : a) {
                    if (d.header != a.front().header) {
                        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "dictionary array contains mismatched headers"});
                    }
                }

                w.write_u8(TC_DictionaryArray);
                w.write_bytes(a.front().header);

                auto rc = write_size32(w, a.size(), "dictionary array count");
                if (!rc)
                    return std::unexpected(rc.error());

                for (const auto& d : a) {
                    auto ed = encode_generic_dictionary_payload(w, *this, *hdr, d, depth + 1);
                    if (!ed)
                        return std::unexpected(ed.error());
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<HashtablePtr>>) {
                w.write_u8(TC_HashtableArray);
                return encode_hashtable_array_payload(w, *this, a, depth);
            } else if constexpr (std::is_same_v<T, std::vector<RawCustomValue>>) {
                if (a.empty()) {
                    return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "cannot encode empty custom type array without element type"});
                }

                const uint8_t custom_code = a.front().custom_code;
                for (const auto& e : a) {
                    if (e.custom_code != custom_code) {
                        return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "custom type array contains mixed custom codes"});
                    }
                }

                w.write_u8(TC_CustomTypeArray);
                auto rc = write_size32(w, a.size(), "custom type array count");
                if (!rc)
                    return std::unexpected(rc.error());

                w.write_u8(custom_code);
                for (const auto& e : a) {
                    auto rs = write_size32(w, e.data.size(), "custom element length");
                    if (!rs)
                        return std::unexpected(rs.error());
                    w.write_bytes(e.data);
                }
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

    const uint8_t t = *tc;

    if (t >= 128 && t <= 228) {
        auto size = r.read_varuint32();
        if (!size)
            return std::unexpected(size.error());

        auto bytes = r.read_span(static_cast<std::size_t>(*size));
        if (!bytes)
            return std::unexpected(bytes.error());

        return Value(RawCustomValue{
            .custom_code = static_cast<uint8_t>(t - 128),
            .data = ByteArray(bytes->begin(), bytes->end()),
        });
    }

    switch (t) {
    case TC_Boolean: {
        auto b = r.read_u8();
        if (!b)
            return std::unexpected(b.error());
        return Value(*b != 0);
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
        auto s = read_string_v18(r);
        if (!s)
            return std::unexpected(s.error());
        return Value(std::move(*s));
    }

    case TC_Null:
        return Value(std::monostate{});

    case TC_CompressedInt32: {
        auto u = r.read_varuint32();
        if (!u)
            return std::unexpected(u.error());
        return Value(zigzag_decode_32(*u));
    }

    case TC_CompressedInt64: {
        auto u = r.read_varuint64();
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

        auto size = r.read_varuint32();
        if (!size)
            return std::unexpected(size.error());

        auto bytes = r.read_span(static_cast<std::size_t>(*size));
        if (!bytes)
            return std::unexpected(bytes.error());

        return Value(RawCustomValue{
            .custom_code = *code,
            .data = ByteArray(bytes->begin(), bytes->end()),
        });
    }

    case TC_ObjectArray: {
        auto a = decode_object_array_payload(r, *this, depth);
        if (!a)
            return std::unexpected(a.error());
        return Value(std::move(*a));
    }

    case TC_ArrayRecursive: {
        auto a = decode_jagged_array_payload(r, *this, depth);
        if (!a)
            return std::unexpected(a.error());
        return Value(std::move(*a));
    }

    case TC_Dictionary: {
        auto hdr = read_dict_header(r);
        if (!hdr)
            return std::unexpected(hdr.error());
        return decode_dictionary_payload_as_value(r, *this, *hdr, depth);
    }

    case TC_Hashtable: {
        auto ht = decode_hashtable_payload(r, *this, depth);
        if (!ht)
            return std::unexpected(ht.error());
        return Value(std::move(*ht));
    }

    case TC_BooleanArray: {
        TypeDesc desc{.kind = TypeDesc::Kind::BooleanArray};
        return decode_payload_by_desc(r, *this, desc, depth);
    }

    case TC_ByteArray: {
        TypeDesc desc{.kind = TypeDesc::Kind::ByteArray};
        return decode_payload_by_desc(r, *this, desc, depth);
    }

    case TC_Int16Array: {
        TypeDesc desc{.kind = TypeDesc::Kind::ShortArray};
        return decode_payload_by_desc(r, *this, desc, depth);
    }

    case TC_CompressedInt32Array: {
        TypeDesc desc{.kind = TypeDesc::Kind::IntArray};
        return decode_payload_by_desc(r, *this, desc, depth);
    }

    case TC_CompressedInt64Array: {
        TypeDesc desc{.kind = TypeDesc::Kind::LongArray};
        return decode_payload_by_desc(r, *this, desc, depth);
    }

    case TC_FloatArray: {
        TypeDesc desc{.kind = TypeDesc::Kind::FloatArray};
        return decode_payload_by_desc(r, *this, desc, depth);
    }

    case TC_DoubleArray: {
        TypeDesc desc{.kind = TypeDesc::Kind::DoubleArray};
        return decode_payload_by_desc(r, *this, desc, depth);
    }

    case TC_StringArray: {
        TypeDesc desc{.kind = TypeDesc::Kind::StringArray};
        return decode_payload_by_desc(r, *this, desc, depth);
    }

    case TC_OperationRequest: {
        auto op = r.read_u8();
        if (!op)
            return std::unexpected(op.error());

        auto params = decode_parameters(r, depth + 1);
        if (!params)
            return std::unexpected(params.error());

        return Value(OperationRequestMessage{.operation_code = *op, .parameters = std::move(*params)});
    }

    case TC_OperationResponse: {
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
            auto s = read_string_v18(r);
            if (!s)
                return std::unexpected(s.error());
            debug_message = std::move(*s);
        } else if (*debug_type == TC_Null) {
            debug_message.reset();
        } else {
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "invalid operation response debug type"});
        }

        auto params = decode_parameters(r, depth + 1);
        if (!params)
            return std::unexpected(params.error());

        return Value(OperationResponseMessage{
            .operation_code = *op,
            .return_code = *rc,
            .debug_message = std::move(debug_message),
            .parameters = std::move(*params),
        });
    }

    case TC_EventData: {
        auto code = r.read_u8();
        if (!code)
            return std::unexpected(code.error());

        auto params = decode_parameters(r, depth + 1);
        if (!params)
            return std::unexpected(params.error());

        return Value(EventMessage{.event_code = *code, .parameters = std::move(*params)});
    }

    case TC_HashtableArray: {
        auto a = decode_hashtable_array_payload(r, *this, depth);
        if (!a)
            return std::unexpected(a.error());
        return Value(std::move(*a));
    }

    case TC_DictionaryArray: {
        auto hdr = read_dict_header(r);
        if (!hdr)
            return std::unexpected(hdr.error());

        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());

        if (is_byte_object_dict_header(*hdr)) {
            std::vector<Dictionary> out;
            out.reserve(static_cast<std::size_t>(*count));
            for (uint32_t i = 0; i < *count; ++i) {
                auto d = decode_convenience_dictionary_payload(r, *this, depth + 1);
                if (!d)
                    return std::unexpected(d.error());
                out.push_back(std::move(*d));
            }
            return Value(std::move(out));
        }

        std::vector<GenericDictionary> out;
        out.reserve(static_cast<std::size_t>(*count));
        for (uint32_t i = 0; i < *count; ++i) {
            auto d = decode_generic_dictionary_payload(r, *this, *hdr, depth + 1);
            if (!d)
                return std::unexpected(d.error());
            out.push_back(std::move(*d));
        }
        return Value(std::move(out));
    }

    case TC_CustomTypeArray: {
        auto count = r.read_varuint32();
        if (!count)
            return std::unexpected(count.error());

        auto code = r.read_u8();
        if (!code)
            return std::unexpected(code.error());

        std::vector<RawCustomValue> out;
        out.reserve(static_cast<std::size_t>(*count));

        for (uint32_t i = 0; i < *count; ++i) {
            auto size = r.read_varuint32();
            if (!size)
                return std::unexpected(size.error());

            auto bytes = r.read_span(static_cast<std::size_t>(*size));
            if (!bytes)
                return std::unexpected(bytes.error());

            out.push_back(RawCustomValue{
                .custom_code = *code,
                .data = ByteArray(bytes->begin(), bytes->end()),
            });
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
                const std::size_t n = std::min<std::size_t>(app.size(), m.app_id.size());
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

                auto dbg = encode_debug_field_null_or_string(payload, m.debug_message);
                if (!dbg)
                    return std::unexpected(dbg.error());

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
                    auto rs = write_string_v18(payload, *m.message);
                    if (!rs)
                        return std::unexpected(rs.error());
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

                auto dbg = encode_debug_field_null_or_string(payload, m.debug_message);
                if (!dbg)
                    return std::unexpected(dbg.error());

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

    const uint8_t b1 = packet_bytes[1];
    const bool encrypted = (b1 & 0x80) != 0;
    const Kind kind = static_cast<Kind>(b1 & 0x7F);

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

        if (r.remaining() != 0)
            return std::unexpected(Error{.code = Error::Code::InvalidValue, .message = "init extra bytes"});

        return Message(InitMessage{
            .protocol_major = *protocol_major,
            .protocol_minor = *protocol_minor,
            .client_sdk_id = static_cast<uint8_t>((*sdk_enc) >> 1),
            .ipv6 = ((*vcombined) & 0x80) != 0,
            .version_major = static_cast<uint8_t>(((*vcombined) >> 4) & 0x07),
            .version_minor = static_cast<uint8_t>((*vcombined) & 0x0F),
            .version_patch = *patch,
            .version_revision = *rev,
            .app_id = std::move(app_id),
        });
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
            auto s = read_string_v18(r);
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
            auto s = read_string_v18(r);
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

    case Kind::RawMessage:
        return Message(RawMessage{.bytes = ByteArray(plaintext->begin(), plaintext->end())}, encrypted);

    default:
        return std::unexpected(Error{.code = Error::Code::UnsupportedKind, .message = "unsupported message kind"});
    }
}
} // namespace luxon::ser
