// include/incremental/transaction.hpp
// Transaction class: groups input changes into one atomic commit.
//
// LIFECYCLE:
//   1. graph.transaction()    -> creates a Transaction (Idle)
//   2. tx.set(handle, value)  -> stages a change (no commit yet)
//   3. tx.commit()            -> Prepare -> Validate -> Publish -> Notify
//   or tx goes out of scope   -> rollback (uncommitted changes discarded)
//
// EXCEPTION SAFETY (strong guarantee):
//   If any formula throws during Prepare, no committed values change.
//   The prepare phase computes ALL new derived values before touching
//   the committed graph state.
//
//   If Publish succeeds (it should not throw for copy types), the new state
//   is live. Notify errors (observer exceptions) are collected but cannot
//   roll back an already-published commit.
//
// REENTRANCY:
//   Mutations from observers during Notify are queued (via CallbackQueue).
//   They are processed as separate transactions after Notify completes.
#pragma once

#include <incremental/change_event.hpp>
#include <incremental/node_handle.hpp>
#include <incremental/error.hpp>
#include <incremental/detail/transaction_state.hpp>
#include <incremental/detail/topology.hpp>
#include <incremental/detail/journal.hpp>
#include <incremental/read_view.hpp>
#include <string>
#include <vector>
#include <format>

namespace incremental {

class Graph;  // forward (graph.hpp brings in transaction.hpp)

// ── Transaction ───────────────────────────────────────────────────────────────
class Transaction {
public:
    explicit Transaction(Graph* g, std::string description = "")
        : graph_(g), description_(std::move(description)) {}

    // Non-copyable, movable.
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = default;
    Transaction& operator=(Transaction&&) = default;

    // Destructor: uncommitted staging area is silently discarded.
    ~Transaction() = default;

    // ── Staging inputs ───────────────────────────────────────────────────────

    /// Stage a new value for an input node. Does not modify the graph.
    template <typename T>
    Transaction& set(const NodeHandle<T>& handle, T new_value) {
        if (!handle.valid()) throw make_invalid_handle("Transaction::set");
        if (handle.graph() != graph_) throw make_wrong_graph("Transaction::set");
        if (!graph_->storage().contains(handle.id())) {
            throw HandleError(ErrorCode::NodeNotFound,
                std::format("Transaction::set: node id={} not found", handle.id()));
        }
        const auto& rec = graph_->storage().at(handle.id());
        if (rec.kind != detail::NodeKind::Input) {
            throw TransactionError(ErrorCode::NotAnInputNode,
                std::format("Transaction::set: '{}' is a derived node. "
                            "Only input nodes can be directly assigned.",
                            rec.name));
        }
        if (!rec.committed_value.template holds<T>()) {
            throw make_type_mismatch(typeid(T).name(), rec.committed_value.type_name());
        }
        state_.staged_inputs[handle.id()] = detail::TypeErasedValue{std::move(new_value)};
        return *this;
    }

    // ── Commit ───────────────────────────────────────────────────────────────

    /// Prepare, validate, and commit all staged changes.
    /// Returns a CommitResult with statistics and success flag.
    /// On formula error: returns with committed==false, graph unchanged.
    CommitResult commit() {
        return graph_->commit_transaction(state_, description_);
    }

    // ── Introspection ─────────────────────────────────────────────────────────

    [[nodiscard]] bool empty() const noexcept { return state_.empty(); }
    [[nodiscard]] std::size_t staged_count() const noexcept {
        return state_.staged_inputs.size();
    }

private:
    Graph* graph_;
    std::string description_;
    detail::TransactionState state_;
};

// ── Graph::transaction() ─────────────────────────────────────────────────────
// Defined here so Graph and Transaction are mutually complete.
inline Transaction Graph::transaction(std::string description) {
    return Transaction{this, std::move(description)};
}

// ── Graph::commit_transaction() ──────────────────────────────────────────────
// The actual commit algorithm. Separated here so the class definitions are
// complete before the implementation.
inline CommitResult Graph::commit_transaction(
    detail::TransactionState& tx,
    const std::string& description,
    detail::UndoRedoJournal* /*unused_override*/,
    ChangeEvent::Origin origin)
{
    CommitResult result;

    if (tx.empty()) {
        // Empty transaction: do nothing, report as committed.
        result.committed = true;
        return result;
    }

    // ── PHASE 1: Identify changed inputs ─────────────────────────────────────
    // Compute which staged inputs differ from committed values.
    std::vector<detail::NodeId> changed_inputs;

    detail::JournalEntry journal_entry;
    journal_entry.description = description.empty() ? "commit" : description;

    for (auto& [id, new_val] : tx.staged_inputs) {
        const auto& rec = storage_.at(id);
        // Compare by type match + string representation is impractical for arbitrary T.
        // We always treat staged as "changed" — the formula correctness relies on
        // the value being updated regardless.
        changed_inputs.push_back(id);
        journal_entry.changes.push_back({
            id,
            rec.committed_value.clone(),  // before
            new_val.clone()               // after
        });
    }

    tx.changed_inputs = changed_inputs;
    tx.counters.inputs_changed = changed_inputs.size();

    // ── PHASE 2: Compute affected subgraph ───────────────────────────────────
    std::vector<detail::NodeId> affected = detail::compute_affected_nodes(storage_, changed_inputs);
    tx.affected_nodes = affected;
    tx.counters.nodes_invalidated = affected.size();
    tx.counters.nodes_skipped = derived_count() - affected.size();

    // ── PHASE 3: Prepare (evaluate all affected formulas) ────────────────────
    // All formula evaluations happen here, BEFORE modifying any committed value.
    // If any formula throws, we return with no graph mutation.
    for (detail::NodeId node_id : affected) {
        const auto& rec = storage_.at(node_id);
        if (rec.kind != detail::NodeKind::Derived) continue;

        try {
            ReadContext ctx{&storage_, &tx, rec.dependencies};
            detail::TypeErasedValue computed = rec.formula(ctx);
            tx.computed_values[node_id] = std::move(computed);
            ++tx.counters.nodes_recomputed;
        } catch (const std::exception& ex) {
            ++tx.counters.formula_exceptions;
            result.committed = false;
            result.error_message = std::format(
                "Formula '{}' threw during preparation: {}",
                rec.name, ex.what());
            result.counters = tx.counters;
            return result; // Strong guarantee: no committed values changed
        } catch (...) {
            ++tx.counters.formula_exceptions;
            result.committed = false;
            result.error_message = std::format(
                "Formula '{}' threw an unknown exception during preparation.", rec.name);
            result.counters = tx.counters;
            return result;
        }
    }

    // ── PHASE 4: Publish ─────────────────────────────────────────────────────
    // At this point all formulas succeeded. Atomically update committed values.
    // NOTE: std::any copy/move should not throw for any well-behaved type.
    // If it does, we are in the basic guarantee territory for those types.

    // Publish staged inputs.
    for (auto& [id, new_val] : tx.staged_inputs) {
        storage_.at(id).committed_value.swap(new_val);
        result.changed_node_ids.push_back(id);
    }

    // Publish computed derived values.
    for (auto& [id, new_val] : tx.computed_values) {
        storage_.at(id).committed_value.swap(new_val);
        result.changed_node_ids.push_back(id);
    }

    result.committed = true;
    result.counters = tx.counters;

    // Record in journal (only normal commits, not undo/redo replays).
    if (origin == ChangeEvent::Origin::Normal) {
        journal_.push(std::move(journal_entry));
    }

    // ── PHASE 5: Notify observers ────────────────────────────────────────────
    // Observers see the fully committed state.
    ChangeEvent event;
    event.changed_ids = result.changed_node_ids;
    event.counters = tx.counters;
    event.origin = origin;

    ++notification_depth_;
    std::vector<std::string> observer_errors;
    notify_observers(event, observer_errors);
    --notification_depth_;

    result.counters.observer_calls = static_cast<std::size_t>(observers_.size());

    // ── PHASE 6: Drain queued mutations ──────────────────────────────────────
    // Process any mutations requested by observers (FIFO order).
    while (callback_queue_.has_pending()) {
        try {
            callback_queue_.increment_drain_depth();
            auto mut = callback_queue_.pop();
            if (mut) mut();
        } catch (const ReentrancyError&) {
            callback_queue_.clear();
            throw;
        }
    }
    callback_queue_.reset_drain_depth();

    return result;
}

// ── Graph::undo() ─────────────────────────────────────────────────────────────
inline CommitResult Graph::undo() {
    if (!journal_.can_undo()) {
        CommitResult r;
        r.committed = false;
        r.error_message = "Nothing to undo.";
        return r;
    }

    const detail::JournalEntry& entry = journal_.undo_entry();

    detail::TransactionState tx;
    for (const auto& change : entry.changes) {
        tx.staged_inputs[change.node_id] = change.before_value.clone();
    }

    // Don't push to journal during undo.
    detail::UndoRedoJournal temp_journal(1);
    CommitResult result = commit_transaction(tx, "undo", nullptr,
                                             ChangeEvent::Origin::Undo);
    if (result.committed) {
        journal_.commit_undo();
    }
    return result;
}

// ── Graph::redo() ─────────────────────────────────────────────────────────────
inline CommitResult Graph::redo() {
    if (!journal_.can_redo()) {
        CommitResult r;
        r.committed = false;
        r.error_message = "Nothing to redo.";
        return r;
    }

    const detail::JournalEntry& entry = journal_.redo_entry();

    detail::TransactionState tx;
    for (const auto& change : entry.changes) {
        tx.staged_inputs[change.node_id] = change.after_value.clone();
    }

    CommitResult result = commit_transaction(tx, "redo", nullptr,
                                             ChangeEvent::Origin::Redo);
    if (result.committed) {
        journal_.commit_redo();
    }
    return result;
}

} // namespace incremental
