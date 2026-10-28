// include/incremental/detail/cost_counters.hpp
// Performance counters tracking invalidation and recomputation statistics.
#pragma once

#include <cstddef>
#include <string>
#include <format>

namespace incremental::detail {

struct CostCounters {
    std::size_t inputs_changed{0};       ///< Number of inputs staged in this transaction
    std::size_t nodes_invalidated{0};    ///< Nodes marked as affected
    std::size_t nodes_recomputed{0};     ///< Derived nodes actually evaluated
    std::size_t nodes_skipped{0};        ///< Nodes not in affected set (preserved)
    std::size_t formula_exceptions{0};   ///< Formulas that threw
    std::size_t observer_calls{0};       ///< Observer notifications dispatched
    std::size_t queued_transactions{0};  ///< Mutations queued during notification

    void reset() { *this = {}; }

    /// Total nodes examined (invalidated + skipped)
    [[nodiscard]] std::size_t total_derived() const noexcept {
        return nodes_invalidated + nodes_skipped;
    }
};

struct BenchmarkStats {
    std::size_t node_count{0};
    std::size_t edge_count{0};
    std::size_t input_mutations{0};
    std::size_t nodes_affected{0};
    std::size_t formula_evaluations_full{0};
    std::size_t formula_evaluations_incremental{0};
    double elapsed_full_ms{0.0};
    double elapsed_incremental_ms{0.0};
    bool results_match{true};

    [[nodiscard]] double speedup() const noexcept {
        if (elapsed_incremental_ms <= 0.0) return 0.0;
        return elapsed_full_ms / elapsed_incremental_ms;
    }
};

} // namespace incremental::detail
