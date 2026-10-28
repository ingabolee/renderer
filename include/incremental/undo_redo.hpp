// include/incremental/undo_redo.hpp
// Public undo/redo API (thin wrappers that delegate to Graph).
// The actual implementation is in detail/journal.hpp and graph.hpp/transaction.hpp.
#pragma once

#include <incremental/change_event.hpp>
#include <string>

namespace incremental {

// Graph is forward-declared here; include graph.hpp for full usage.
class Graph;

/// Attempt to undo the most recent successfully committed transaction.
/// Returns the CommitResult of the undo operation (which is itself a transaction).
/// If nothing to undo, result.committed == false.
CommitResult graph_undo(Graph& g);

/// Attempt to redo the next transaction in the redo stack.
/// Returns the CommitResult of the redo operation.
/// If nothing to redo, result.committed == false.
CommitResult graph_redo(Graph& g);

} // namespace incremental
