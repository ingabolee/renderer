// include/incremental/read_view.hpp
// ReadView: a controlled interface through which formulas read node values.
// During preparation, staged (new) values are visible for changed inputs.
// During normal reads, committed values are returned.
#pragma once

#include <incremental/node_handle.hpp>
#include <incremental/error.hpp>
#include <incremental/detail/graph_storage.hpp>
#include <incremental/detail/transaction_state.hpp>
#include <unordered_map>
#include <typeinfo>
#include <format>

namespace incremental {

// ── ReadContext ───────────────────────────────────────────────────────────────
// Internal context passed to the formula. Passed by reference to the formula.
// Formulas must only read values for nodes that they declared as dependencies.
// Any read of an undeclared dependency throws UndeclaredDependency.

class ReadContext {
public:
    // During a commit preparation: staged + committed
    ReadContext(
        const detail::GraphStorage*       storage,
        const detail::TransactionState*   tx_state,     // may be null (read-only)
        const std::vector<detail::NodeId>& declared_deps
    )
        : storage_(storage)
        , tx_state_(tx_state)
        , declared_deps_(declared_deps)
    {}

    // Read the value of a node through its typed handle.
    template <typename T>
    [[nodiscard]] const T& get(const NodeHandle<T>& handle) const {
        validate_handle(handle);
        validate_declared(handle.id());

        detail::NodeId id = handle.id();

        // If we have a staged transaction and this is a staged input, use staged value.
        if (tx_state_) {
            auto it = tx_state_->staged_inputs.find(id);
            if (it != tx_state_->staged_inputs.end()) {
                if (!it->second.template holds<T>()) {
                    throw make_type_mismatch(typeid(T).name(), it->second.type_name());
                }
                return it->second.template get<T>();
            }
            // Check computed derived values in staging area.
            auto cit = tx_state_->computed_values.find(id);
            if (cit != tx_state_->computed_values.end()) {
                if (!cit->second.template holds<T>()) {
                    throw make_type_mismatch(typeid(T).name(), cit->second.type_name());
                }
                return cit->second.template get<T>();
            }
        }

        // Fall back to committed value.
        const auto& rec = storage_->at(id);
        if (!rec.committed_value.template holds<T>()) {
            throw make_type_mismatch(typeid(T).name(), rec.committed_value.type_name());
        }
        return rec.committed_value.template get<T>();
    }

    // Validate that this handle belongs to the correct graph.
    template <typename T>
    void validate_handle(const NodeHandle<T>& handle) const {
        if (!handle.valid()) {
            throw make_invalid_handle("ReadContext::get");
        }
        if (!storage_->contains(handle.id())) {
            throw HandleError(ErrorCode::NodeNotFound,
                std::format("Node {} not found in graph", handle.id()));
        }
    }

private:
    void validate_declared(detail::NodeId id) const {
        bool found = false;
        for (detail::NodeId dep : declared_deps_) {
            if (dep == id) { found = true; break; }
        }
        if (!found) {
            // Allow reading from storage to get the name for the error message.
            std::string name = storage_->contains(id) ? storage_->at(id).name : "<unknown>";
            throw HandleError(ErrorCode::UndeclaredDependency,
                std::format("Formula attempted to read undeclared dependency '{}' (id={}). "
                            "Declare all dependencies when calling graph.derive().", name, id));
        }
    }

    const detail::GraphStorage*       storage_;
    const detail::TransactionState*   tx_state_;
    const std::vector<detail::NodeId>& declared_deps_;
};

} // namespace incremental
