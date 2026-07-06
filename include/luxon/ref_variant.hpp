#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace luxon::ser {
namespace rv_detail {
template <class...> struct unique_types : std::true_type {};

template <class T, class... Rest> struct unique_types<T, Rest...> : std::bool_constant<(!(std::same_as<T, Rest>) && ...) && unique_types<Rest...>::value> {};

template <class T, class... Ts> struct index_of;

template <class T, class... Rest> struct index_of<T, T, Rest...> : std::integral_constant<std::size_t, 0> {};

template <class T, class U, class... Rest> struct index_of<T, U, Rest...> : std::integral_constant<std::size_t, 1 + index_of<T, Rest...>::value> {};
} // namespace rv_detail

template <class... Ts> class ref_variant {
    static_assert(sizeof...(Ts) > 0, "ref_variant needs at least one alternative");
    static_assert(rv_detail::unique_types<Ts...>::value, "ref_variant alternatives must be unique");
    static_assert((std::is_object_v<Ts> && ...), "ref_variant alternatives must be object types");
    static_assert((!std::is_reference_v<Ts> && ...), "ref_variant alternatives must not be references");
    static_assert((!std::is_pointer_v<Ts> && ...), "Declare T, not T*. Passing T* stores a non-owning reference to T");
    static_assert(((!std::is_const_v<Ts> && !std::is_volatile_v<Ts>) && ...), "This implementation expects cv-unqualified alternatives");

    template <std::size_t I> using alt_t = std::tuple_element_t<I, std::tuple<Ts...>>;

    using first_t = alt_t<0>;

    static constexpr std::size_t storage_size_ = std::max({std::size_t(sizeof(Ts))...});
    static constexpr std::size_t storage_align_ = std::max({std::size_t(alignof(Ts))...});

    struct no_init_t {
        explicit no_init_t() = default;
    };

    alignas(storage_align_) std::byte storage_[storage_size_];
    void *ptr_ = nullptr; // points to active object, owned or referenced
    std::size_t index_ = 0;
    bool owning_ = false;

    template <class T> static constexpr bool is_alt_v = (std::same_as<T, Ts> || ...);

    template <class T> static constexpr std::size_t index_of_v = rv_detail::index_of<T, Ts...>::value;

    template <class T> static T *cast_ptr(void *p) noexcept { return std::launder(reinterpret_cast<T *>(p)); }

    template <class T> static const T *cast_ptr(const void *p) noexcept { return std::launder(reinterpret_cast<const T *>(p)); }

    template <class T> void bind_reference(T *p) noexcept {
        assert(p != nullptr);
        ptr_ = p;
        index_ = index_of_v<T>;
        owning_ = false;
    }

    template <std::size_t I = 0> void destroy_impl() noexcept {
        if constexpr (I < sizeof...(Ts)) {
            if (index_ == I) {
                using T = alt_t<I>;
                std::destroy_at(cast_ptr<T>(ptr_));
            } else {
                destroy_impl<I + 1>();
            }
        }
    }

    void destroy() noexcept {
        if (owning_) {
            destroy_impl();
        }
        ptr_ = nullptr;
        index_ = 0;
        owning_ = false;
    }

    void copy_from(const ref_variant& other) {
        if (other.owning_) {
            other.visit([this]<class T>(const T& x) { this->template emplace<T>(x); });
        } else {
            ptr_ = other.ptr_;
            index_ = other.index_;
            owning_ = false;
        }
    }

    void move_from(ref_variant& other) {
        if (other.owning_) {
            other.visit([this]<class T>(T& x) { this->template emplace<T>(std::move(x)); });
        } else {
            ptr_ = other.ptr_;
            index_ = other.index_;
            owning_ = false;
        }
    }

    static ref_variant from_raw_index(std::size_t idx, void *p) noexcept {
        assert(p != nullptr);
        assert(idx < sizeof...(Ts));
        ref_variant v(no_init_t{});
        v.ptr_ = p;
        v.index_ = idx;
        v.owning_ = false;
        return v;
    }

    template <std::size_t I = 0, class Self, class F> static decltype(auto) visit_impl(Self&& self, F&& f) {
        if constexpr (I == sizeof...(Ts) - 1) {
            return std::forward<F>(f)(self.template get<I>());
        } else {
            if (self.index() == I) {
                return std::forward<F>(f)(self.template get<I>());
            }
            return visit_impl<I + 1>(std::forward<Self>(self), std::forward<F>(f));
        }
    }

    explicit ref_variant(no_init_t) noexcept {}

public:
    using tagged_handle = uint64_t;

    ref_variant()
        requires std::default_initializable<first_t>
    {
        emplace<0>();
    }

    ref_variant()
        requires(!std::default_initializable<first_t>)
    = delete;

    ref_variant(const ref_variant& other) { copy_from(other); }

    ref_variant(ref_variant&& other) noexcept((std::is_nothrow_move_constructible_v<Ts> && ...)) { move_from(other); }

    ~ref_variant() { destroy(); }

    ref_variant& operator=(const ref_variant& other) {
        if (this == &other) {
            return *this;
        }
        ref_variant tmp(other);
        destroy();
        move_from(tmp);
        return *this;
    }

    ref_variant& operator=(ref_variant&& other) noexcept((std::is_nothrow_move_constructible_v<Ts> && ...)) {
        if (this == &other) {
            return *this;
        }
        ref_variant tmp(std::move(other));
        destroy();
        move_from(tmp);
        return *this;
    }

    template <std::size_t I, class... Args>
        requires std::constructible_from<alt_t<I>, Args...>
    explicit ref_variant(std::in_place_index_t<I>, Args&&...args) {
        emplace<I>(std::forward<Args>(args)...);
    }

    template <class T, class... Args>
        requires is_alt_v<T> && std::constructible_from<T, Args...>
    explicit ref_variant(std::in_place_type_t<T>, Args&&...args) {
        emplace<T>(std::forward<Args>(args)...);
    }

    // Owning construction from an exact alternative type.
    template <class U>
        requires(!std::is_pointer_v<std::remove_reference_t<U>>) && (is_alt_v<std::remove_cvref_t<U>>)
    ref_variant(U&& value) {
        using T = std::remove_cvref_t<U>;
        emplace<T>(std::forward<U>(value));
    }

    // Owning construction from C-style strings
    template <class U>
        requires(!is_alt_v<std::remove_cvref_t<U>>) && is_alt_v<std::string_view> && std::convertible_to<U, std::string_view>
    ref_variant(U&& value) {
        emplace<std::string_view>(std::forward<U>(value));
    }
    template <class U>
        requires(!is_alt_v<std::remove_cvref_t<U>>) && (!is_alt_v<std::string_view>) && is_alt_v<std::string> && std::convertible_to<U, std::string>
    ref_variant(U&& value) {
        emplace<std::string>(std::forward<U>(value));
    }

    // Owning construction from an enum whose underlying type is an exact alternative.
    template <class U>
        requires std::is_enum_v<std::remove_cvref_t<U>> && is_alt_v<std::underlying_type_t<std::remove_cvref_t<U>>>
    ref_variant(U&& value) {
        using EnumType = std::remove_cvref_t<U>;
        using UnderlyingT = std::underlying_type_t<EnumType>;
        emplace<UnderlyingT>(static_cast<UnderlyingT>(value));
    }

    // Non-owning construction from T* where T is an alternative.
    template <class U>
        requires std::is_pointer_v<std::remove_reference_t<U>> && (is_alt_v<std::remove_pointer_t<std::remove_reference_t<U>>>)
    ref_variant(U&& p) noexcept {
        using T = std::remove_pointer_t<std::remove_reference_t<U>>;
        bind_reference<T>(p);
    }

    template <class T>
        requires is_alt_v<T>
    static ref_variant reference(T& obj) noexcept {
        ref_variant v(no_init_t{});
        v.bind_reference<T>(std::addressof(obj));
        return v;
    }

    template <class T>
        requires is_alt_v<T>
    static ref_variant reference(T *obj) noexcept {
        ref_variant v(no_init_t{});
        v.bind_reference<T>(obj);
        return v;
    }

    [[nodiscard]]
    std::size_t index() const noexcept {
        return index_;
    }

    [[nodiscard]]
    bool is_owning() const noexcept {
        return owning_;
    }

    [[nodiscard]]
    bool is_reference() const noexcept {
        return !owning_;
    }

    template <class T>
        requires is_alt_v<T>
    [[nodiscard]]
    bool holds_alternative() const noexcept {
        return index_ == index_of_v<T>;
    }

    template <std::size_t I, class... Args>
        requires std::constructible_from<alt_t<I>, Args...>
    alt_t<I>& emplace(Args&&...args) {
        destroy();
        using T = alt_t<I>;
        T *p = ::new (static_cast<void *>(storage_)) T(std::forward<Args>(args)...);
        ptr_ = p;
        index_ = I;
        owning_ = true;
        return *p;
    }

    template <class T, class... Args>
        requires is_alt_v<T> && std::constructible_from<T, Args...>
    T& emplace(Args&&...args) {
        return emplace<index_of_v<T>>(std::forward<Args>(args)...);
    }

    template <class T>
        requires is_alt_v<T>
    T& emplace_reference(T& obj) noexcept {
        destroy();
        bind_reference<T>(std::addressof(obj));
        return obj;
    }

    template <class T>
        requires is_alt_v<T>
    T& emplace_reference(T *obj) noexcept {
        destroy();
        bind_reference<T>(obj);
        return *obj;
    }

    template <std::size_t I> alt_t<I>& get() & {
        if (index_ != I) {
            throw std::bad_variant_access{};
        }
        return *cast_ptr<alt_t<I>>(ptr_);
    }

    template <std::size_t I> const alt_t<I>& get() const& {
        if (index_ != I) {
            throw std::bad_variant_access{};
        }
        return *cast_ptr<alt_t<I>>(ptr_);
    }

    template <class T>
        requires is_alt_v<T>
    T& get() & {
        return get<index_of_v<T>>();
    }

    template <class T>
        requires is_alt_v<T>
    const T& get() const& {
        return get<index_of_v<T>>();
    }

    template <std::size_t I> alt_t<I> *get_if() noexcept { return (index_ == I) ? cast_ptr<alt_t<I>>(ptr_) : nullptr; }

    template <std::size_t I> const alt_t<I> *get_if() const noexcept { return (index_ == I) ? cast_ptr<alt_t<I>>(ptr_) : nullptr; }

    template <class T>
        requires is_alt_v<T>
    T *get_if() noexcept {
        return get_if<index_of_v<T>>();
    }

    template <class T>
        requires is_alt_v<T>
    const T *get_if() const noexcept {
        return get_if<index_of_v<T>>();
    }

    template <class F> decltype(auto) visit(F&& f) & { return visit_impl(*this, std::forward<F>(f)); }

    template <class F> decltype(auto) visit(F&& f) const& { return visit_impl(*this, std::forward<F>(f)); }

    // Returns a NON-OWNING wrapper to the current object.
    // If *this owns the object, the returned variant references *this's internal storage.
    [[nodiscard]]
    ref_variant as_ref() const& noexcept {
        return from_raw_index(index_, ptr_);
    }

    [[nodiscard]]
    ref_variant as_ref() && = delete;

    [[nodiscard]]
    ref_variant clone() const {
        ref_variant cloned(no_init_t{});
        visit([&cloned]<class T>(const T& x) { cloned.template emplace<T>(x); });
        return cloned;
    }

    [[nodiscard]]
    void *raw_pointer() noexcept {
        return ptr_;
    }

    [[nodiscard]]
    const void *raw_pointer() const noexcept {
        return ptr_;
    }

    [[nodiscard]]
    tagged_handle encode_tagged_handle() const noexcept {
        tagged_handle u = reinterpret_cast<std::uintptr_t>(ptr_);
        u |= (static_cast<tagged_handle>(index_) << 56);
        return u;
    }

    [[nodiscard]]
    static ref_variant decode_tagged_handle(tagged_handle tagged) noexcept {
        static_assert(sizeof...(Ts) <= 256, "decode_tagged_ptr needs at most 256 alternatives");

        const std::size_t idx = static_cast<std::uint8_t>(tagged >> 56);

        void *p = reinterpret_cast<void *>(static_cast<std::uintptr_t>(tagged & 0x00FF'FFFF'FFFF'FFFFull));

        assert(idx < sizeof...(Ts));
        assert(p != nullptr);

        return from_raw_index(idx, p);
    }
};
} // namespace luxon::ser
