#pragma once

#include "flat_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace luxon::ser {
using ByteArray = std::vector<uint8_t>;

struct RawCustomValue {
    uint8_t custom_code{};
    ByteArray data;

    bool operator==(const RawCustomValue& other) const = default;
};

struct PreSerializedValue {
    ByteArray data;

    bool operator==(const PreSerializedValue& other) const = default;
};

struct Value;
extern const Value null;

struct ValueHash {
    std::size_t operator()(const Value& v) const noexcept;
};

using Hashtable = std::unordered_map<Value, Value, ValueHash>;
using HashtablePtr = std::shared_ptr<Hashtable>;

using ObjectArray = std::vector<Value>;

class Dictionary : public flat_map<uint8_t, Value> {
public:
    using flat_map<uint8_t, Value>::flat_map;
    using flat_map<uint8_t, Value>::operator[];

    Dictionary();
    ~Dictionary();
    Dictionary(const Dictionary&);
    Dictionary(Dictionary&&) noexcept;
    Dictionary& operator=(const Dictionary&);
    Dictionary& operator=(Dictionary&&) noexcept;

    const Value& operator[](uint8_t key) const;
};

using ParameterList = Dictionary;

struct JaggedArray {
    std::vector<Value> elements{};
    bool operator==(const JaggedArray& other) const;
};

struct GenericDictionary {
    ByteArray header{};
    std::vector<std::pair<Value, Value>> entries{};

    GenericDictionary();
    ~GenericDictionary();
    GenericDictionary(const GenericDictionary&);
    GenericDictionary(GenericDictionary&&) noexcept;
    GenericDictionary& operator=(const GenericDictionary&);
    GenericDictionary& operator=(GenericDictionary&&) noexcept;

    bool operator==(const GenericDictionary& other) const;
};

struct InitMessage {
    uint8_t protocol_major = 1;
    uint8_t protocol_minor = 8;
    uint8_t client_sdk_id = 15;
    bool ipv6{};
    uint8_t version_major = 4;
    uint8_t version_minor = 1;
    uint8_t version_patch = 6;
    uint8_t version_revision = 8;
    std::string app_id{};
};

struct InitResponseMessage {};

struct EventMessage {
    uint8_t event_code{};
    ParameterList parameters{};

    bool operator==(const EventMessage& other) const;
};

struct OperationRequestMessage {
    uint8_t operation_code{};
    ParameterList parameters{};

    bool operator==(const OperationRequestMessage& other) const;
};

struct OperationResponseMessage {
    uint8_t operation_code{};
    int16_t return_code{};
    std::optional<std::string> debug_message{};
    ParameterList parameters{};

    bool operator==(const OperationResponseMessage& other) const;
};

struct DisconnectMessage {
    int16_t code{};
    std::optional<std::string> message{};
    ParameterList parameters{};
};

struct InternalOperationRequestMessage {
    uint8_t operation_code{};
    ParameterList parameters{};
};

struct InternalOperationResponseMessage {
    uint8_t operation_code{};
    int16_t return_code{};
    std::optional<std::string> debug_message{};
    ParameterList parameters{};
};

struct Value {
    using VariantType = std::variant<std::monostate,                 // null
                                     bool,                           // boolean
                                     uint8_t,                        // byte
                                     int16_t,                        // short
                                     int32_t,                        // int
                                     int64_t,                        // long
                                     float,                          // float
                                     double,                         // double
                                     std::string,                    // string
                                     ByteArray,                      // byte array
                                     std::vector<bool>,              // boolean array
                                     std::vector<int16_t>,           // short array
                                     std::vector<int32_t>,           // int array
                                     std::vector<int64_t>,           // long array
                                     std::vector<float>,             // float array
                                     std::vector<double>,            // double array
                                     std::vector<std::string>,       // string array
                                     ObjectArray,                    // object array (0x17)
                                     JaggedArray,                    // array/jagged array (0x40)
                                     Dictionary,                     // convenience dictionary<byte,object>
                                     GenericDictionary,              // exact generic dictionary
                                     HashtablePtr,                   // hashtable
                                     RawCustomValue,                 // custom type
                                     EventMessage,                   // event data
                                     OperationRequestMessage,        // operation request
                                     OperationResponseMessage,       // operation response
                                     std::vector<Dictionary>,        // convenience dictionary array
                                     std::vector<GenericDictionary>, // exact generic dictionary array
                                     std::vector<HashtablePtr>,      // hashtable array
                                     std::vector<RawCustomValue>,    // custom type array
                                     PreSerializedValue              // value that is already serialized
                                     >;

    VariantType value;

    Value() : value(std::monostate{}) {}

    Value(const Value& other) = default;
    Value(Value&& other) noexcept = default;

    Value& operator=(const Hashtable& other);
    Value& operator=(Hashtable&& other);

    Value& operator=(const Value& other) = default;
    Value& operator=(Value&& other) noexcept = default;

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Value>>> Value(T&& v) : value(std::forward<T>(v)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(value); }

    template <typename T> bool is() const { return std::holds_alternative<T>(value); }

    template <typename T> const T& get() const { return std::get<T>(value); }

    template <typename T> T& get() { return std::get<T>(value); }

    template <typename T> T *get_ptr() { return std::get_if<T>(&value); }

    template <typename T> const T *get_ptr() const { return std::get_if<T>(&value); }

    template <typename T> bool is_equal(const T& target) const {
        if (const T *ptr = std::get_if<T>(&value)) {
            return target == *ptr;
        }
        return false;
    }

    template <typename T> bool store_if(T& target) const {
        if (const T *ptr = std::get_if<T>(&value)) {
            target = *ptr;
            return true;
        }
        return false;
    }

    template <typename T> T get_or() const {
        if (const T *ptr = std::get_if<T>(&value)) {
            return *ptr;
        }
        return T{};
    }

    template <typename T> const T& get_or(T& default_value) const {
        if (const T *ptr = std::get_if<T>(&value)) {
            return *ptr;
        }
        return default_value;
    }

    template <typename T> const T& get_or(const T& default_value) const {
        if (const T *ptr = std::get_if<T>(&value)) {
            return *ptr;
        }
        return default_value;
    }

    template <typename T> T get_or(T&& default_value) const {
        if (const T *ptr = std::get_if<T>(&value)) {
            return *ptr;
        }
        return std::forward<T>(default_value);
    }

    template <typename T> std::optional<T> get_optional() const {
        if (const T *ptr = std::get_if<T>(&value)) {
            return *ptr;
        }
        return std::nullopt;
    }

    bool operator==(const Value& other) const;
};

// --- Deferred Implementations (Prevents libc++ incomplete type instantiation errors) ---

inline Dictionary::Dictionary() = default;
inline Dictionary::~Dictionary() = default;
inline Dictionary::Dictionary(const Dictionary&) = default;
inline Dictionary::Dictionary(Dictionary&&) noexcept = default;
inline Dictionary& Dictionary::operator=(const Dictionary&) = default;
inline Dictionary& Dictionary::operator=(Dictionary&&) noexcept = default;

inline GenericDictionary::GenericDictionary() = default;
inline GenericDictionary::~GenericDictionary() = default;
inline GenericDictionary::GenericDictionary(const GenericDictionary&) = default;
inline GenericDictionary::GenericDictionary(GenericDictionary&&) noexcept = default;
inline GenericDictionary& GenericDictionary::operator=(const GenericDictionary&) = default;
inline GenericDictionary& GenericDictionary::operator=(GenericDictionary&&) noexcept = default;

inline bool JaggedArray::operator==(const JaggedArray& other) const = default;
inline bool GenericDictionary::operator==(const GenericDictionary& other) const = default;
inline bool EventMessage::operator==(const EventMessage& other) const = default;
inline bool OperationRequestMessage::operator==(const OperationRequestMessage& other) const = default;
inline bool OperationResponseMessage::operator==(const OperationResponseMessage& other) const = default;

inline Value& Value::operator=(const Hashtable& other) { return *this = std::make_shared<Hashtable>(other); }
inline Value& Value::operator=(Hashtable&& other) { return *this = std::make_shared<Hashtable>(std::move(other)); }

// --------------------------------------------------------------------------------------

struct GenericValueMessage {
    Value value{};
};

struct RawMessage {
    ByteArray bytes{};
};

using MessageVariant = std::variant<InitMessage, InitResponseMessage, OperationRequestMessage, OperationResponseMessage, EventMessage, DisconnectMessage,
                                    InternalOperationRequestMessage, InternalOperationResponseMessage, GenericValueMessage, RawMessage>;

struct Error {
    enum class Code : uint8_t {
        Ok = 0,
        BadPacket,
        BadMagic,
        UnsupportedKind,
        UnsupportedTypeCode,
        Truncated,
        InvalidValue,
        DepthLimit,
        CryptoNotReady,
        CryptoError,
        BadPadding,
        BadCiphertextLength,
        DhError,
        HandshakeError,
    };

    Code code{Code::Ok};
    std::string message{};
};

inline constexpr uint8_t GP_MAGIC = 0xF3;

enum class Kind : uint8_t {
    Init = 0,
    InitResponse = 1,
    Operation = 2,
    OperationResponse = 3,
    Event = 4,
    DisconnectMessage = 5,
    InternalOperationRequest = 6,
    InternalOperationResponse = 7,
    Message = 8,
    RawMessage = 9,
};

struct Message : public MessageVariant {
    bool encrypted = false;

    using MessageVariant::MessageVariant;

    Message() = default;

    Message(const Message&) = default;
    Message(Message&&) = default;
    Message& operator=(const Message&) = default;
    Message& operator=(Message&&) = default;

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Message> && std::is_constructible_v<MessageVariant, T>>>
    Message(T&& arg, bool encrypted = false) : MessageVariant(std::forward<T>(arg)), encrypted(encrypted) {}

    Message(const MessageVariant& v, bool encrypted = false) : MessageVariant(v), encrypted(encrypted) {}
    Message(MessageVariant&& v, bool encrypted = false) : MessageVariant(std::move(v)), encrypted(encrypted) {}

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Message> && std::is_assignable_v<MessageVariant&, T>>>
    Message& operator=(T&& arg) {
        MessageVariant::operator=(std::forward<T>(arg));
        return *this;
    }
};
} // namespace luxon::ser
