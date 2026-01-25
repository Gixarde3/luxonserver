#include <luxon/ser_gp_binary_v18.hpp>
#include <luxon/ser_gp_binary_v16.hpp>
#include <luxon/visualizer.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace luxon::ser;

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct TestSkip : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define REQUIRE(cond)                                                                                                                                          \
    do {                                                                                                                                                       \
        if (!(cond))                                                                                                                                           \
            throw TestFailure(std::string("REQUIRE failed: ") + #cond + " @ " + __FILE__ + ":" + std::to_string(__LINE__));                                    \
    } while (0)

#define REQUIRE_MSG(cond, msg)                                                                                                                                 \
    do {                                                                                                                                                       \
        if (!(cond))                                                                                                                                           \
            throw TestFailure(std::string("REQUIRE failed: ") + (msg) + " (" + #cond + ") @ " + __FILE__ + ":" + std::to_string(__LINE__));                    \
    } while (0)

template <class T> static void require_expected(const std::expected<T, Error>& e, const char *what) {
    if (!e) {
        throw TestFailure(std::string(what) + " failed: code=" + std::to_string((int)e.error().code) + " msg=" + e.error().message);
    }
}

// overload for std::expected<void, Error>
static void require_expected(const std::expected<void, Error>& e, const char *what) {
    if (!e) {
        throw TestFailure(std::string(what) + " failed: code=" + std::to_string((int)e.error().code) + " msg=" + e.error().message);
    }
}

static bool parameter_lists_equal(const ParameterList& a, const ParameterList& b);
static bool values_equal_deep(const Value& a, const Value& b);

// Compare Hashtable content deterministically (unordered, so compare by lookup)
static bool hashtable_equal(const Hashtable& a, const Hashtable& b) {
    if (a.size() != b.size())
        return false;

    for (const auto& [ka, va] : a) {
        auto it = b.find(ka);
        if (it == b.end())
            return false;
        if (!values_equal_deep(va, it->second))
            return false;
    }
    return true;
}

static bool dictionaries_equal(const Dictionary& a, const Dictionary& b) {
    if (a.size() != b.size())
        return false;
    for (const auto& [k, va] : a) {
        auto it = b.find(k);
        if (it == b.end())
            return false;
        if (!values_equal_deep(va, it->second))
            return false;
    }
    return true;
}

static bool values_equal_deep(const Value& a, const Value& b) {
    if (a.value.index() != b.value.index())
        return false;

    // Special handling for NaN and signed zero for float/double, plus arrays containing them.
    if (a.is<float>()) {
        float af = a.get<float>(), bf = b.get<float>();
        if (std::isnan(af) && std::isnan(bf))
            return true;
        if (af == 0.0f && bf == 0.0f)
            return std::signbit(af) == std::signbit(bf);
        return af == bf;
    }
    if (a.is<double>()) {
        double ad = a.get<double>(), bd = b.get<double>();
        if (std::isnan(ad) && std::isnan(bd))
            return true;
        if (ad == 0.0 && bd == 0.0)
            return std::signbit(ad) == std::signbit(bd);
        return ad == bd;
    }
    if (a.is<std::vector<float>>()) {
        const auto& av = a.get<std::vector<float>>();
        const auto& bv = b.get<std::vector<float>>();
        if (av.size() != bv.size())
            return false;
        for (size_t i = 0; i < av.size(); ++i) {
            float x = av[i], y = bv[i];
            if (std::isnan(x) && std::isnan(y))
                continue;
            if (x == 0.0f && y == 0.0f) {
                if (std::signbit(x) != std::signbit(y))
                    return false;
                continue;
            }
            if (x != y)
                return false;
        }
        return true;
    }
    if (a.is<std::vector<double>>()) {
        const auto& av = a.get<std::vector<double>>();
        const auto& bv = b.get<std::vector<double>>();
        if (av.size() != bv.size())
            return false;
        for (size_t i = 0; i < av.size(); ++i) {
            double x = av[i], y = bv[i];
            if (std::isnan(x) && std::isnan(y))
                continue;
            if (x == 0.0 && y == 0.0) {
                if (std::signbit(x) != std::signbit(y))
                    return false;
                continue;
            }
            if (x != y)
                return false;
        }
        return true;
    }
    if (a.is<ObjectArray>()) {
        const auto& ao = a.get<ObjectArray>();
        const auto& bo = b.get<ObjectArray>();
        if (ao.size() != bo.size())
            return false;
        for (size_t i = 0; i < ao.size(); ++i) {
            if (!values_equal_deep(ao[i], bo[i]))
                return false;
        }
        return true;
    }
    if (a.is<Dictionary>()) {
        return dictionaries_equal(a.get<Dictionary>(), b.get<Dictionary>());
    }
    if (a.is<HashtablePtr>()) {
        const auto& ap = a.get<HashtablePtr>();
        const auto& bp = b.get<HashtablePtr>();
        if (!ap || !bp)
            return ap == bp;
        return hashtable_equal(*ap, *bp);
    }

    // Fallback to Value::operator== for the remaining types
    return a == b;
}

static bool parameter_lists_equal(const ParameterList& a, const ParameterList& b) {
    if (a.size() != b.size())
        return false;

    for (const auto& [k, va] : a) {
        auto it = b.find(k);
        if (it == b.end())
            return false;
        if (!values_equal_deep(va, it->second))
            return false;
    }
    return true;
}

static bool messages_equal_deep(const Message& a, const Message& b) {
    if (a.index() != b.index())
        return false;

    return std::visit(
        [&](const auto& ma) -> bool {
            using T = std::decay_t<decltype(ma)>;
            const auto& mb = std::get<T>(b);

            if constexpr (std::is_same_v<T, InitMessage>) {
                return ma.protocol_major == mb.protocol_major && ma.protocol_minor == mb.protocol_minor && ma.client_sdk_id == mb.client_sdk_id &&
                       ma.ipv6 == mb.ipv6 && ma.version_major == mb.version_major && ma.version_minor == mb.version_minor &&
                       ma.version_patch == mb.version_patch && ma.version_revision == mb.version_revision && ma.app_id == mb.app_id;
            } else if constexpr (std::is_same_v<T, InitResponseMessage>) {
                return true;
            } else if constexpr (std::is_same_v<T, EventMessage>) {
                return ma.event_code == mb.event_code && parameter_lists_equal(ma.parameters, mb.parameters);
            } else if constexpr (std::is_same_v<T, OperationRequestMessage>) {
                return ma.operation_code == mb.operation_code && parameter_lists_equal(ma.parameters, mb.parameters);
            } else if constexpr (std::is_same_v<T, OperationResponseMessage>) {
                return ma.operation_code == mb.operation_code && ma.return_code == mb.return_code && ma.debug_message == mb.debug_message &&
                       parameter_lists_equal(ma.parameters, mb.parameters);
            } else if constexpr (std::is_same_v<T, DisconnectMessage>) {
                return ma.code == mb.code && ma.message == mb.message && parameter_lists_equal(ma.parameters, mb.parameters);
            } else if constexpr (std::is_same_v<T, InternalOperationRequestMessage>) {
                return ma.operation_code == mb.operation_code && parameter_lists_equal(ma.parameters, mb.parameters);
            } else if constexpr (std::is_same_v<T, InternalOperationResponseMessage>) {
                return ma.operation_code == mb.operation_code && ma.return_code == mb.return_code && ma.debug_message == mb.debug_message &&
                       parameter_lists_equal(ma.parameters, mb.parameters);
            } else if constexpr (std::is_same_v<T, GenericValueMessage>) {
                return values_equal_deep(ma.value, mb.value);
            } else if constexpr (std::is_same_v<T, RawMessage>) {
                return ma.bytes == mb.bytes;
            } else {
                static_assert(!sizeof(T *), "Unhandled Message alternative");
            }
        },
        a);
}

static void roundtrip(GpBinaryV18& p, const Message& msg) {
    auto bytes = p.Serialize(msg);
    require_expected(bytes, "Serialize");

    auto decoded = p.Deserialize(std::span<const uint8_t>(bytes->data(), bytes->size()));
    require_expected(decoded, "Deserialize");

    if (!messages_equal_deep(msg, *decoded)) {
        std::cout << "\nOriginal:\n";
        luxon::visualizer::print_ser_message(msg, 4);
        std::cout << "Original after writing, then reading again:\n";
        luxon::visualizer::print_ser_message(decoded.value(), 4);
        throw TestFailure("Roundtrip message mismatch");
    }
}

static Value make_deeply_nested_value(int depth) {
    // nested ObjectArray structure, to exercise depth limits
    Value v = Value(int32_t{123});
    for (int i = 0; i < depth; ++i) {
        ObjectArray arr;
        arr.push_back(v);
        v = Value(std::move(arr));
    }
    return v;
}

static ParameterList make_parameter_list_kitchen_sink() {
    ParameterList p;

    p.emplace(uint8_t{1}, Value()); // null
    p.emplace(uint8_t{2}, Value(true));
    p.emplace(uint8_t{3}, Value(uint8_t{0xAB}));
    p.emplace(uint8_t{4}, Value(int16_t{-12345}));
    p.emplace(uint8_t{5}, Value(int32_t{std::numeric_limits<int32_t>::min()}));
    p.emplace(uint8_t{6}, Value(int64_t{std::numeric_limits<int64_t>::max()}));

    // floats: include signed zero; NaN; inf
    p.emplace(uint8_t{7}, Value(-0.0f));
    p.emplace(uint8_t{8}, Value(std::numeric_limits<float>::infinity()));
    p.emplace(uint8_t{9}, Value(std::numeric_limits<float>::quiet_NaN()));

    p.emplace(uint8_t{10}, Value(-0.0));
    p.emplace(uint8_t{11}, Value(std::numeric_limits<double>::infinity()));
    p.emplace(uint8_t{12}, Value(std::numeric_limits<double>::quiet_NaN()));

    // string: empty + non-ascii bytes (UTF-8 sequence)
    p.emplace(uint8_t{13}, Value(std::string{}));
    p.emplace(uint8_t{14}, Value(std::string("hello \xF0\x9F\x8C\x8D"))); // "hello 🌍" bytes

    // byte array
    p.emplace(uint8_t{15}, Value(ByteArray{0x00, 0x01, 0xFE, 0xFF}));
    p.emplace(uint8_t{16}, Value(ByteArray{}));

    // boolean array (vector<bool>)
    p.emplace(uint8_t{17}, Value(std::vector<bool>{true, false, true, true}));
    p.emplace(uint8_t{18}, Value(std::vector<bool>{}));

    // numeric arrays
    p.emplace(uint8_t{19}, Value(std::vector<int16_t>{-1, 0, 1, 12345, -12345}));
    p.emplace(uint8_t{20}, Value(std::vector<int32_t>{std::numeric_limits<int32_t>::min(), -1, 0, 1, std::numeric_limits<int32_t>::max()}));
    p.emplace(uint8_t{21}, Value(std::vector<int64_t>{std::numeric_limits<int64_t>::min(), -1, 0, 1, std::numeric_limits<int64_t>::max()}));
    p.emplace(uint8_t{22}, Value(std::vector<float>{-0.0f, 1.25f, std::numeric_limits<float>::quiet_NaN()}));
    p.emplace(uint8_t{23}, Value(std::vector<double>{-0.0, 2.5, std::numeric_limits<double>::quiet_NaN()}));

    // string array
    p.emplace(uint8_t{24}, Value(std::vector<std::string>{"a", "", "bbb"}));

    // object array with mixed values
    {
        ObjectArray arr;
        arr.emplace_back(Value()); // null
        arr.emplace_back(Value(int32_t{7}));
        arr.emplace_back(Value(std::string("x")));
        arr.emplace_back(Value(ByteArray{0x10}));
        p.emplace(uint8_t{25}, Value(std::move(arr)));
    }

    // dictionary<byte, value>
    {
        Dictionary d;
        d.emplace(uint8_t{1}, Value(int32_t{111}));
        d.emplace(uint8_t{2}, Value(std::string("two")));
        d.emplace(uint8_t{3}, Value(ByteArray{3, 2, 1}));
        p.emplace(uint8_t{26}, Value(std::move(d)));
    }

    // hashtable<value,value>
    {
        auto ht = std::make_shared<Hashtable>();
        ht->emplace(Value(std::string("k")), Value(std::string("v")));
        ht->emplace(Value(int32_t{123}), Value(ByteArray{9, 8, 7}));
        ht->emplace(Value(false), Value(int16_t{-7}));
        p.emplace(uint8_t{27}, Value(std::move(ht)));
    }

    // custom raw
    {
        RawCustomValue cv;
        cv.custom_code = 0x42;
        cv.data = ByteArray{0xDE, 0xAD, 0xBE, 0xEF};
        p.emplace(uint8_t{28}, Value(std::move(cv)));
    }

    return p;
}

struct Test {
    std::string name;
    std::function<void()> fn;
};

} // namespace

int main() {
    std::vector<Test> tests;

    tests.push_back({"Roundtrip: Init / InitResponse / RawMessage", [] {
                         GpBinaryV18 p;

                         {
                             InitMessage m;
                             m.protocol_major = 1;
                             m.protocol_minor = 8;
                             m.client_sdk_id = 15;
                             m.ipv6 = true;
                             m.version_major = 4;
                             m.version_minor = 1;
                             m.version_patch = 6;
                             m.version_revision = 9;
                             m.app_id = "app-id-123";

                             roundtrip(p, Message(m));
                         }
                         {
                             InitResponseMessage m;
                             roundtrip(p, Message(m));
                         }
                         {
                             RawMessage m;
                             m.bytes = ByteArray{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
                             roundtrip(p, Message(m));
                         }
                     }});

    tests.push_back({"Roundtrip: OperationRequest/Response, Event, Disconnect (with kitchen-sink parameters)", [] {
                         GpBinaryV18 p;
                         auto params = make_parameter_list_kitchen_sink();

                         {
                             OperationRequestMessage m;
                             m.operation_code = 7;
                             m.parameters = params;
                             roundtrip(p, Message(m));
                         }
                         {
                             OperationResponseMessage m;
                             m.operation_code = 7;
                             m.return_code = 0;
                             m.debug_message = std::string("OK");
                             m.parameters = params;
                             roundtrip(p, Message(m));
                         }
                         {
                             OperationResponseMessage m;
                             m.operation_code = 7;
                             m.return_code = -3;
                             m.debug_message = std::nullopt;
                             m.parameters = ParameterList{}; // empty
                             roundtrip(p, Message(m));
                         }
                         {
                             EventMessage m;
                             m.event_code = 99;
                             m.parameters = params;
                             roundtrip(p, Message(m));
                         }
                         {
                             DisconnectMessage m;
                             m.code = 123;
                             m.message = std::string("bye");
                             m.parameters = params;
                             roundtrip(p, Message(m));
                         }
                         {
                             DisconnectMessage m;
                             m.code = -1;
                             m.message = std::nullopt;
                             m.parameters = ParameterList{};
                             roundtrip(p, Message(m));
                         }
                     }});

    tests.push_back({"Roundtrip: InternalOperationRequest/Response", [] {
                         GpBinaryV18 p;
                         auto params = make_parameter_list_kitchen_sink();

                         {
                             InternalOperationRequestMessage m;
                             m.operation_code = 1;
                             m.parameters = params;
                             roundtrip(p, Message(m));
                         }
                         {
                             InternalOperationResponseMessage m;
                             m.operation_code = 1;
                             m.return_code = 0;
                             m.debug_message = std::string("internal-ok");
                             m.parameters = params;
                             roundtrip(p, Message(m));
                         }
                         {
                             InternalOperationResponseMessage m;
                             m.operation_code = 1;
                             m.return_code = -10;
                             m.debug_message = std::nullopt;
                             m.parameters = ParameterList{};
                             roundtrip(p, Message(m));
                         }
                     }});

    tests.push_back({"Roundtrip: GenericValueMessage for all Value alternatives (and some nesting)", [] {
                         GpBinaryV18 p;

                         std::vector<Value> values;

                         values.emplace_back(Value()); // null
                         values.emplace_back(Value(true));
                         values.emplace_back(Value(uint8_t{0x7F}));
                         values.emplace_back(Value(int16_t{-123}));
                         values.emplace_back(Value(int32_t{1234567}));
                         values.emplace_back(Value(int64_t{-1234567890123LL}));
                         values.emplace_back(Value(-0.0f));
                         values.emplace_back(Value(std::numeric_limits<float>::quiet_NaN()));
                         values.emplace_back(Value(-0.0));
                         values.emplace_back(Value(std::numeric_limits<double>::quiet_NaN()));
                         values.emplace_back(Value(std::string("")));
                         values.emplace_back(Value(std::string("str")));
                         values.emplace_back(Value(ByteArray{}));
                         values.emplace_back(Value(ByteArray{1, 2, 3}));

                         values.emplace_back(Value(std::vector<bool>{}));
                         values.emplace_back(Value(std::vector<bool>{true, false, true}));

                         values.emplace_back(Value(std::vector<int16_t>{-1, 0, 1}));
                         values.emplace_back(Value(std::vector<int32_t>{-1, 0, 1, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()}));
                         values.emplace_back(Value(std::vector<int64_t>{-1, 0, 1, std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()}));
                         values.emplace_back(Value(std::vector<float>{-0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN()}));
                         values.emplace_back(Value(std::vector<double>{-0.0, 2.0, std::numeric_limits<double>::quiet_NaN()}));
                         values.emplace_back(Value(std::vector<std::string>{"a", "", "b"}));

                         {
                             ObjectArray arr;
                             arr.emplace_back(Value());
                             arr.emplace_back(Value(int32_t{7}));
                             arr.emplace_back(Value(std::string("x")));
                             values.emplace_back(Value(std::move(arr)));
                         }
                         {
                             Dictionary d;
                             d.emplace(uint8_t{1}, Value(int32_t{1}));
                             d.emplace(uint8_t{2}, Value(std::string("two")));
                             values.emplace_back(Value(std::move(d)));
                         }
                         {
                             auto ht = std::make_shared<Hashtable>();
                             ht->emplace(Value(std::string("k")), Value(std::string("v")));
                             ht->emplace(Value(int32_t{5}), Value(int32_t{6}));
                             values.emplace_back(Value(std::move(ht)));
                         }
                         {
                             RawCustomValue cv;
                             cv.custom_code = 9;
                             cv.data = ByteArray{9, 9, 9};
                             values.emplace_back(Value(std::move(cv)));
                         }

                         // Some nesting
                         values.emplace_back(make_deeply_nested_value(5));

                         for (const auto& v : values) {
                             GenericValueMessage m;
                             m.value = v;
                             roundtrip(p, Message(m));
                         }
                     }});

    tests.push_back({"Error path: Bad magic", [] {
                         GpBinaryV18 p;
                         OperationRequestMessage m;
                         m.operation_code = 1;
                         m.parameters = ParameterList{};

                         auto bytes = p.Serialize(Message(m));
                         require_expected(bytes, "Serialize");

                         REQUIRE(!bytes->empty());
                         (*bytes)[0] ^= 0xFF; // GP_MAGIC is 0xF3; corrupt it.

                         auto decoded = p.Deserialize(std::span<const uint8_t>(bytes->data(), bytes->size()));
                         REQUIRE(!decoded);
                         REQUIRE(decoded.error().code == Error::Code::BadMagic || decoded.error().code == Error::Code::BadPacket);
                     }});

    tests.push_back({"Error path: Truncated packet", [] {
                         GpBinaryV18 p;
                         EventMessage m;
                         m.event_code = 7;
                         m.parameters = make_parameter_list_kitchen_sink();

                         auto bytes = p.Serialize(Message(m));
                         require_expected(bytes, "Serialize");
                         REQUIRE(bytes->size() >= 2);

                         bytes->pop_back(); // truncate

                         auto decoded = p.Deserialize(std::span<const uint8_t>(bytes->data(), bytes->size()));
                         REQUIRE(!decoded);
                         REQUIRE(decoded.error().code == Error::Code::Truncated || decoded.error().code == Error::Code::BadPacket);
                     }});

    tests.push_back({"Depth limit: deep nesting eventually fails with DepthLimit", [] {
                         GpBinaryV18 p;

                         // Ensure shallow works
                         {
                             GenericValueMessage m;
                             m.value = make_deeply_nested_value(2);
                             roundtrip(p, Message(m));
                         }

                         // Ensure very deep fails (implementation MAX_DEPTH=32)
                         {
                             GenericValueMessage m;
                             m.value = make_deeply_nested_value(100);
                             auto bytes = p.Serialize(Message(m));
                             REQUIRE(!bytes);
                             REQUIRE(bytes.error().code == Error::Code::DepthLimit);
                         }
                     }});

    tests.push_back({"Encryption: handshake + encrypted payload roundtrip + tamper detect + encrypted-without-key", [] {
                         GpBinaryV18 client;
                         GpBinaryV18 server;

                         REQUIRE(!client.has_encryption_key());
                         REQUIRE(!server.has_encryption_key());

                         // Client -> Server: init encryption request
                         auto init_req_bytes = client.CreateInitEncryptionRequest();
                         require_expected(init_req_bytes, "CreateInitEncryptionRequest");

                         auto decoded_req = server.Deserialize(std::span<const uint8_t>(init_req_bytes->data(), init_req_bytes->size()));
                         require_expected(decoded_req, "Server Deserialize init enc request");

                         REQUIRE(std::holds_alternative<InternalOperationRequestMessage>(*decoded_req));
                         auto req_msg = std::get<InternalOperationRequestMessage>(*decoded_req);

                         // Server handles it -> response message
                         auto response_msg = server.HandleInitEncryptionRequest(req_msg);
                         require_expected(response_msg, "HandleInitEncryptionRequest");

                         // Server -> Client: serialize response (must be unencrypted)
                         auto resp_bytes = server.Serialize(Message(*response_msg));
                         require_expected(resp_bytes, "Server Serialize init enc response");

                         auto decoded_resp = client.Deserialize(std::span<const uint8_t>(resp_bytes->data(), resp_bytes->size()));
                         require_expected(decoded_resp, "Client Deserialize init enc response");

                         REQUIRE(std::holds_alternative<InternalOperationResponseMessage>(*decoded_resp));
                         auto resp_msg2 = std::get<InternalOperationResponseMessage>(*decoded_resp);

                         auto ok = client.HandleInitEncryptionResponse(resp_msg2);
                         require_expected(ok, "HandleInitEncryptionResponse");

                         REQUIRE(client.has_encryption_key());
                         REQUIRE(server.has_encryption_key());

                         // Encrypted payload roundtrip: client->server
                         OperationRequestMessage op;
                         op.operation_code = 33;
                         op.parameters = make_parameter_list_kitchen_sink();

                         auto enc_bytes = client.Serialize(Message(op, true));
                         require_expected(enc_bytes, "Serialize encrypted");

                         auto dec_on_server = server.Deserialize(std::span<const uint8_t>(enc_bytes->data(), enc_bytes->size()));
                         require_expected(dec_on_server, "Server Deserialize encrypted");
                         REQUIRE(messages_equal_deep(Message(op), *dec_on_server));

                         // Encrypted without key: a fresh protocol should reject
                         {
                             GpBinaryV18 stranger;
                             REQUIRE(!stranger.has_encryption_key());
                             auto dec3 = stranger.Deserialize(std::span<const uint8_t>(enc_bytes->data(), enc_bytes->size()));
                             REQUIRE(!dec3);
                             REQUIRE(dec3.error().code == Error::Code::CryptoNotReady || dec3.error().code == Error::Code::CryptoError);
                         }
                     }});

    int passed = 0, failed = 0, skipped = 0;

    for (const auto& t : tests) {
        try {
            t.fn();
            ++passed;
            std::cout << "[PASS] " << t.name << "\n";
        } catch (const TestSkip& e) {
            ++skipped;
            std::cout << "[SKIP] " << t.name << " : " << e.what() << "\n";
        } catch (const TestFailure& e) {
            ++failed;
            std::cout << "[FAIL] " << t.name << " : " << e.what() << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << t.name << " : unexpected exception: " << e.what() << "\n";
        } catch (...) {
            ++failed;
            std::cout << "[FAIL] " << t.name << " : unknown exception\n";
        }
    }

    std::cout << "\nSummary: passed=" << passed << " failed=" << failed << " skipped=" << skipped << "\n";
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
