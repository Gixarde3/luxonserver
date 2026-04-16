#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace luxon {
template <std::unsigned_integral Key, class Mapped, std::size_t WindowSize>
class sliding_flat_map {
    static_assert(WindowSize > 0, "WindowSize must be > 0");
    static_assert(std::has_single_bit(WindowSize),
                  "WindowSize must be a power of two");

public:
    using key_type = Key;
    using mapped_type = Mapped;
    using value_type = std::pair<key_type, mapped_type>;
    using size_type = std::size_t;

    enum class insert_state {
        inserted,
        existing,
        out_of_window
    };

    struct insert_result {
        mapped_type* value = nullptr;
        insert_state state = insert_state::out_of_window;

        [[nodiscard]] bool inserted() const noexcept {
            return state == insert_state::inserted;
        }

        [[nodiscard]] bool existing() const noexcept {
            return state == insert_state::existing;
        }

        [[nodiscard]] bool out_of_window() const noexcept {
            return state == insert_state::out_of_window;
        }

        explicit operator bool() const noexcept {
            return state != insert_state::out_of_window;
        }
    };

private:
    static constexpr size_type kMask = WindowSize - 1;

    // Each slot stores the full key so modulo collisions are detectable.
    std::array<std::optional<value_type>, WindowSize> storage_{};

    // Lowest key currently addressable by the window.
    key_type base_{};

    // Number of occupied slots.
    size_type size_ = 0;

    static constexpr size_type physical_index(key_type key) noexcept {
        return static_cast<size_type>(key) & kMask;
    }

    // Unsigned subtraction makes wrap-around work naturally for sequence numbers.
    static constexpr size_type distance_from(key_type from, key_type to) noexcept {
        return static_cast<size_type>(to - from);
    }

    [[nodiscard]] constexpr bool in_window(key_type key) const noexcept {
        return distance_from(base_, key) < WindowSize;
    }

    [[nodiscard]] constexpr std::optional<value_type>& slot_for(key_type key) noexcept {
        return storage_[physical_index(key)];
    }

    [[nodiscard]] constexpr const std::optional<value_type>& slot_for(key_type key) const noexcept {
        return storage_[physical_index(key)];
    }

    [[nodiscard]] static constexpr bool matches(const std::optional<value_type>& slot,
                                                key_type key) noexcept {
        return slot.has_value() && slot->first == key;
    }

    void clear_physical_slot(key_type key) noexcept {
        auto& slot = slot_for(key);
        if (slot.has_value()) {
            slot.reset();
            --size_;
        }
    }

public:
    sliding_flat_map() = default;

    explicit sliding_flat_map(key_type base) noexcept
        : base_(base) {}

    [[nodiscard]] constexpr key_type base_key() const noexcept { return base_; }
    [[nodiscard]] constexpr key_type next_key() const noexcept { return base_; }

    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] static constexpr size_type capacity() noexcept { return WindowSize; }

    [[nodiscard]] constexpr bool accepts(key_type key) const noexcept {
        return in_window(key);
    }

    void clear() noexcept {
        for (auto& slot : storage_) {
            slot.reset();
        }
        size_ = 0;
    }

    void reset(key_type new_base = key_type{}) noexcept {
        clear();
        base_ = new_base;
    }

    [[nodiscard]] bool contains(key_type key) const noexcept {
        return in_window(key) && matches(slot_for(key), key);
    }

    mapped_type* find(key_type key) noexcept {
        if (!contains(key)) return nullptr;
        return &slot_for(key)->second;
    }

    const mapped_type* find(key_type key) const noexcept {
        if (!contains(key)) return nullptr;
        return &slot_for(key)->second;
    }

    value_type* find_value(key_type key) noexcept {
        if (!contains(key)) return nullptr;
        return &*slot_for(key);
    }

    const value_type* find_value(key_type key) const noexcept {
        if (!contains(key)) return nullptr;
        return &*slot_for(key);
    }

    mapped_type& at(key_type key) {
        auto* p = find(key);
        if (!p) {
            throw std::out_of_range("sliding_flat_map::at: key not found");
        }
        return *p;
    }

    const mapped_type& at(key_type key) const {
        auto* p = find(key);
        if (!p) {
            throw std::out_of_range("sliding_flat_map::at: key not found");
        }
        return *p;
    }

    template <class... Args>
    insert_result try_emplace(key_type key, Args&&... args) {
        if (!in_window(key)) {
            return {};
        }

        auto& slot = slot_for(key);

        if (matches(slot, key)) {
            return {&slot->second, insert_state::existing};
        }

        // If a stale entry is still sitting in this physical slot, evict it.
        if (slot.has_value()) {
            slot.reset();
            --size_;
        }

        slot.emplace(std::piecewise_construct,
                     std::forward_as_tuple(key),
                     std::forward_as_tuple(std::forward<Args>(args)...));
        ++size_;
        return {&slot->second, insert_state::inserted};
    }

    template <class... Args>
    insert_result emplace(key_type key, Args&&... args) {
        return try_emplace(key, std::forward<Args>(args)...);
    }

    insert_result insert(const value_type& value) {
        return try_emplace(value.first, value.second);
    }

    insert_result insert(value_type&& value) {
        if (!in_window(value.first)) {
            return {};
        }

        auto& slot = slot_for(value.first);

        if (matches(slot, value.first)) {
            return {&slot->second, insert_state::existing};
        }

        if (slot.has_value()) {
            slot.reset();
            --size_;
        }

        slot.emplace(std::move(value));
        ++size_;
        return {&slot->second, insert_state::inserted};
    }

    template <class M>
    insert_result insert_or_assign(key_type key, M&& obj) {
        if (!in_window(key)) {
            return {};
        }

        auto& slot = slot_for(key);

        if (matches(slot, key)) {
            slot->second = std::forward<M>(obj);
            return {&slot->second, insert_state::existing};
        }

        if (slot.has_value()) {
            slot.reset();
            --size_;
        }

        slot.emplace(std::piecewise_construct,
                     std::forward_as_tuple(key),
                     std::forward_as_tuple(std::forward<M>(obj)));
        ++size_;
        return {&slot->second, insert_state::inserted};
    }

    template <class T = mapped_type>
        requires std::default_initializable<T>
    mapped_type& operator[](key_type key) {
        auto result = try_emplace(key);
        if (result.out_of_window()) {
            throw std::out_of_range(
                "sliding_flat_map::operator[]: key outside active window");
        }
        return *result.value;
    }

    size_type erase(key_type key) noexcept {
        if (!in_window(key)) {
            return 0;
        }

        auto& slot = slot_for(key);
        if (!matches(slot, key)) {
            return 0;
        }

        slot.reset();
        --size_;
        return 1;
    }

    [[nodiscard]] bool has_front() const noexcept {
        return contains(base_);
    }

    mapped_type* front() noexcept {
        return find(base_);
    }

    const mapped_type* front() const noexcept {
        return find(base_);
    }

    value_type* front_value() noexcept {
        return find_value(base_);
    }

    const value_type* front_value() const noexcept {
        return find_value(base_);
    }

    bool pop_front() noexcept {
        auto& slot = slot_for(base_);
        if (!matches(slot, base_)) {
            return false;
        }

        slot.reset();
        --size_;
        ++base_;
        return true;
    }

    std::optional<value_type> pop_front_value() {
        auto& slot = slot_for(base_);
        if (!matches(slot, base_)) {
            return std::nullopt;
        }

        std::optional<value_type> out(std::move(*slot));
        slot.reset();
        --size_;
        ++base_;
        return out;
    }

    void advance_one() noexcept {
        advance_to(static_cast<key_type>(base_ + 1));
    }

    void advance_to(key_type new_base) noexcept {
        const size_type delta = distance_from(base_, new_base);
        if (delta == 0) {
            return;
        }

        // If we jumped farther than the whole window, everything becomes stale.
        if (delta >= WindowSize) {
            clear();
            base_ = new_base;
            return;
        }

        for (size_type i = 0; i < delta; ++i) {
            clear_physical_slot(static_cast<key_type>(base_ + i));
        }

        base_ = new_base;
    }

    template <class Fn>
    size_type consume_contiguous(Fn&& fn) {
        size_type consumed = 0;

        while (auto* entry = front_value()) {
            std::invoke(fn, entry->first, entry->second);
            pop_front();
            ++consumed;
        }

        return consumed;
    }
};
} // namespace luxon
