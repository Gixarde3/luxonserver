// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "ser_types.hpp"

#include <bit>

namespace luxon::ser {
namespace {
inline std::size_t hash_combine(std::size_t a, std::size_t b) noexcept {
    // boost-ish combine
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return a;
}

std::size_t hash_bytes(std::span<const uint8_t> s) noexcept {
    if constexpr (sizeof(std::size_t) == 8) {
        // 64-bit FNV-1a
        std::size_t h = 1469598103934665603ULL;
        for (uint8_t b : s) {
            h ^= b;
            h *= 1099511628211ULL;
        }
        return h;
    } else {
        // 32-bit FNV-1a
        // Basis: 2166136261, Prime: 16777619
        std::size_t h = 2166136261U;
        for (uint8_t b : s) {
            h ^= b;
            h *= 16777619U;
        }
        return h;
    }
}

inline bool f32_bits_equal(float a, float b) noexcept { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }
inline bool f64_bits_equal(double a, double b) noexcept { return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b); }
} // namespace

const Value null;

bool Value::operator==(const Value& other) const {
    if (value.index() != other.value.index())
        return false;

    return std::visit(
        [&](const auto& a) -> bool {
            using T = std::decay_t<decltype(a)>;
            const T *b = std::get_if<T>(&other.value);
            if (!b)
                return false;

            if constexpr (std::is_same_v<T, float>) {
                return f32_bits_equal(a, *b);
            } else if constexpr (std::is_same_v<T, double>) {
                return f64_bits_equal(a, *b);
            } else if constexpr (std::is_same_v<T, std::vector<float>>) {
                if (a.size() != b->size())
                    return false;
                for (std::size_t i = 0; i < a.size(); ++i) {
                    if (!f32_bits_equal(a[i], (*b)[i]))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                if (a.size() != b->size())
                    return false;
                for (std::size_t i = 0; i < a.size(); ++i) {
                    if (!f64_bits_equal(a[i], (*b)[i]))
                        return false;
                }
                return true;
            }

            if constexpr (std::is_same_v<T, std::shared_ptr<Hashtable>>) {
                if (!a && !*b)
                    return true;
                if (!a || !*b)
                    return false;
                return *a == **b;
            } else {
                return a == *b;
            }
        },
        value);
}

std::size_t ValueHash::operator()(const Value& v) const noexcept {
    std::size_t h = std::hash<std::size_t>{}(v.value.index());

    auto hash_vec_values = [&](const auto& vec) {
        std::size_t out = 0;
        for (const auto& e : vec)
            out = hash_combine(out, ValueHash{}(e));
        return out;
    };

    auto hash_dictionary = [&](const Dictionary& d) {
        std::size_t out = 0;
        for (const auto& [k, val] : d) {
            std::size_t ph = hash_combine(std::hash<uint8_t>{}(k), ValueHash{}(val));
            out ^= ph;
        }
        return out;
    };

    auto hash_hashtable_ptr = [&](const HashtablePtr& p) {
        if (!p)
            return std::size_t{0};

        std::size_t out = 0;
        for (const auto& [k, val] : *p) {
            std::size_t ph = hash_combine(ValueHash{}(k), ValueHash{}(val));
            out ^= ph;
        }
        return out;
    };

    auto hash_generic_dictionary = [&](const GenericDictionary& gd) {
        std::size_t out = hash_bytes(gd.header);
        for (const auto& [k, val] : gd.entries) {
            out = hash_combine(out, ValueHash{}(k));
            out = hash_combine(out, ValueHash{}(val));
        }
        return out;
    };

    std::visit(
        [&](const auto& a) {
            using T = std::decay_t<decltype(a)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                h = hash_combine(h, 0);
            } else if constexpr (std::is_same_v<T, bool>) {
                h = hash_combine(h, std::hash<bool>{}(a));
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                h = hash_combine(h, std::hash<uint8_t>{}(a));
            } else if constexpr (std::is_same_v<T, int16_t>) {
                h = hash_combine(h, std::hash<int16_t>{}(a));
            } else if constexpr (std::is_same_v<T, int32_t>) {
                h = hash_combine(h, std::hash<int32_t>{}(a));
            } else if constexpr (std::is_same_v<T, int64_t>) {
                h = hash_combine(h, std::hash<int64_t>{}(a));
            } else if constexpr (std::is_same_v<T, float>) {
                h = hash_combine(h, std::hash<uint32_t>{}(std::bit_cast<uint32_t>(a)));
            } else if constexpr (std::is_same_v<T, double>) {
                h = hash_combine(h, std::hash<uint64_t>{}(std::bit_cast<uint64_t>(a)));
            } else if constexpr (std::is_same_v<T, std::string>) {
                h = hash_combine(h, std::hash<std::string>{}(a));
            } else if constexpr (std::is_same_v<T, ByteArray>) {
                h = hash_combine(h, hash_bytes(a));
            } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
                std::size_t out = 0;
                for (bool b : a)
                    out = hash_combine(out, std::hash<bool>{}(b));
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, std::vector<int16_t>> || std::is_same_v<T, std::vector<int32_t>> ||
                                 std::is_same_v<T, std::vector<int64_t>> || std::is_same_v<T, std::vector<float>> || std::is_same_v<T, std::vector<double>> ||
                                 std::is_same_v<T, std::vector<std::string>>) {
                std::size_t out = 0;
                for (const auto& e : a) {
                    using E = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<E, float>)
                        out = hash_combine(out, std::hash<uint32_t>{}(std::bit_cast<uint32_t>(e)));
                    else if constexpr (std::is_same_v<E, double>)
                        out = hash_combine(out, std::hash<uint64_t>{}(std::bit_cast<uint64_t>(e)));
                    else
                        out = hash_combine(out, std::hash<E>{}(e));
                }
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, ObjectArray>) {
                h = hash_combine(h, hash_vec_values(a));
            } else if constexpr (std::is_same_v<T, JaggedArray>) {
                h = hash_combine(h, hash_vec_values(a.elements));
            } else if constexpr (std::is_same_v<T, Dictionary>) {
                h = hash_combine(h, hash_dictionary(a));
            } else if constexpr (std::is_same_v<T, GenericDictionary>) {
                h = hash_combine(h, hash_generic_dictionary(a));
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Hashtable>>) {
                h = hash_combine(h, hash_hashtable_ptr(a));
            } else if constexpr (std::is_same_v<T, RawCustomValue>) {
                h = hash_combine(h, std::hash<uint8_t>{}(a.custom_code));
                h = hash_combine(h, hash_bytes(a.data));
            } else if constexpr (std::is_same_v<T, EventMessage>) {
                h = hash_combine(h, std::hash<uint8_t>{}(a.event_code));
                h = hash_combine(h, hash_dictionary(a.parameters));
            } else if constexpr (std::is_same_v<T, OperationRequestMessage>) {
                h = hash_combine(h, std::hash<uint8_t>{}(a.operation_code));
                h = hash_combine(h, hash_dictionary(a.parameters));
            } else if constexpr (std::is_same_v<T, OperationResponseMessage>) {
                h = hash_combine(h, std::hash<uint8_t>{}(a.operation_code));
                h = hash_combine(h, std::hash<int16_t>{}(a.return_code));
                h = hash_combine(h, a.debug_message ? std::hash<std::string>{}(*a.debug_message) : 0);
                h = hash_combine(h, hash_dictionary(a.parameters));
            } else if constexpr (std::is_same_v<T, std::vector<Dictionary>>) {
                std::size_t out = 0;
                for (const auto& d : a)
                    out = hash_combine(out, hash_dictionary(d));
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, std::vector<GenericDictionary>>) {
                std::size_t out = 0;
                for (const auto& d : a)
                    out = hash_combine(out, hash_generic_dictionary(d));
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, std::vector<HashtablePtr>>) {
                std::size_t out = 0;
                for (const auto& p : a)
                    out = hash_combine(out, hash_hashtable_ptr(p));
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, std::vector<RawCustomValue>>) {
                std::size_t out = 0;
                for (const auto& e : a) {
                    out = hash_combine(out, std::hash<uint8_t>{}(e.custom_code));
                    out = hash_combine(out, hash_bytes(e.data));
                }
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, PreSerializedValue>) {
                h = hash_combine(h, hash_bytes(a.data));
            }
        },
        v.value);

    return h;
}

const Value& Dictionary::operator[](uint8_t key) const {
    if (auto res = find(key); res != end())
        return res->second;
    return null;
}
} // namespace luxon::ser
