// include/incremental/error.hpp
// Error types and error codes for the incremental library.
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <format>
#include <vector>

namespace incremental {

// ── Error codes ───────────────────────────────────────────────────────────────

enum class ErrorCode {
    // Graph/handle errors
    InvalidHandle,          ///< Handle is null or malformed
    WrongGraph,             ///< Handle belongs to a different graph
    TypeMismatch,           ///< Requested type does not match node's type
    NodeNotFound,           ///< Node ID does not exist in this graph
    NotAnInputNode,         ///< Operation requires an input node (not derived)
    NotADerivedNode,        ///< Operation requires a derived node

    // Topology errors
    CycleDetected,          ///< Proposed edges would create a cycle
    UndeclaredDependency,   ///< Formula read a node not listed as a dependency

    // Transaction errors
    TransactionActive,      ///< Cannot start a nested transaction
    NoActiveTransaction,    ///< Commit/rollback with no active transaction
    FormulaException,       ///< A formula threw during preparation
    ValidationFailed,       ///< Pre-commit validation rejected the update

    // Reentrancy / lifecycle
    ReentrancyLimitExceeded,///< Callback feedback loop exceeded configured limit
    IllegalStateTransition, ///< Internal lifecycle state machine error

    // Undo/redo
    NothingToUndo,
    NothingToRedo,

    // Generic
    InternalError,          ///< Bug in the library – please report
};

[[nodiscard]] inline std::string_view error_code_name(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidHandle:           return "InvalidHandle";
    case ErrorCode::WrongGraph:              return "WrongGraph";
    case ErrorCode::TypeMismatch:            return "TypeMismatch";
    case ErrorCode::NodeNotFound:            return "NodeNotFound";
    case ErrorCode::NotAnInputNode:          return "NotAnInputNode";
    case ErrorCode::NotADerivedNode:         return "NotADerivedNode";
    case ErrorCode::CycleDetected:           return "CycleDetected";
    case ErrorCode::UndeclaredDependency:    return "UndeclaredDependency";
    case ErrorCode::TransactionActive:       return "TransactionActive";
    case ErrorCode::NoActiveTransaction:     return "NoActiveTransaction";
    case ErrorCode::FormulaException:        return "FormulaException";
    case ErrorCode::ValidationFailed:        return "ValidationFailed";
    case ErrorCode::ReentrancyLimitExceeded: return "ReentrancyLimitExceeded";
    case ErrorCode::IllegalStateTransition:  return "IllegalStateTransition";
    case ErrorCode::NothingToUndo:           return "NothingToUndo";
    case ErrorCode::NothingToRedo:           return "NothingToRedo";
    case ErrorCode::InternalError:           return "InternalError";
    }
    return "Unknown";
}

// ── Base exception ────────────────────────────────────────────────────────────

class Error : public std::runtime_error {
public:
    explicit Error(ErrorCode code, std::string message)
        : std::runtime_error(message)
        , code_(code)
        , message_(std::move(message))
    {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& detail() const noexcept { return message_; }

    [[nodiscard]] std::string formatted() const {
        return std::format("[{}] {}", error_code_name(code_), message_);
    }

private:
    ErrorCode code_;
    std::string message_;
};

// ── Typed exception subclasses ─────────────────────────────────────────────────

class HandleError : public Error {
public:
    explicit HandleError(ErrorCode code, std::string message)
        : Error(code, std::move(message)) {}
};

class TopologyError : public Error {
public:
    // cycle_path contains the node names forming the cycle (may be empty if unknown)
    std::vector<std::string> cycle_path;

    explicit TopologyError(std::string message, std::vector<std::string> path = {})
        : Error(ErrorCode::CycleDetected, std::move(message))
        , cycle_path(std::move(path))
    {}
};

class TransactionError : public Error {
public:
    explicit TransactionError(ErrorCode code, std::string message)
        : Error(code, std::move(message)) {}
};

class FormulaError : public Error {
public:
    std::string node_name;
    std::exception_ptr original; ///< The exception thrown by the formula

    explicit FormulaError(std::string node, std::exception_ptr ex, std::string message)
        : Error(ErrorCode::FormulaException, std::move(message))
        , node_name(std::move(node))
        , original(std::move(ex))
    {}
};

class ReentrancyError : public Error {
public:
    int depth;
    explicit ReentrancyError(int d, std::string message)
        : Error(ErrorCode::ReentrancyLimitExceeded, std::move(message))
        , depth(d)
    {}
};

// ── Convenience factories ─────────────────────────────────────────────────────

inline HandleError make_invalid_handle(std::string_view context) {
    return HandleError(ErrorCode::InvalidHandle,
        std::format("Invalid handle in {}: handle is null or malformed", context));
}

inline HandleError make_wrong_graph(std::string_view context) {
    return HandleError(ErrorCode::WrongGraph,
        std::format("Wrong-graph handle in {}: handle belongs to a different graph", context));
}

inline HandleError make_type_mismatch(std::string_view expected, std::string_view actual) {
    return HandleError(ErrorCode::TypeMismatch,
        std::format("Type mismatch: expected '{}', but node holds '{}'", expected, actual));
}

} // namespace incremental
