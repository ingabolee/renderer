// include/incremental/node_handle.hpp
// Typed handle to a node in an incremental::Graph.
// Handles are lightweight tokens, safe to copy and pass by value.
#pragma once

#include <incremental/detail/graph_storage.hpp>
#include <cstddef>
#include <functional>

namespace incremental {

// Forward declaration (Graph is defined in graph.hpp)
class Graph;

// ── NodeHandle<T> ─────────────────────────────────────────────────────────────
// A typed, graph-bound reference to a node whose committed value is of type T.
//
// Lifetime rule: a NodeHandle is valid as long as the Graph that owns it is
// alive. Nodes are never removed from a Graph, so handles do not dangle.
//
// A default-constructed NodeHandle is invalid (id == kInvalidNodeId).
// Passing an invalid handle is detected and throws HandleError.
//
// A handle obtained from graph A must not be used with graph B. Handles carry
// a graph pointer for ownership validation.

template <typename T>
class NodeHandle {
public:
    using value_type = T;

    // Default: invalid handle
    NodeHandle() = default;

    // Internal constructor (called only by Graph)
    NodeHandle(detail::NodeId id, const Graph* owner_graph)
        : id_(id), owner_(owner_graph) {}

    [[nodiscard]] detail::NodeId id() const noexcept { return id_; }
    [[nodiscard]] const Graph*   graph() const noexcept { return owner_; }

    [[nodiscard]] bool valid() const noexcept {
        return id_ != detail::kInvalidNodeId && owner_ != nullptr;
    }

    explicit operator bool() const noexcept { return valid(); }

    // Equality: same node in the same graph.
    [[nodiscard]] bool operator==(const NodeHandle& other) const noexcept {
        return id_ == other.id_ && owner_ == other.owner_;
    }
    [[nodiscard]] bool operator!=(const NodeHandle& other) const noexcept {
        return !(*this == other);
    }

private:
    detail::NodeId id_{detail::kInvalidNodeId};
    const Graph*   owner_{nullptr};
};

} // namespace incremental

// ── std::hash specialization ──────────────────────────────────────────────────
namespace std {
template <typename T>
struct hash<incremental::NodeHandle<T>> {
    std::size_t operator()(const incremental::NodeHandle<T>& h) const noexcept {
        return std::hash<std::size_t>{}(h.id());
    }
};
} // namespace std
