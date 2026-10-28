// include/incremental/change_event.hpp
// Event type describing what changed after a successful commit.
#pragma once

#include <incremental/detail/graph_storage.hpp>
#include <incremental/detail/cost_counters.hpp>
#include <vector>
#include <string>

namespace incremental {

// ── CommitResult ──────────────────────────────────────────────────────────────
// Returned by Transaction::commit(). Contains statistics and node lists.

struct CommitResult {
    bool committed{false};           ///< true if successfully committed
    std::string error_message;       ///< set if committed == false

    detail::CostCounters counters;

    // Node IDs that were actually changed (inputs or recomputed deriveds).
    std::vector<detail::NodeId> changed_node_ids;

    // Node IDs that were in the affected set but skipped (i.e., no transaction
    // included them - always empty for incremental; populated by naïve evaluator).
    std::vector<detail::NodeId> skipped_node_ids;
};

// ── ChangeEvent ───────────────────────────────────────────────────────────────
// Passed to observer callbacks, describing which nodes changed values.

struct ChangeEvent {
    // All node IDs whose committed value changed in this transaction.
    std::vector<detail::NodeId> changed_ids;

    // Performance counters for this commit.
    detail::CostCounters counters;

    // Whether this event came from a normally committed tx or an undo/redo.
    enum class Origin { Normal, Undo, Redo, QueuedCallback } origin{Origin::Normal};
};

} // namespace incremental
