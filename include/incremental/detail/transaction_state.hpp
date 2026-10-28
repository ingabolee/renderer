// include/incremental/detail/transaction_state.hpp
// Staging area for a pending transaction.
#pragma once

#include <incremental/detail/graph_storage.hpp>
#include <incremental/detail/cost_counters.hpp>
#include <unordered_map>
#include <vector>

namespace incremental::detail {

// ── TransactionState ──────────────────────────────────────────────────────────
// Holds staged input changes and computed values for a pending commit.
// Nothing here modifies the committed graph state until publish() is called.

struct TransactionState {
    // Staged input values: node -> new value (not yet committed).
    std::unordered_map<NodeId, TypeErasedValue> staged_inputs;

    // Computed values for affected derived nodes (from preparation phase).
    std::unordered_map<NodeId, TypeErasedValue> computed_values;

    // Nodes affected by this transaction (sorted by topo_order).
    std::vector<NodeId> affected_nodes;

    // Which inputs actually changed (value differed from committed).
    std::vector<NodeId> changed_inputs;

    // Performance stats accumulated during preparation.
    CostCounters counters;

    void clear() {
        staged_inputs.clear();
        computed_values.clear();
        affected_nodes.clear();
        changed_inputs.clear();
        counters.reset();
    }

    [[nodiscard]] bool empty() const noexcept {
        return staged_inputs.empty();
    }
};

} // namespace incremental::detail
