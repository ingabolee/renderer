// include/incremental/customization_points.hpp
// Generic for_each customization point with documented dispatch priority:
//
//   1. Member function: object.for_each(callback)
//   2. ADL free function: incremental_for_each(object, callback)
//         (found via argument-dependent lookup)
//   3. Standard range: begin/end iteration
//   4. Index-based: object.size() + object[index]
//
// Usage:
//   incremental::for_each(container, callback);
//
// Callback signature: void(const element_type&) or void(element_type)
//
// ADL safety: incremental::for_each does NOT participate in ADL itself.
// We define it inside the `incremental` namespace. User overloads must be
// named `incremental_for_each` in the ADL-associated namespace of their type.
//
// Dispatch priority test: The highest-priority satisfied protocol wins.
// If both member and ADL are available, member wins.
// If both ADL and range are available, ADL wins.
// If both range and index are available, range wins.
//
// Unsupported type: static_assert with a clear message.
#pragma once

#include <concepts>
#include <ranges>
#include <cstddef>
#include <type_traits>

namespace incremental {

namespace detail_cp {

// ── Protocol detection concepts ────────────────────────────────────────────────

// Protocol 1: obj.for_each(cb)
template <typename T, typename Cb>
concept HasMemberForEach = requires(T& obj, Cb cb) {
    obj.for_each(cb);
};

// Protocol 2: incremental_for_each(obj, cb)  [ADL]
// We use a separate function object to avoid any self-lookup issue.
// The free function name is `incremental_for_each`.
void incremental_for_each() = delete;  // prevent unintended lookup

template <typename T, typename Cb>
concept HasAdlForEach = requires(T& obj, Cb cb) {
    incremental_for_each(obj, cb);
};

// Protocol 3: range (begin/end)
template <typename T>
concept IsRange = std::ranges::range<T>;

// Protocol 4: size() + operator[]
template <typename T>
concept HasIndexAccess = requires(T& obj) {
    { obj.size() } -> std::convertible_to<std::size_t>;
    obj[std::size_t{0}];
};

// ── Dispatcher ────────────────────────────────────────────────────────────────

template <typename T, typename Cb>
void dispatch(T& obj, Cb&& cb) {
    if constexpr (HasMemberForEach<T, Cb>) {
        // Protocol 1: member
        obj.for_each(std::forward<Cb>(cb));
    } else if constexpr (HasAdlForEach<T, Cb>) {
        // Protocol 2: ADL
        incremental_for_each(obj, std::forward<Cb>(cb));
    } else if constexpr (IsRange<T>) {
        // Protocol 3: range
        for (auto&& elem : obj) {
            cb(std::forward<decltype(elem)>(elem));
        }
    } else if constexpr (HasIndexAccess<T>) {
        // Protocol 4: index-based
        std::size_t n = obj.size();
        for (std::size_t i = 0; i < n; ++i) {
            cb(obj[i]);
        }
    } else {
        static_assert(
            sizeof(T) == 0,
            "incremental::for_each: unsupported type. "
            "Provide one of:\n"
            "  (1) obj.for_each(cb)  [member protocol]\n"
            "  (2) incremental_for_each(obj, cb) in obj's namespace  [ADL protocol]\n"
            "  (3) begin/end iteration (std::ranges::range)  [range protocol]\n"
            "  (4) obj.size() + obj[index]  [index protocol]");
    }
}

// const overload
template <typename T, typename Cb>
void dispatch(const T& obj, Cb&& cb) {
    if constexpr (HasMemberForEach<const T, Cb>) {
        obj.for_each(std::forward<Cb>(cb));
    } else if constexpr (HasAdlForEach<const T, Cb>) {
        incremental_for_each(obj, std::forward<Cb>(cb));
    } else if constexpr (IsRange<const T>) {
        for (auto&& elem : obj) {
            cb(std::forward<decltype(elem)>(elem));
        }
    } else if constexpr (HasIndexAccess<const T>) {
        std::size_t n = obj.size();
        for (std::size_t i = 0; i < n; ++i) {
            cb(obj[i]);
        }
    } else {
        static_assert(sizeof(T) == 0,
            "incremental::for_each: unsupported type (const overload).");
    }
}

} // namespace detail_cp

// ── Public API ────────────────────────────────────────────────────────────────

/// Visit each element of `obj` using the highest-priority supported protocol.
/// Dispatch priority: member > ADL > range > index.
template <typename T, typename Cb>
void for_each(T& obj, Cb&& cb) {
    detail_cp::dispatch(obj, std::forward<Cb>(cb));
}

template <typename T, typename Cb>
void for_each(const T& obj, Cb&& cb) {
    detail_cp::dispatch(obj, std::forward<Cb>(cb));
}

} // namespace incremental
