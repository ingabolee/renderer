// include/incremental/result.hpp
// Result<T> type for operations that may fail without throwing.
// Thin wrapper around std::expected<T, Error>.
#pragma once

#include <incremental/error.hpp>
#include <expected>
#include <functional>

namespace incremental {

/// Result<T> is an alias for std::expected<T, Error>.
/// Use it for operations that should not throw but may produce an error.
template <typename T>
using Result = std::expected<T, Error>;

/// Shorthand for a Result carrying no value (success/failure only).
using VoidResult = std::expected<void, Error>;

/// Create a successful result.
template <typename T>
[[nodiscard]] inline Result<T> ok(T value) {
    return Result<T>{std::move(value)};
}

/// Create a failure result.
template <typename T>
[[nodiscard]] inline Result<T> err(Error e) {
    return Result<T>{std::unexpect, std::move(e)};
}

/// Create a successful void result.
[[nodiscard]] inline VoidResult ok_void() {
    return VoidResult{};
}

/// Create a failure void result.
[[nodiscard]] inline VoidResult err_void(Error e) {
    return VoidResult{std::unexpect, std::move(e)};
}

} // namespace incremental
