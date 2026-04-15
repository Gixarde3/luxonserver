// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <algorithm>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace luxon::ser {
template <class Key,
          class Mapped,
          class Compare = std::less<Key>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map {
public:
    using key_type = Key;
    using mapped_type = Mapped;
    using value_type = std::pair<Key, Mapped>;
    using size_type = typename Container::size_type;
    using difference_type = typename Container::difference_type;
    using key_compare = Compare;
    using reference = typename Container::reference;
    using const_reference = typename Container::const_reference;
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;
    using reverse_iterator = typename Container::reverse_iterator;
    using const_reverse_iterator = typename Container::const_reverse_iterator;
    using container_type = Container;

private:
    container_type storage_;
    Compare comp_;

    // Helper to compare a value_type with a key
    struct KeyValueCompare {
        Compare comp;
        bool operator()(const value_type& lhs, const key_type& rhs) const {
            return comp(lhs.first, rhs);
        }
        bool operator()(const key_type& lhs, const value_type& rhs) const {
            return comp(lhs, rhs.first);
        }
        bool operator()(const value_type& lhs, const value_type& rhs) const {
            return comp(lhs.first, rhs.first);
        }
    };

    void sort_and_unique() {
        std::sort(storage_.begin(), storage_.end(), KeyValueCompare{comp_});
        auto last = std::unique(storage_.begin(), storage_.end(),
                                [this](const value_type& a, const value_type& b) {
                                    // Elements are equal if neither is less than the other
                                    return !comp_(a.first, b.first) && !comp_(b.first, a.first);
                                });
        storage_.erase(last, storage_.end());
    }

public:
    // Constructors
    flat_map() = default;

    explicit flat_map(const Compare& comp) : comp_(comp) {}

    flat_map(std::initializer_list<value_type> init, const Compare& comp = Compare())
        : storage_(init), comp_(comp) {
        sort_and_unique();
    }

    template <class InputIt>
    flat_map(InputIt first, InputIt last, const Compare& comp = Compare())
        : storage_(first, last), comp_(comp) {
        sort_and_unique();
    }

    explicit flat_map(container_type container, const Compare& comp = Compare())
        : storage_(std::move(container)), comp_(comp) {
        sort_and_unique();
    }

    // Iterators
    iterator begin() { return storage_.begin(); }
    const_iterator begin() const { return storage_.begin(); }
    const_iterator cbegin() const { return storage_.cbegin(); }

    iterator end() { return storage_.end(); }
    const_iterator end() const { return storage_.end(); }
    const_iterator cend() const { return storage_.cend(); }

    reverse_iterator rbegin() { return storage_.rbegin(); }
    const_reverse_iterator rbegin() const { return storage_.rbegin(); }
    const_reverse_iterator crbegin() const { return storage_.crbegin(); }

    reverse_iterator rend() { return storage_.rend(); }
    const_reverse_iterator rend() const { return storage_.rend(); }
    const_reverse_iterator crend() const { return storage_.crend(); }

    // Capacity
    bool empty() const { return storage_.empty(); }
    size_type size() const { return storage_.size(); }
    size_type max_size() const { return storage_.max_size(); }
    void reserve(size_type new_cap) { storage_.reserve(new_cap); }
    size_type capacity() const { return storage_.capacity(); }
    void shrink_to_fit() { storage_.shrink_to_fit(); }

    // Modifiers
    void clear() { storage_.clear(); }

    std::pair<iterator, bool> insert(const value_type& value) {
        auto it = lower_bound(value.first);
        if (it != end() && !comp_(value.first, it->first)) {
            return {it, false}; // Already exists
        }
        return {storage_.insert(it, value), true};
    }

    std::pair<iterator, bool> insert(value_type&& value) {
        auto it = lower_bound(value.first);
        if (it != end() && !comp_(value.first, it->first)) {
            return {it, false};
        }
        return {storage_.insert(it, std::move(value)), true};
    }

    template <class... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        value_type val(std::forward<Args>(args)...);
        return insert(std::move(val));
    }

    template <class... Args>
    std::pair<iterator, bool> try_emplace(const key_type& key, Args&&... args) {
        auto it = lower_bound(key);
        if (it != end() && !comp_(key, it->first)) {
            return {it, false};
        }
        return {storage_.emplace(it, std::piecewise_construct,
                                 std::forward_as_tuple(key),
                                 std::forward_as_tuple(std::forward<Args>(args)...)),
                true};
    }

    template <class M>
    std::pair<iterator, bool> insert_or_assign(const key_type& key, M&& obj) {
        auto it = lower_bound(key);
        if (it != end() && !comp_(key, it->first)) {
            it->second = std::forward<M>(obj);
            return {it, false};
        }
        return {storage_.emplace(it, key, std::forward<M>(obj)), true};
    }

    iterator erase(iterator pos) { return storage_.erase(pos); }
    iterator erase(const_iterator pos) { return storage_.erase(pos); }
    iterator erase(const_iterator first, const_iterator last) { return storage_.erase(first, last); }

    size_type erase(const key_type& key) {
        auto it = find(key);
        if (it == end()) return 0;
        storage_.erase(it);
        return 1;
    }

    void swap(flat_map& other) noexcept {
        storage_.swap(other.storage_);
        std::swap(comp_, other.comp_);
    }

    // Lookup
    mapped_type& at(const key_type& key) {
        auto it = find(key);
        if (it == end()) throw std::out_of_range("flat_map::at: key not found");
        return it->second;
    }

    const mapped_type& at(const key_type& key) const {
        auto it = find(key);
        if (it == end()) throw std::out_of_range("flat_map::at: key not found");
        return it->second;
    }

    mapped_type& operator[](const key_type& key) {
        auto it = lower_bound(key);
        if (it == end() || comp_(key, it->first)) {
            it = storage_.emplace(it, key, mapped_type());
        }
        return it->second;
    }

    mapped_type& operator[](key_type&& key) {
        auto it = lower_bound(key);
        if (it == end() || comp_(key, it->first)) {
            it = storage_.emplace(it, std::move(key), mapped_type());
        }
        return it->second;
    }

    size_type count(const key_type& key) const {
        return find(key) == end() ? 0 : 1;
    }

    iterator find(const key_type& key) {
        auto it = lower_bound(key);
        if (it != end() && !comp_(key, it->first)) {
            return it;
        }
        return end();
    }

    const_iterator find(const key_type& key) const {
        auto it = lower_bound(key);
        if (it != end() && !comp_(key, it->first)) {
            return it;
        }
        return end();
    }

    bool contains(const key_type& key) const {
        return find(key) != end();
    }

    iterator lower_bound(const key_type& key) {
        return std::lower_bound(storage_.begin(), storage_.end(), key, KeyValueCompare{comp_});
    }

    const_iterator lower_bound(const key_type& key) const {
        return std::lower_bound(storage_.begin(), storage_.end(), key, KeyValueCompare{comp_});
    }

    iterator upper_bound(const key_type& key) {
        return std::upper_bound(storage_.begin(), storage_.end(), key, KeyValueCompare{comp_});
    }

    const_iterator upper_bound(const key_type& key) const {
        return std::upper_bound(storage_.begin(), storage_.end(), key, KeyValueCompare{comp_});
    }

    std::pair<iterator, iterator> equal_range(const key_type& key) {
        return std::equal_range(storage_.begin(), storage_.end(), key, KeyValueCompare{comp_});
    }

    std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const {
        return std::equal_range(storage_.begin(), storage_.end(), key, KeyValueCompare{comp_});
    }

    // Observers
    key_compare key_comp() const { return comp_; }
    container_type extract() && { return std::move(storage_); }
    void replace(container_type&& container) {
        storage_ = std::move(container);
        sort_and_unique();
    }
};

template <class Key, class Mapped, class Compare, class Container>
void swap(flat_map<Key, Mapped, Compare, Container>& lhs,
          flat_map<Key, Mapped, Compare, Container>& rhs) noexcept {
    lhs.swap(rhs);
}

template <class Key, class Mapped, class Compare, class Container>
bool operator==(const flat_map<Key, Mapped, Compare, Container>& lhs,
                const flat_map<Key, Mapped, Compare, Container>& rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template <class Key, class Mapped, class Compare, class Container>
bool operator!=(const flat_map<Key, Mapped, Compare, Container>& lhs,
                const flat_map<Key, Mapped, Compare, Container>& rhs) {
    return !(lhs == rhs);
}
} // namespace luxon::ser
