// include/incremental/detail/type_erasure.hpp
// Type-erased node value storage for the heterogeneous dependency graph.
// Internal implementation detail - not part of the public API.
#pragma once

#include <any>
#include <typeindex>
#include <typeinfo>
#include <string>
#include <functional>
#include <memory>
#include <stdexcept>

namespace incremental::detail {

// NodeId: a stable, graph-unique identifier for each node.
using NodeId = std::size_t;
static constexpr NodeId kInvalidNodeId = static_cast<NodeId>(-1);

// ── TypeErasedValue ───────────────────────────────────────────────────────────
// Wraps an arbitrary value type using std::any.
// Requirements on T: CopyConstructible, CopyAssignable.
// The strong exception guarantee on transactions requires that value types be
// copy-constructible so we can stage new values before committing.

class TypeErasedValue {
public:
    TypeErasedValue() = default;

    template <typename T>
    explicit TypeErasedValue(T value)
        : value_(std::make_any<T>(std::move(value)))
        , type_index_(std::type_index(typeid(T)))
        , type_name_(typeid(T).name())
    {}

    [[nodiscard]] bool has_value() const noexcept { return value_.has_value(); }

    [[nodiscard]] std::type_index type() const noexcept { return type_index_; }
    [[nodiscard]] const std::string& type_name() const noexcept { return type_name_; }

    template <typename T>
    [[nodiscard]] bool holds() const noexcept {
        return value_.has_value() && type_index_ == std::type_index(typeid(T));
    }

    template <typename T>
    [[nodiscard]] const T& get() const {
        if (!holds<T>()) {
            throw std::bad_any_cast{};
        }
        return std::any_cast<const T&>(value_);
    }

    template <typename T>
    [[nodiscard]] T& get_mut() {
        if (!holds<T>()) {
            throw std::bad_any_cast{};
        }
        return std::any_cast<T&>(value_);
    }

    // Replace value with a new value of the same type (RAII-safe copy).
    template <typename T>
    void assign(T new_value) {
        if (!holds<T>()) {
            throw std::bad_any_cast{};
        }
        value_ = std::make_any<T>(std::move(new_value));
    }

    // Create a copy of this erased value (for staging area).
    [[nodiscard]] TypeErasedValue clone() const {
        return *this; // std::any is copy-constructible
    }

    void swap(TypeErasedValue& other) noexcept {
        value_.swap(other.value_);
        std::swap(type_index_, other.type_index_);
        std::swap(type_name_, other.type_name_);
    }

private:
    std::any value_;
    std::type_index type_index_{typeid(void)};
    std::string type_name_;
};

// ── ErasedFormula ─────────────────────────────────────────────────────────────
// Stores a type-erased formula (callable) that reads from the graph state
// and produces a new value.
// ReadContext is defined in incremental/read_view.hpp; forward-declared here.

} // namespace incremental::detail

namespace incremental { class ReadContext; } // forward declaration

namespace incremental::detail {

using FormulaFn = std::function<TypeErasedValue(const incremental::ReadContext&)>;

} // namespace incremental::detail
