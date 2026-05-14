// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "ser_ipc_binary.hpp"

namespace luxon::ser {
ProtocolImplID IPCBinaryProtocol::GetProtcolImplID() { return ProtocolImplID::IPCBinary; }

std::expected<ByteArray, Error> IPCBinaryProtocol::Serialize(const Message& message) {
    ByteWriter w;

    w.write_u8(0xF5);
    w.write_u8(static_cast<uint8_t>(message.index())); // Message variant index
    w.write_u8(message.encrypted ? 1 : 0);             // Carry encrypted flag

    auto res = std::visit(
        [&](auto&& arg) -> std::expected<void, Error> {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, InitMessage>) {
                w.write_u8(arg.protocol_major);
                w.write_u8(arg.protocol_minor);
                w.write_u8(arg.client_sdk_id);
                w.write_u8(arg.ipv6 ? 1 : 0);
                w.write_u8(arg.version_major);
                w.write_u8(arg.version_minor);
                w.write_u8(arg.version_patch);
                w.write_u8(arg.version_revision);
                encode_string(w, arg.app_id);
            } else if constexpr (std::is_same_v<T, InitResponseMessage>) {
                // Empty payload
            } else if constexpr (std::is_same_v<T, OperationRequestMessage> || std::is_same_v<T, InternalOperationRequestMessage>) {
                return encode_op_req(w, arg, 0);
            } else if constexpr (std::is_same_v<T, OperationResponseMessage> || std::is_same_v<T, InternalOperationResponseMessage>) {
                return encode_op_resp(w, arg, 0);
            } else if constexpr (std::is_same_v<T, EventMessage>) {
                return encode_event(w, arg, 0);
            } else if constexpr (std::is_same_v<T, DisconnectMessage>) {
                w.write_i16_le(arg.code);
                w.write_u8(arg.message.has_value() ? 1 : 0);
                if (arg.message)
                    encode_string(w, *arg.message);
                return encode_dict(w, arg.parameters, 0);
            } else if constexpr (std::is_same_v<T, GenericValueMessage>) {
                return EncodeValue(w, arg.value, 0);
            } else if constexpr (std::is_same_v<T, RawMessage>) {
                w.write_u32_le(static_cast<uint32_t>(arg.bytes.size()));
                w.write_bytes(arg.bytes);
            }
            return {};
        },
        static_cast<const MessageVariant&>(message));

    if (!res)
        return std::unexpected(res.error());
    return w.take();
}

std::expected<Message, Error> IPCBinaryProtocol::Deserialize(std::span<const uint8_t> packet_bytes) {
    ByteReader r(packet_bytes);

    auto magic = r.read_u8();
    if (!magic || *magic != GP_MAGIC)
        return std::unexpected(Error{.code = Error::Code::BadMagic, .message = "Invalid magic for IPCBinary"});

    auto msg_idx = r.read_u8();
    if (!msg_idx)
        return std::unexpected(msg_idx.error());

    auto enc_flag = r.read_u8();
    if (!enc_flag)
        return std::unexpected(enc_flag.error());
    bool is_encrypted = (*enc_flag != 0);

    Message msg;
    msg.encrypted = is_encrypted;

    switch (*msg_idx) {
    case 0: { // InitMessage
        InitMessage im;
        if (auto v = r.read_u8(); v)
            im.protocol_major = *v;
        else
            return std::unexpected(v.error());
        if (auto v = r.read_u8(); v)
            im.protocol_minor = *v;
        else
            return std::unexpected(v.error());
        if (auto v = r.read_u8(); v)
            im.client_sdk_id = *v;
        else
            return std::unexpected(v.error());
        if (auto v = r.read_u8(); v)
            im.ipv6 = (*v != 0);
        else
            return std::unexpected(v.error());
        if (auto v = r.read_u8(); v)
            im.version_major = *v;
        else
            return std::unexpected(v.error());
        if (auto v = r.read_u8(); v)
            im.version_minor = *v;
        else
            return std::unexpected(v.error());
        if (auto v = r.read_u8(); v)
            im.version_patch = *v;
        else
            return std::unexpected(v.error());
        if (auto v = r.read_u8(); v)
            im.version_revision = *v;
        else
            return std::unexpected(v.error());
        if (auto str = decode_string(r); str)
            im.app_id = std::move(*str);
        else
            return std::unexpected(str.error());
        msg = std::move(im);
        break;
    }
    case 1:
        msg = InitResponseMessage{};
        break;
    case 2: {
        if (auto v = decode_op_req<OperationRequestMessage>(r, 0); v)
            msg = std::move(*v);
        else
            return std::unexpected(v.error());
        break;
    }
    case 3: {
        if (auto v = decode_op_resp<OperationResponseMessage>(r, 0); v)
            msg = std::move(*v);
        else
            return std::unexpected(v.error());
        break;
    }
    case 4: {
        if (auto v = decode_event(r, 0); v)
            msg = std::move(*v);
        else
            return std::unexpected(v.error());
        break;
    }
    case 5: { // DisconnectMessage
        DisconnectMessage dm;
        if (auto v = r.read_i16_le(); v)
            dm.code = *v;
        else
            return std::unexpected(v.error());
        auto has_msg = r.read_u8();
        if (!has_msg)
            return std::unexpected(has_msg.error());
        if (*has_msg) {
            if (auto str = decode_string(r); str)
                dm.message = std::move(*str);
            else
                return std::unexpected(str.error());
        }
        if (auto dict = decode_dict(r, 0); dict)
            dm.parameters = std::move(*dict);
        else
            return std::unexpected(dict.error());
        msg = std::move(dm);
        break;
    }
    case 6: {
        if (auto v = decode_op_req<InternalOperationRequestMessage>(r, 0); v)
            msg = std::move(*v);
        else
            return std::unexpected(v.error());
        break;
    }
    case 7: {
        if (auto v = decode_op_resp<InternalOperationResponseMessage>(r, 0); v)
            msg = std::move(*v);
        else
            return std::unexpected(v.error());
        break;
    }
    case 8: { // GenericValueMessage
        if (auto val = DecodeValue(r, 0); val)
            msg = GenericValueMessage{std::move(*val)};
        else
            return std::unexpected(val.error());
        break;
    }
    case 9: { // RawMessage
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        auto s = r.read_span(*len);
        if (!s)
            return std::unexpected(s.error());
        msg = RawMessage{ByteArray(s->begin(), s->end())};
        break;
    }
    default:
        return std::unexpected(Error{.code = Error::Code::UnsupportedKind, .message = "Unknown message variant in IPCBinary"});
    }

    msg.encrypted = is_encrypted;
    return msg;
}

std::expected<void, Error> IPCBinaryProtocol::EncodeValue(ByteWriter& w, const Value& v, int depth) const {
    if (depth > 64)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "Nesting too deep"});

    w.write_u8(static_cast<uint8_t>(v.value.index()));

    return std::visit(
        [&](auto&& arg) -> std::expected<void, Error> {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return {};
            else if constexpr (std::is_same_v<T, bool>) {
                w.write_u8(arg ? 1 : 0);
                return {};
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                w.write_u8(arg);
                return {};
            } else if constexpr (std::is_same_v<T, int16_t>) {
                w.write_i16_le(arg);
                return {};
            } else if constexpr (std::is_same_v<T, int32_t>) {
                w.write_i32_le(arg);
                return {};
            } else if constexpr (std::is_same_v<T, int64_t>) {
                w.write_i64_le(arg);
                return {};
            } else if constexpr (std::is_same_v<T, float>) {
                w.write_f32_le(arg);
                return {};
            } else if constexpr (std::is_same_v<T, double>) {
                w.write_f64_le(arg);
                return {};
            } else if constexpr (std::is_same_v<T, std::string>) {
                encode_string(w, arg);
                return {};
            } else if constexpr (std::is_same_v<T, ByteArray>) {
                encode_bytearray(w, arg);
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
                w.write_u32_le(static_cast<uint32_t>(arg.size()));
                for (bool b : arg)
                    w.write_u8(b ? 1 : 0);
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<int16_t>>)
                return encode_pod_vector(w, arg);
            else if constexpr (std::is_same_v<T, std::vector<int32_t>>)
                return encode_pod_vector(w, arg);
            else if constexpr (std::is_same_v<T, std::vector<int64_t>>)
                return encode_pod_vector(w, arg);
            else if constexpr (std::is_same_v<T, std::vector<float>>)
                return encode_pod_vector(w, arg);
            else if constexpr (std::is_same_v<T, std::vector<double>>)
                return encode_pod_vector(w, arg);
            else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                w.write_u32_le(static_cast<uint32_t>(arg.size()));
                for (const auto& s : arg)
                    encode_string(w, s);
                return {};
            } else if constexpr (std::is_same_v<T, ObjectArray>) {
                w.write_u32_le(static_cast<uint32_t>(arg.size()));
                for (const auto& val : arg)
                    if (auto err = EncodeValue(w, val, depth + 1); !err)
                        return err;
                return {};
            } else if constexpr (std::is_same_v<T, JaggedArray>) {
                w.write_u32_le(static_cast<uint32_t>(arg.elements.size()));
                for (const auto& val : arg.elements)
                    if (auto err = EncodeValue(w, val, depth + 1); !err)
                        return err;
                return {};
            } else if constexpr (std::is_same_v<T, Dictionary>)
                return encode_dict(w, arg, depth);
            else if constexpr (std::is_same_v<T, GenericDictionary>) {
                encode_bytearray(w, arg.header);
                w.write_u32_le(static_cast<uint32_t>(arg.entries.size()));
                for (const auto& [k, val] : arg.entries) {
                    if (auto err = EncodeValue(w, k, depth + 1); !err)
                        return err;
                    if (auto err = EncodeValue(w, val, depth + 1); !err)
                        return err;
                }
                return {};
            } else if constexpr (std::is_same_v<T, HashtablePtr>) {
                if (!arg) {
                    w.write_u32_le(0xFFFFFFFF);
                    return {};
                }
                w.write_u32_le(static_cast<uint32_t>(arg->size()));
                for (const auto& [k, val] : *arg) {
                    if (auto err = EncodeValue(w, k, depth + 1); !err)
                        return err;
                    if (auto err = EncodeValue(w, val, depth + 1); !err)
                        return err;
                }
                return {};
            } else if constexpr (std::is_same_v<T, RawCustomValue>) {
                w.write_u8(arg.custom_code);
                encode_bytearray(w, arg.data);
                return {};
            } else if constexpr (std::is_same_v<T, EventMessage>)
                return encode_event(w, arg, depth);
            else if constexpr (std::is_same_v<T, OperationRequestMessage>)
                return encode_op_req(w, arg, depth);
            else if constexpr (std::is_same_v<T, OperationResponseMessage>)
                return encode_op_resp(w, arg, depth);
            else if constexpr (std::is_same_v<T, std::vector<Dictionary>>) {
                w.write_u32_le(static_cast<uint32_t>(arg.size()));
                for (const auto& item : arg)
                    if (auto err = encode_dict(w, item, depth + 1); !err)
                        return err;
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<GenericDictionary>>) {
                w.write_u32_le(static_cast<uint32_t>(arg.size()));
                for (const auto& item : arg) {
                    encode_bytearray(w, item.header);
                    w.write_u32_le(static_cast<uint32_t>(item.entries.size()));
                    for (const auto& [k, val] : item.entries) {
                        if (auto err = EncodeValue(w, k, depth + 1); !err)
                            return err;
                        if (auto err = EncodeValue(w, val, depth + 1); !err)
                            return err;
                    }
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<HashtablePtr>>) {
                w.write_u32_le(static_cast<uint32_t>(arg.size()));
                for (const auto& ht : arg) {
                    if (!ht) {
                        w.write_u32_le(0xFFFFFFFF);
                        continue;
                    } // Sentinel
                    w.write_u32_le(static_cast<uint32_t>(ht->size()));
                    for (const auto& [k, val] : *ht) {
                        if (auto err = EncodeValue(w, k, depth + 1); !err)
                            return err;
                        if (auto err = EncodeValue(w, val, depth + 1); !err)
                            return err;
                    }
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::vector<RawCustomValue>>) {
                w.write_u32_le(static_cast<uint32_t>(arg.size()));
                for (const auto& cv : arg) {
                    w.write_u8(cv.custom_code);
                    encode_bytearray(w, cv.data);
                }
                return {};
            } else if constexpr (std::is_same_v<T, PreSerializedValue>) {
                encode_bytearray(w, arg.data);
                return {};
            }
            return std::unexpected(Error{.code = Error::Code::UnsupportedTypeCode, .message = "Unknown value type"});
        },
        v.value);
}

std::expected<Value, Error> IPCBinaryProtocol::DecodeValue(ByteReader& r, int depth) const {
    if (depth > 64)
        return std::unexpected(Error{.code = Error::Code::DepthLimit, .message = "Nesting too deep"});

    auto type_tag = r.read_u8();
    if (!type_tag)
        return std::unexpected(type_tag.error());

    switch (*type_tag) {
    case 0:
        return Value{};
    case 1: {
        auto v = r.read_u8();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v != 0);
    }
    case 2: {
        auto v = r.read_u8();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case 3: {
        auto v = r.read_i16_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case 4: {
        auto v = r.read_i32_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case 5: {
        auto v = r.read_i64_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case 6: {
        auto v = r.read_f32_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case 7: {
        auto v = r.read_f64_le();
        if (!v)
            return std::unexpected(v.error());
        return Value(*v);
    }
    case 8: {
        auto v = decode_string(r);
        if (!v)
            return std::unexpected(v.error());
        return Value(std::move(*v));
    }
    case 9: {
        auto v = decode_bytearray(r);
        if (!v)
            return std::unexpected(v.error());
        return Value(std::move(*v));
    }
    case 10: {
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        std::vector<bool> vec;
        vec.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            auto b = r.read_u8();
            if (!b)
                return std::unexpected(b.error());
            vec.push_back(*b != 0);
        }
        return Value(std::move(vec));
    }
    case 11:
        return decode_pod_vector<int16_t>(r);
    case 12:
        return decode_pod_vector<int32_t>(r);
    case 13:
        return decode_pod_vector<int64_t>(r);
    case 14:
        return decode_pod_vector<float>(r);
    case 15:
        return decode_pod_vector<double>(r);
    case 16: {
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        std::vector<std::string> vec;
        vec.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            auto s = decode_string(r);
            if (!s)
                return std::unexpected(s.error());
            vec.push_back(std::move(*s));
        }
        return Value(std::move(vec));
    }
    case 17: { // ObjectArray
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        ObjectArray vec;
        vec.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            auto val = DecodeValue(r, depth + 1);
            if (!val)
                return std::unexpected(val.error());
            vec.push_back(std::move(*val));
        }
        return Value(std::move(vec));
    }
    case 18: { // JaggedArray
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        JaggedArray vec;
        vec.elements.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            auto val = DecodeValue(r, depth + 1);
            if (!val)
                return std::unexpected(val.error());
            vec.elements.push_back(std::move(*val));
        }
        return Value(std::move(vec));
    }
    case 19: { // Dictionary
        auto v = decode_dict(r, depth);
        if (!v)
            return std::unexpected(v.error());
        return Value(std::move(*v));
    }
    case 20: { // GenericDictionary
        GenericDictionary dict;
        auto hdr = decode_bytearray(r);
        if (!hdr)
            return std::unexpected(hdr.error());
        dict.header = std::move(*hdr);
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        dict.entries.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            auto k = DecodeValue(r, depth + 1);
            if (!k)
                return std::unexpected(k.error());
            auto val = DecodeValue(r, depth + 1);
            if (!val)
                return std::unexpected(val.error());
            dict.entries.emplace_back(std::move(*k), std::move(*val));
        }
        return Value(std::move(dict));
    }
    case 21: { // Hashtable
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        if (*len == 0xFFFFFFFF)
            return Value(HashtablePtr{}); // Reproduce nullptr exactly

        HashtablePtr ht = std::make_shared<Hashtable>();
        for (uint32_t i = 0; i < *len; ++i) {
            auto k = DecodeValue(r, depth + 1);
            if (!k)
                return std::unexpected(k.error());
            auto val = DecodeValue(r, depth + 1);
            if (!val)
                return std::unexpected(val.error());
            ht->emplace(std::move(*k), std::move(*val));
        }
        return Value(std::move(ht));
    }
    case 22: { // RawCustomValue
        RawCustomValue rv;
        auto c = r.read_u8();
        if (!c)
            return std::unexpected(c.error());
        rv.custom_code = *c;
        auto d = decode_bytearray(r);
        if (!d)
            return std::unexpected(d.error());
        rv.data = std::move(*d);
        return Value(std::move(rv));
    }
    case 23: {
        auto v = decode_event(r, depth);
        if (!v)
            return std::unexpected(v.error());
        return Value(std::move(*v));
    }
    case 24: {
        auto v = decode_op_req<OperationRequestMessage>(r, depth);
        if (!v)
            return std::unexpected(v.error());
        return Value(std::move(*v));
    }
    case 25: {
        auto v = decode_op_resp<OperationResponseMessage>(r, depth);
        if (!v)
            return std::unexpected(v.error());
        return Value(std::move(*v));
    }
    case 26: {
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        std::vector<Dictionary> vec;
        vec.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            auto dict = decode_dict(r, depth + 1);
            if (!dict)
                return std::unexpected(dict.error());
            vec.push_back(std::move(*dict));
        }
        return Value(std::move(vec));
    }
    case 27: {
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        std::vector<GenericDictionary> vec;
        vec.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            GenericDictionary dict;
            auto hdr = decode_bytearray(r);
            if (!hdr)
                return std::unexpected(hdr.error());
            dict.header = std::move(*hdr);
            auto elen = r.read_u32_le();
            if (!elen)
                return std::unexpected(elen.error());
            dict.entries.reserve(*elen);
            for (uint32_t j = 0; j < *elen; ++j) {
                auto k = DecodeValue(r, depth + 1);
                if (!k)
                    return std::unexpected(k.error());
                auto val = DecodeValue(r, depth + 1);
                if (!val)
                    return std::unexpected(val.error());
                dict.entries.emplace_back(std::move(*k), std::move(*val));
            }
            vec.push_back(std::move(dict));
        }
        return Value(std::move(vec));
    }
    case 28: {
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        std::vector<HashtablePtr> vec;
        vec.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            auto hlen = r.read_u32_le();
            if (!hlen)
                return std::unexpected(hlen.error());
            if (*hlen == 0xFFFFFFFF) {
                vec.push_back(HashtablePtr{});
            } else {
                HashtablePtr ht = std::make_shared<Hashtable>();
                for (uint32_t j = 0; j < *hlen; ++j) {
                    auto k = DecodeValue(r, depth + 1);
                    if (!k)
                        return std::unexpected(k.error());
                    auto val = DecodeValue(r, depth + 1);
                    if (!val)
                        return std::unexpected(val.error());
                    ht->emplace(std::move(*k), std::move(*val));
                }
                vec.push_back(std::move(ht));
            }
        }
        return Value(std::move(vec));
    }
    case 29: {
        auto len = r.read_u32_le();
        if (!len)
            return std::unexpected(len.error());
        std::vector<RawCustomValue> vec;
        vec.reserve(*len);
        for (uint32_t i = 0; i < *len; ++i) {
            RawCustomValue rv;
            auto c = r.read_u8();
            if (!c)
                return std::unexpected(c.error());
            rv.custom_code = *c;
            auto d = decode_bytearray(r);
            if (!d)
                return std::unexpected(d.error());
            rv.data = std::move(*d);
            vec.push_back(std::move(rv));
        }
        return Value(std::move(vec));
    }
    case 30: { // PreSerializedValue
        auto d = decode_bytearray(r);
        if (!d)
            return std::unexpected(d.error());
        return Value(PreSerializedValue{std::move(*d)});
    }
    default:
        return std::unexpected(Error{.code = Error::Code::UnsupportedTypeCode, .message = "Unknown type index"});
    }
}

std::expected<ByteArray, Error> IPCBinaryProtocol::CreateInitEncryptionRequest() {
    return std::unexpected(Error{.code = Error::Code::UnsupportedKind, .message = "IPCBinary does not support encryption."});
}

std::expected<void, Error> IPCBinaryProtocol::HandleInitEncryptionResponse(const InternalOperationResponseMessage&) {
    return std::unexpected(Error{.code = Error::Code::UnsupportedKind, .message = "IPCBinary does not support encryption."});
}

std::expected<InternalOperationResponseMessage, Error> IPCBinaryProtocol::HandleInitEncryptionRequest(const InternalOperationRequestMessage&) {
    return std::unexpected(Error{.code = Error::Code::UnsupportedKind, .message = "IPCBinary does not support encryption."});
}

void IPCBinaryProtocol::encode_string(ByteWriter& w, const std::string& str) {
    w.write_u32_le(static_cast<uint32_t>(str.size()));
    w.write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(str.data()), str.size()));
}

std::expected<std::string, Error> IPCBinaryProtocol::decode_string(ByteReader& r) {
    auto len = r.read_u32_le();
    if (!len)
        return std::unexpected(len.error());
    auto s = r.read_span(*len);
    if (!s)
        return std::unexpected(s.error());
    return std::string(reinterpret_cast<const char *>(s->data()), s->size());
}

void IPCBinaryProtocol::encode_bytearray(ByteWriter& w, const ByteArray& arr) {
    w.write_u32_le(static_cast<uint32_t>(arr.size()));
    w.write_bytes(arr);
}

std::expected<ByteArray, Error> IPCBinaryProtocol::decode_bytearray(ByteReader& r) {
    auto len = r.read_u32_le();
    if (!len)
        return std::unexpected(len.error());
    auto s = r.read_span(*len);
    if (!s)
        return std::unexpected(s.error());
    return ByteArray(s->begin(), s->end());
}

std::expected<void, Error> IPCBinaryProtocol::encode_dict(ByteWriter& w, const Dictionary& d, int depth) const {
    w.write_u32_le(static_cast<uint32_t>(d.size()));
    for (const auto& [k, val] : d) {
        w.write_u8(k);
        if (auto err = EncodeValue(w, val, depth + 1); !err)
            return err;
    }
    return {};
}

std::expected<Dictionary, Error> IPCBinaryProtocol::decode_dict(ByteReader& r, int depth) const {
    auto len = r.read_u32_le();
    if (!len)
        return std::unexpected(len.error());
    Dictionary dict;
    for (uint32_t i = 0; i < *len; ++i) {
        auto k = r.read_u8();
        if (!k)
            return std::unexpected(k.error());
        auto val = DecodeValue(r, depth + 1);
        if (!val)
            return std::unexpected(val.error());
        dict[*k] = std::move(*val);
    }
    return dict;
}

std::expected<void, Error> IPCBinaryProtocol::encode_event(ByteWriter& w, const EventMessage& ev, int depth) const {
    w.write_u8(ev.event_code);
    return encode_dict(w, ev.parameters, depth);
}

std::expected<EventMessage, Error> IPCBinaryProtocol::decode_event(ByteReader& r, int depth) const {
    EventMessage ev;
    auto code = r.read_u8();
    if (!code)
        return std::unexpected(code.error());
    ev.event_code = *code;
    auto dict = decode_dict(r, depth);
    if (!dict)
        return std::unexpected(dict.error());
    ev.parameters = std::move(*dict);
    return ev;
}
} // namespace luxon::ser
