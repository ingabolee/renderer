// include/incremental/detail/journal.hpp
// Undo/redo journal implementation.
#pragma once

#include <incremental/detail/graph_storage.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <optional>

namespace incremental::detail {

// ── JournalEntry ──────────────────────────────────────────────────────────────
// Records the input snapshot before and after a commit so that the change
// can be reversed (undo) or reapplied (redo) through normal transaction logic.

struct JournalEntry {
    std::string description;   ///< Human-readable label

    // For each changed input: {node_id, before_value, after_value}
    struct InputChange {
        NodeId          node_id;
        TypeErasedValue before_value;
        TypeErasedValue after_value;
    };

    std::vector<InputChange> changes;

    [[nodiscard]] bool empty() const noexcept { return changes.empty(); }
};

// ── UndoRedoJournal ──────────────────────────────────────────────────────────
// Maintains a bounded history of committed transactions.
// Memory: O(history_limit * avg_inputs_per_transaction * sizeof(value)).
// Time for undo/redo: same as a normal transaction commit.

class UndoRedoJournal {
public:
    explicit UndoRedoJournal(std::size_t limit = 64) : limit_(limit) {}

    // Record a new entry (called after a successful commit).
    void push(JournalEntry entry) {
        if (entry.empty()) return; // Don't journal no-ops

        // A new edit branches: truncate any redo history.
        if (redo_index_ < history_.size()) {
            history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(redo_index_),
                           history_.end());
        }

        history_.push_back(std::move(entry));
        if (history_.size() > limit_) {
            history_.erase(history_.begin());
        }
        redo_index_ = history_.size();
    }

    [[nodiscard]] bool can_undo() const noexcept {
        return redo_index_ > 0;
    }

    [[nodiscard]] bool can_redo() const noexcept {
        return redo_index_ < history_.size();
    }

    // Returns the entry to reverse (move backward).
    [[nodiscard]] const JournalEntry& undo_entry() const {
        return history_.at(redo_index_ - 1);
    }

    // Returns the entry to reapply (move forward).
    [[nodiscard]] const JournalEntry& redo_entry() const {
        return history_.at(redo_index_);
    }

    void commit_undo() {
        if (redo_index_ > 0) --redo_index_;
    }

    void commit_redo() {
        if (redo_index_ < history_.size()) ++redo_index_;
    }

    void clear() {
        history_.clear();
        redo_index_ = 0;
    }

    [[nodiscard]] std::size_t history_depth() const noexcept { return redo_index_; }
    [[nodiscard]] std::size_t redo_depth() const noexcept { return history_.size() - redo_index_; }
    [[nodiscard]] std::size_t limit() const noexcept { return limit_; }

private:
    std::vector<JournalEntry> history_;
    std::size_t redo_index_{0};
    std::size_t limit_;
};

} // namespace incremental::detail
