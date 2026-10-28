# Architecture

## Overview

```
┌─────────────────────────────────────────────────────┐
│                     User Code                       │
│   Graph g; auto h = g.input<double>("X", 1.0);      │
│   auto tx = g.transaction(); tx.set(h, 2.0);        │
│   tx.commit();                                       │
└───────────────────────┬─────────────────────────────┘
                        │
           ┌────────────▼────────────┐
           │       Graph             │
           │  - GraphStorage         │
           │  - UndoRedoJournal      │
           │  - CallbackQueue        │
           │  - Observer list        │
           └────────────┬────────────┘
                        │
           ┌────────────▼────────────┐
           │   commit_transaction()  │ (defined in transaction.hpp)
           │                         │
           │  Phase 1: Identify      │ Which inputs changed?
           │  Phase 2: Subgraph      │ BFS from changed inputs → affected set
           │  Phase 3: Prepare       │ Evaluate all formulas (no state mutation)
           │  Phase 4: Publish       │ Swap committed values atomically
           │  Phase 5: Notify        │ Call observers (all values visible)
           │  Phase 6: Drain         │ Process queued mutations (FIFO)
           └─────────────────────────┘
```

## Key Types

| Type | File | Role |
|---|---|---|
| `Graph` | `graph.hpp` | Central graph; owns nodes, journal, callbacks |
| `Transaction` | `transaction.hpp` | RAII staging + commit engine |
| `NodeHandle<T>` | `node_handle.hpp` | Typed, validated handle to a graph node |
| `ReadContext` | `read_view.hpp` | Formula read-access with dependency checking |
| `GraphStorage` | `detail/graph_storage.hpp` | Adjacency list with topological ordering |
| `TypeErasedValue` | `detail/type_erasure.hpp` | `std::any`-backed heterogeneous value |
| `UndoRedoJournal` | `detail/journal.hpp` | Bounded undo/redo stack |
| `CallbackQueue` | `detail/callback_queue.hpp` | FIFO mutation queue with depth limit |

## Topological Order

`GraphStorage` maintains a **cached topological order** (Kahn's algorithm) over all nodes. The `derive()` call adds the new node and runs a topological sort. The sort result is used in Phase 2 to guarantee that `compute_affected_nodes` visits dependent nodes in dependency order — each node is processed exactly once, and never before its inputs.

## Affected-Subgraph Computation

`detail::compute_affected_nodes()` (in `topology.hpp`) does a BFS starting from the set of changed input nodes. Only reachable descendants are added to the affected set. The result is returned in topological order so Phase 3 (prepare) can evaluate each formula exactly once, with all its dependencies already computed.

## Exception Safety

The key invariant: **committed state is never mutated during Phase 3 (Prepare)**. This makes the strong guarantee automatic — if any formula throws during Prepare, abandon the staging area and return a failed `CommitResult`. No cleanup needed because the committed graph was never touched.

## Reentrancy Model

During Phase 5 (Notify), any observer that calls `graph.enqueue_mutation(fn)` adds `fn` to a `CallbackQueue`. The queue is drained in Phase 6 as separate full transactions. A recursive depth counter prevents infinite feedback loops (limit: 32 deep, raises `ReentrancyError`).
