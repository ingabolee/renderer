// include/incremental/detail/graph_storage.hpp
// Internal node storage and adjacency structures.
#pragma once

#include <incremental/detail/type_erasure.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <optional>
#include <cassert>

namespace incremental::detail {

// ── NodeKind ──────────────────────────────────────────────────────────────────
enum class NodeKind { Input, Derived };

// ── NodeRecord ────────────────────────────────────────────────────────────────
// Central per-node data stored in GraphStorage.
struct NodeRecord {
    NodeId       id;
    NodeKind     kind;
    std::string  name;

    // Committed value (stable after each successful transaction).
    TypeErasedValue committed_value;

    // Dependencies: node IDs this node depends ON (only for Derived nodes).
    std::vector<NodeId> dependencies;

    // Dependents: node IDs that depend ON this node (reverse edges).
    std::vector<NodeId> dependents;

    // Formula: produces a new value given committed+staged state (Derived only).
    FormulaFn formula; // null for Input nodes

    // Topology order (updated on every structural change).
    // Lower means evaluated earlier.
    int topo_order{0};

    // Dirty flag: set during transaction preparation.
    mutable bool dirty{false};
};

// ── GraphStorage ──────────────────────────────────────────────────────────────
// Owns all nodes. Provides O(1) lookup by NodeId.
class GraphStorage {
public:
    // ── Node creation ───────────────────────────────────────────────────────

    [[nodiscard]] NodeId add_input(std::string name, TypeErasedValue initial_value) {
        NodeId id = next_id_++;
        NodeRecord rec;
        rec.id = id;
        rec.kind = NodeKind::Input;
        rec.name = std::move(name);
        rec.committed_value = std::move(initial_value);
        nodes_.emplace(id, std::move(rec));
        order_.push_back(id);
        return id;
    }

    [[nodiscard]] NodeId add_derived(
        std::string name,
        std::vector<NodeId> deps,
        FormulaFn formula,
        TypeErasedValue placeholder_value
    ) {
        NodeId id = next_id_++;
        NodeRecord rec;
        rec.id = id;
        rec.kind = NodeKind::Derived;
        rec.name = std::move(name);
        rec.dependencies = deps;
        rec.formula = std::move(formula);
        rec.committed_value = std::move(placeholder_value);

        // Link reverse edges.
        for (NodeId dep : rec.dependencies) {
            if (nodes_.count(dep)) {
                nodes_.at(dep).dependents.push_back(id);
            }
        }

        nodes_.emplace(id, std::move(rec));
        order_.push_back(id);
        return id;
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    [[nodiscard]] bool contains(NodeId id) const noexcept {
        return nodes_.count(id) != 0;
    }

    [[nodiscard]] NodeRecord& at(NodeId id) { return nodes_.at(id); }
    [[nodiscard]] const NodeRecord& at(NodeId id) const { return nodes_.at(id); }

    [[nodiscard]] const std::vector<NodeId>& insertion_order() const noexcept {
        return order_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }

    // Iterate all nodes in insertion order.
    template <typename Fn>
    void for_each(Fn&& fn) {
        for (NodeId id : order_) {
            fn(nodes_.at(id));
        }
    }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (NodeId id : order_) {
            fn(nodes_.at(id));
        }
    }

    // ── Topology sort metadata ────────────────────────────────────────────────
    // Update the topo_order field for every node using Kahn's algorithm.
    // Must be called after any structural change.
    // Returns false if a cycle is detected.
    [[nodiscard]] bool recompute_topo_order() {
        // Compute in-degree for each node.
        std::unordered_map<NodeId, int> in_deg;
        for (NodeId id : order_) {
            const auto& rec = nodes_.at(id);
            in_deg[id] = static_cast<int>(rec.dependencies.size());
        }

        std::vector<NodeId> queue;
        for (auto& [id, deg] : in_deg) {
            if (deg == 0) queue.push_back(id);
        }

        int order = 0;
        std::vector<NodeId> sorted;
        sorted.reserve(nodes_.size());

        while (!queue.empty()) {
            NodeId cur = queue.back(); queue.pop_back();
            nodes_.at(cur).topo_order = order++;
            sorted.push_back(cur);
            for (NodeId dep : nodes_.at(cur).dependents) {
                if (--in_deg[dep] == 0) {
                    queue.push_back(dep);
                }
            }
        }

        if (sorted.size() != nodes_.size()) {
            return false; // cycle
        }

        // Rebuild insertion_order in topo order so iteration is always valid.
        // We preserve original order for inputs (topo=0 ties resolved by insertion).
        order_ = sorted;
        return true;
    }

private:
    std::unordered_map<NodeId, NodeRecord> nodes_;
    std::vector<NodeId> order_;
    NodeId next_id_{0};
};

} // namespace incremental::detail
