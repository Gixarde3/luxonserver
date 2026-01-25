#include "ser_types.hpp"

namespace luxon::ser {
namespace {
inline std::size_t hash_combine(std::size_t a, std::size_t b) noexcept {
    // boost-ish combine
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return a;
}

std::size_t hash_bytes(std::span<const uint8_t> s) noexcept {
    std::size_t h = 1469598103934665603ULL; // FNV-1a 64-bit basis
    for (uint8_t b : s) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

inline bool f32_bits_equal(float a, float b) noexcept { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }
inline bool f64_bits_equal(double a, double b) noexcept { return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b); }
} // namespace

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

    auto hash_vec = [&](auto&& vec) {
        std::size_t out = 0;
        for (const auto& e : vec) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, bool>)
                out = hash_combine(out, std::hash<bool>{}(e));
            else if constexpr (std::is_same_v<E, float>)
                out = hash_combine(out, std::hash<uint32_t>{}(std::bit_cast<uint32_t>(e)));
            else if constexpr (std::is_same_v<E, double>)
                out = hash_combine(out, std::hash<uint64_t>{}(std::bit_cast<uint64_t>(e)));
            else
                out = hash_combine(out, std::hash<E>{}(e));
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
                h = hash_combine(h, hash_vec(a));
            } else if constexpr (std::is_same_v<T, ObjectArray>) {
                std::size_t out = 0;
                for (const auto& e : a)
                    out = hash_combine(out, ValueHash{}(e));
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, Dictionary>) {
                // order-insensitive: XOR pair hashes
                std::size_t out = 0;
                for (const auto& [k, val] : a) {
                    std::size_t ph = hash_combine(std::hash<uint8_t>{}(k), ValueHash{}(val));
                    out ^= ph;
                }
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Hashtable>>) {
                if (!a) {
                    h = hash_combine(h, 0);
                    return;
                }
                std::size_t out = 0;
                for (const auto& [k, val] : *a) {
                    std::size_t ph = hash_combine(ValueHash{}(k), ValueHash{}(val));
                    out ^= ph;
                }
                h = hash_combine(h, out);
            } else if constexpr (std::is_same_v<T, RawCustomValue>) {
                h = hash_combine(h, std::hash<uint8_t>{}(a.custom_code));
                h = hash_combine(h, hash_bytes(a.data));
            }
        },
        v.value);

    return h;
}
} // namespace luxon::ser
