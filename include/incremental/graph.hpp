// include/incremental/graph.hpp
// Central Graph class: the main public API for the incremental library.
//
// Usage overview:
//   incremental::Graph graph;
//   auto rev = graph.input<double>("Revenue", 120.0);
//   auto cost = graph.input<double>("Costs", 80.0);
//   auto profit = graph.derive<double>("Profit", {rev.id(), cost.id()},
//       [rev, cost](const auto& view){ return view.get(rev) - view.get(cost); });
//
//   auto tx = graph.transaction();
//   tx.set(rev, 130.0);
//   auto result = tx.commit();   // incremental recomputation
//
// Thread safety: Graph is NOT thread-safe. All access must be from one thread.
// Reentrancy: observers MAY queue mutations; see observer.hpp for the contract.
#pragma once

#include <incremental/error.hpp>
#include <incremental/result.hpp>
#include <incremental/node_handle.hpp>
#include <incremental/read_view.hpp>
#include <incremental/change_event.hpp>
#include <incremental/observer.hpp>
#include <incremental/detail/graph_storage.hpp>
#include <incremental/detail/topology.hpp>
#include <incremental/detail/transaction_state.hpp>
#include <incremental/detail/callback_queue.hpp>
#include <incremental/detail/journal.hpp>
#include <incremental/detail/cost_counters.hpp>

#include <concepts>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <typeindex>
#include <format>
#include <any>
#include <algorithm>

namespace incremental {

// ── Concept: node value type requirements ─────────────────────────────────────
// Value types must be CopyConstructible to allow staging in transactions.
template <typename T>
concept NodeValue = std::copy_constructible<T>;

// ── Transaction forward declaration ───────────────────────────────────────────
class Transaction;

// ── Graph ─────────────────────────────────────────────────────────────────────
class Graph {
public:
    // ── Graph lifecycle ──────────────────────────────────────────────────────

    explicit Graph(std::size_t journal_limit = 64, int reentrancy_limit = 32)
        : journal_(journal_limit)
        , callback_queue_(reentrancy_limit)
    {}

    // Non-copyable (nodes hold back-pointers to the graph).
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    // Movable (back-pointers would need updating; keep simple: only allow
    // creation, not move, since NodeHandle stores const Graph*).
    Graph(Graph&&) = delete;
    Graph& operator=(Graph&&) = delete;

    ~Graph() = default;

    // ── Node creation ────────────────────────────────────────────────────────

    /// Create an input node with an initial value.
    /// T must satisfy NodeValue (copy-constructible).
    template <NodeValue T>
    [[nodiscard]] NodeHandle<T> input(std::string name, T initial_value) {
        detail::NodeId id = storage_.add_input(
            std::move(name),
            detail::TypeErasedValue{std::move(initial_value)});
        if (!storage_.recompute_topo_order()) {
            throw Error(ErrorCode::InternalError,
                "Topological sort failed after adding input node (unreachable).");
        }
        return NodeHandle<T>{id, this};
    }

    /// Create a derived node.
    /// deps: the NodeIds this node depends on (must all be in this graph).
    /// formula: a callable (const ReadContext& view) -> T
    ///          MUST only call view.get(h) for handles whose ids are in deps.
    template <NodeValue T, typename Formula>
    [[nodiscard]] NodeHandle<T> derive(
        std::string name,
        std::vector<detail::NodeId> deps,
        Formula formula
    )
    requires std::invocable<Formula, const ReadContext&>
    {
        // Validate all deps.
        for (detail::NodeId dep : deps) {
            if (!storage_.contains(dep)) {
                throw HandleError(ErrorCode::NodeNotFound,
                    std::format("Dependency id={} not found in this graph", dep));
            }
        }

        // Create a type-erased formula wrapper that produces TypeErasedValue.
        detail::FormulaFn erased_formula = [formula=std::move(formula), deps]
            (const ReadContext& ctx) -> detail::TypeErasedValue {
                T result = formula(ctx);
                return detail::TypeErasedValue{std::move(result)};
            };

        // Compute an initial value synchronously (if no deps are broken).
        // This ensures derived nodes have a valid committed value immediately.
        detail::TypeErasedValue initial;
        try {
            ReadContext ctx{&storage_, nullptr, deps};
            initial = erased_formula(ctx);
        } catch (...) {
            // If initial evaluation fails, store a default-constructed value
            // or rethrow — we rethrow to surface formula bugs early.
            throw;
        }

        // Check proposed topology for cycles.
        // We need to check if adding deps from 'new node -> deps' could create
        // a cycle. Since the new node doesn't exist yet, we check that none of
        // the deps transitively depend on any dep that tries to reach the new node.
        // Practically: just verify the graph stays a DAG after adding.
        // We'll add tentatively and recompute topo; rollback on failure.
        detail::NodeId id = storage_.add_derived(
            std::move(name),
            deps,
            std::move(erased_formula),
            std::move(initial));

        if (!storage_.recompute_topo_order()) {
            // This is a cycle. Remove the node we just added.
            // Since GraphStorage doesn't support removal, and cycles can only be
            // introduced by adding deps pointing to already-reachable nodes,
            // we instead check BEFORE adding. Let's detect here and throw.
            // NOTE: In production we'd do a dry-run check; here we throw since
            // cycles at derive() time are bugs (formulas should be DAG).
            throw TopologyError(
                "Dependency cycle detected while adding derived node. "
                "Ensure formulas do not form circular dependencies.");
        }

        return NodeHandle<T>{id, this};
    }

    // ── Value reading ────────────────────────────────────────────────────────

    /// Read the committed value of a node.
    template <NodeValue T>
    [[nodiscard]] const T& get(const NodeHandle<T>& handle) const {
        validate_handle_in_this_graph(handle, "Graph::get");
        const auto& rec = storage_.at(handle.id());
        if (!rec.committed_value.template holds<T>()) {
            throw make_type_mismatch(typeid(T).name(), rec.committed_value.type_name());
        }
        return rec.committed_value.template get<T>();
    }

    /// Get the committed node name.
    [[nodiscard]] std::string node_name(detail::NodeId id) const {
        if (!storage_.contains(id)) throw HandleError(ErrorCode::NodeNotFound,
            std::format("Node id={} not found", id));
        return storage_.at(id).name;
    }

    // ── Transaction management ───────────────────────────────────────────────

    /// Open a new transaction. There can only be one active transaction
    /// at a time (enforced at commit). Transactions are NOT nested.
    [[nodiscard]] Transaction transaction(std::string description = "");

    /// Commit a pre-built transaction state. Called by Transaction::commit().
    /// This is the prepare-validate-publish-notify lifecycle.
    CommitResult commit_transaction(
        detail::TransactionState& tx,
        const std::string& description,
        detail::UndoRedoJournal* journal_override = nullptr,
        ChangeEvent::Origin origin = ChangeEvent::Origin::Normal);

    // ── Undo/redo ─────────────────────────────────────────────────────────────

    [[nodiscard]] bool can_undo() const noexcept { return journal_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return journal_.can_redo(); }

    CommitResult undo();
    CommitResult redo();

    // ── Observer registration ─────────────────────────────────────────────────

    [[nodiscard]] ObserverHandle subscribe(ObserverFn callback) {
        ObserverId id = ++next_observer_id_;
        observers_.emplace_back(id, std::move(callback));
        return ObserverHandle{id};
    }

    bool unsubscribe(ObserverHandle handle) {
        auto it = std::find_if(observers_.begin(), observers_.end(),
            [&](const auto& p){ return p.first == handle.id; });
        if (it == observers_.end()) return false;
        observers_.erase(it);
        return true;
    }

    // ── Diagnostics ───────────────────────────────────────────────────────────

    [[nodiscard]] std::size_t node_count() const noexcept { return storage_.size(); }

    [[nodiscard]] std::size_t input_count() const noexcept {
        std::size_t c = 0;
        storage_.for_each([&](const detail::NodeRecord& r){
            if (r.kind == detail::NodeKind::Input) ++c;
        });
        return c;
    }

    [[nodiscard]] std::size_t derived_count() const noexcept {
        return node_count() - input_count();
    }

    // Iterate all nodes in topological order (inputs first then derived).
    template <typename Fn>
    void for_each_node(Fn&& fn) const {
        storage_.for_each(std::forward<Fn>(fn));
    }

    // Access storage (semi-internal, used by Transaction and tests).
    [[nodiscard]] detail::GraphStorage& storage() noexcept { return storage_; }
    [[nodiscard]] const detail::GraphStorage& storage() const noexcept { return storage_; }

    // ── Internal helpers ──────────────────────────────────────────────────────

    // Validate a handle belongs to this graph.
    template <typename T>
    void validate_handle_in_this_graph(const NodeHandle<T>& handle,
                                        std::string_view context) const {
        if (!handle.valid()) {
            throw make_invalid_handle(context);
        }
        if (handle.graph() != this) {
            throw make_wrong_graph(context);
        }
        if (!storage_.contains(handle.id())) {
            throw HandleError(ErrorCode::NodeNotFound,
                std::format("{}: node id={} not found in this graph", context, handle.id()));
        }
        const auto& rec = storage_.at(handle.id());
        if (!rec.committed_value.template holds<T>()) {
            throw make_type_mismatch(typeid(T).name(), rec.committed_value.type_name());
        }
    }

    // Notify all observers (called only after full publication).
    // Exceptions in callbacks are caught and stored in errors.
    void notify_observers(const ChangeEvent& event,
                          std::vector<std::string>& errors) {
        for (auto& [id, cb] : observers_) {
            try {
                cb(event);
            } catch (const std::exception& ex) {
                errors.push_back(std::format("Observer id={}: {}", id, ex.what()));
            } catch (...) {
                errors.push_back(std::format("Observer id={}: unknown exception", id));
            }
        }
    }

    // Queue a deferred mutation (called from within an observer).
    void enqueue_mutation(detail::DeferredMutation mut) {
        callback_queue_.enqueue(std::move(mut));
    }

    [[nodiscard]] bool is_notifying() const noexcept {
        return notification_depth_ > 0;
    }

    detail::UndoRedoJournal& journal() noexcept { return journal_; }
    detail::CallbackQueue& callback_queue() noexcept { return callback_queue_; }

private:
    detail::GraphStorage storage_;
    detail::UndoRedoJournal journal_;
    detail::CallbackQueue callback_queue_;

    std::vector<std::pair<ObserverId, ObserverFn>> observers_;
    ObserverId next_observer_id_{0};

    int notification_depth_{0};   // guards re-entrancy in notification phase
};

} // namespace incremental

// ── Transaction implementation (declared here, defined in transaction.hpp) ────
#include <incremental/transaction.hpp>
