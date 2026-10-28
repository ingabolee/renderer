# incremental: Dependency-Tracked Incremental Recomputation

## Overview

This project provides a **header-only C++23 library** for **dependency-tracked incremental recomputation**. Using a directed acyclic graph (DAG) of input and derived nodes, the application models spreadsheet-style formulas, transactions, observers, and undo/redo so that a change to one input updates only the formulas that actually depend on it.

The library supports **strongly typed** `NodeHandle<T>` values, **atomic exception-safe transactions**, **topological affected-subgraph recomputation**, **cycle detection**, **reentrancy-safe observer notifications**, and a **bounded undo/redo journal**. A companion `recalc` CLI demonstrates the model with a spreadsheet walkthrough and compares incremental evaluation against a naive full recompute.

Nodes compete against a full-graph baseline by attempting to produce the same results while evaluating far fewer formulas when only a subset of inputs change.

## Computation Logic: Incremental Recalculation

Incremental recomputation is a graph evaluation strategy in which derived values are recomputed only when their upstream inputs change. Players of the spreadsheet analogy are **inputs** (cells you set) and **derived nodes** (formulas). The house rule is: if a node is not reachable from a changed input, its committed value is left alone.

### Node Values

Each node has a kind, a name, a committed value, and (for derived nodes) a formula plus dependency edges. The `TypeErasedValue` class stores heterogeneous values inside `std::any` while `NodeHandle<T>` restores type safety at the API boundary.

- **Input nodes**: Hold a user-assigned value. Only inputs may be staged with `Transaction::set`.
- **Derived nodes**: Hold a formula `(const ReadContext&) -> T`. Their committed value is produced by evaluating that formula.
- **Value types**: Must satisfy `NodeValue` (`std::copy_constructible`) so the engine can clone values into the staging area before publish.
- **ReadContext**: Formulas may only call `view.get(handle)` for nodes listed in `deps` at `derive()` time. An undeclared read throws `UndeclaredDependency`.

During a commit’s prepare phase, `ReadContext::get` prefers **staged input values**, then **already-computed derived values in the same transaction**, then **committed storage**. That lets a formula see the new world without mutating the old one.

### Recalculation Flow

A commit follows a fixed sequence of identify, invalidate, prepare, publish, notify, and drain.

1. **Stage**: The caller opens `graph.transaction()`, calls `tx.set(handle, value)` on one or more inputs, then `tx.commit()`. Uncommitted transactions discard their staging area on destruction.
2. **Identify**: Every staged input is treated as changed. Before/after clones are recorded for the undo journal.
3. **Affected subgraph**: BFS over reverse edges (dependents) from those inputs. Unreachable derived nodes are skipped.
4. **Prepare**: Affected derived nodes are evaluated in cached topological order. Committed storage is not written.
5. **Publish**: If every formula succeeded, staged inputs and computed derived values are swapped into committed storage.
6. **Notify**: Observers receive a `ChangeEvent` with the new consistent state visible.
7. **Drain**: Mutations queued by observers run as separate FIFO transactions, with a depth limit against feedback loops.

> [!IMPORTANT]
> The implementation is a **push / dirty-set** engine, not a pull / lazy cache. There is no per-node memoization beyond the committed value, no equality filtering of “same as old” staged inputs, and no thread safety. `Graph` must be used from a single thread. Nodes are never removed, so handles do not dangle for the lifetime of the graph.

## Object-Oriented Architecture

The implementation separates graph ownership, transaction lifecycle, topology, and journaling. Public types live under `include/incremental/`; internals live under `include/incremental/detail/`.

- **`Graph`**: Owns `GraphStorage`, `UndoRedoJournal`, `CallbackQueue`, and the observer list. Creates inputs and derived nodes, opens transactions, commits, undoes, and redos.
- **`Transaction`**: RAII staging object. `set` writes only into `TransactionState`. `commit` delegates to `Graph::commit_transaction`.
- **`NodeHandle<T>`**: Lightweight typed token (`NodeId` + owning `Graph*`). Copyable; default-constructed handles are invalid.
- **`ReadContext`**: Formula read API with declared-dependency checking and staged/committed overlay.
- **`TypeErasedValue`**: `std::any` plus cached `std::type_index` / type name for `holds<T>()` without extra `any_cast` probes.
- **`GraphStorage`**: Hash map of `NodeRecord` plus insertion/topo order vector.
- **`UndoRedoJournal`**: Bounded history of input before/after snapshots.
- **`CallbackQueue`**: FIFO queue of deferred observer mutations with a drain-depth cap (default 32).
- **`ObserverFn` / `ChangeEvent`**: Post-commit notification. Observer exceptions are collected; they cannot roll back a published commit.

### Memory Management

The library is **header-only**. There is no `new`/`delete` of cards or nodes at the public API. Ownership is value-based with type erasure.

- **Node records**: Stored in `std::unordered_map<NodeId, NodeRecord>`. IDs are monotonic `std::size_t` counters (`next_id_`).
- **Formulas**: Type-erased as `std::function<TypeErasedValue(const ReadContext&)>`.
- **Staging**: Copies of values live in `TransactionState` until publish, then `TypeErasedValue::swap` exchanges them with committed slots (no extra heap protocol beyond `std::any`).
- **Journal**: Each history entry clones **input** values only (not every derived node), then undo/redo re-runs the normal commit path so derived nodes recompute.
- **Handles**: Do not own nodes. The graph is non-copyable and non-movable because handles store a raw `const Graph*`.

This design lets heterogeneous `T` values move through the DAG without a shared base class for every cell type.

## Data Structures

| Structure | Location | Role |
| :--- | :--- | :--- |
| **Adjacency lists** | `NodeRecord::dependencies` / `dependents` | Forward edges (formula reads) and reverse edges (who to invalidate). |
| **Hash map of nodes** | `GraphStorage::nodes_` | O(1) lookup by `NodeId`. |
| **Order vector** | `GraphStorage::order_` | After each structural change, rebuilt as a topological order. |
| **Cached `topo_order`** | `NodeRecord::topo_order` | Integer rank used to sort the affected set without another Kahn pass at commit time. |
| **Staging maps** | `TransactionState` | `unordered_map` of staged inputs and computed derived values. |
| **Work list + set** | `compute_affected_nodes` | Vector BFS queue plus `unordered_set` for O(1) membership. |
| **DFS color map** | `find_cycle` | White/Gray/Black marking plus parent map for cycle path reconstruction. |
| **Journal vector** | `UndoRedoJournal::history_` | Linear history with `redo_index_`; new edits truncate the redo tail. |
| **FIFO queue** | `CallbackQueue::pending_` | `std::queue<std::function<void()>>`. |
| **Observer list** | `Graph::observers_` | `vector<pair<ObserverId, ObserverFn>>`. |

`NodeRecord` also carries a `dirty` flag that is reserved for transaction preparation; the current commit path uses the affected-set vector rather than walking a dirty bit as the source of truth.

## Algorithms

### Kahn topological sort

`GraphStorage::recompute_topo_order()` runs **Kahn’s algorithm** after every `input()` / `derive()`:

1. Compute in-degree from `dependencies.size()`.
2. Seed a work list with in-degree 0 (inputs and independent nodes).
3. Repeatedly pop a node, assign `topo_order`, decrement dependents’ in-degrees.
4. If the number of sorted nodes is not `|V|`, a cycle exists and the function returns `false`.

The work list uses `vector` + `pop_back` (LIFO). That is still a correct topological sort; it is not a BFS layering of the DAG.

`Graph::derive` adds the node (and reverse edges) then calls this sort. A cycle throws `TopologyError`. The storage layer does not currently roll back the added node if the sort fails.

### Three-color DFS cycle detection

`detail::find_cycle` walks dependents with **White / Gray / Black** colors. A Gray-to-Gray edge is a back edge. The parent map reconstructs a named cycle path for diagnostics. `would_create_cycle` DFS-searches whether a proposed dependency set can reach the new node.

Public wrappers live in `cycle_detection.hpp` / `dependency_traversal.hpp`.

### Affected-subgraph BFS

`detail::compute_affected_nodes(storage, changed_inputs)`:

1. Seed the set with **direct dependents** of each changed input (inputs themselves are not re-evaluated as formulas).
2. Expand transitively over `dependents` (index-based BFS so the queue can grow in place).
3. Copy the set to a vector and **sort by `topo_order`**.

Prepare then evaluates each affected **Derived** node once. Downstream formulas see upstream **computed** values from the same staging map because the vector is topo-sorted.

Complexity for a commit with changed set *C* and affected derived set *A*:

- Identify: O(|C|)
- Invalidate: O(|A| + edges out of *A* ∪ *C*)
- Sort affected: O(|A| log |A|)
- Prepare: O(|A|) formula calls
- Publish: O(|C| + |A|) swaps

Unrelated branches cost O(1) per skipped node in the counters (`nodes_skipped = derived_count - |affected|`), not a formula evaluation.

### Prepare–publish (strong exception safety)

Formulas run **before** any committed `swap`. If a formula throws, `commit_transaction` returns `CommitResult{committed: false}` and the graph is unchanged. Observer errors after a successful publish are recorded and cannot undo the commit.

Undo and redo reuse this engine: they stage `before_value` or `after_value` clones and call `commit_transaction` with `Origin::Undo` / `Origin::Redo` so the journal is not pushed again.

### Observer drain and reentrancy

After notify, the graph pops `CallbackQueue` until empty. Each queued mutation is a full transaction. `increment_drain_depth` throws if depth exceeds the configured limit (constructor default **32**), then the queue is cleared.

Observers may **read** committed state freely. They should **queue** further writes rather than assuming nested live mutation of the in-flight commit.

### Generic `for_each` dispatch

`incremental::for_each` is a compile-time protocol dispatcher (`customization_points.hpp`):

1. Member `obj.for_each(cb)`
2. ADL `incremental_for_each(obj, cb)`
3. `std::ranges::range`
4. `size()` + `operator[]`

The first matching protocol wins (`if constexpr`). Unsupported types hit a `static_assert` with those four options listed.

## Optimizations

- **Incremental vs naive**: Only the reverse-reachable derived subgraph is evaluated. Benchmarks (`recalc benchmark`, `incremental_bench`) compare this to evaluating every derived node.
- **Cached topology**: Commits sort the (usually small) affected set by precomputed ranks instead of re-running Kahn on the whole graph.
- **Reverse adjacency**: Invalidation is O(affected), not O(all edges).
- **Header-only / zero runtime deps**: No extra shared library; the hot path is templates + `std::function` formulas.
- **Swap publish**: `TypeErasedValue::swap` is `noexcept` on the wrapper fields; committed slots exchange with staging rather than assigning through an extra copy when possible.
- **Journal compactness**: History stores input snapshots only; derived state is reconstructed by commit.
- **Bounded journal**: Default 64 entries; oldest entries drop when the limit is exceeded.
- **Handle validation**: Graph pointer + type check prevent cross-graph and type-mismatched reads before they corrupt storage.
- **Cost counters**: `inputs_changed`, `nodes_invalidated`, `nodes_recomputed`, `nodes_skipped` expose whether a commit actually skipped work.

Non-optimizations (current limits): staged inputs are always treated as changed (no `operator==` skip); `undeclared` dependency checks are a linear scan of `deps`; `Graph` is not concurrent; `std::function` and `std::any` allocate for many value types.

## Technical Stack

- **C++23**: Concepts (`NodeValue`), `std::expected` (`Result<T>`), `std::format`, ranges, `if constexpr` protocol dispatch.
- **STL**: `unordered_map`, `unordered_set`, `vector`, `queue`, `function`, `any`, `type_index`.
- **CMake 3.22+**: Interface library `incremental::incremental`, executables `recalc`, `spreadsheet_demo`, `graph_examples`, `incremental_tests`, `incremental_bench`.
- **CLI / terminal**: Optional ANSI color and Unicode box drawing (`terminal.hpp`), with `--ascii` for consoles that are not UTF-8.

### Header dependencies (library)

Typical consumer include is a single umbrella:

- `<incremental/incremental.hpp>`: Graph, transactions, observers, cycle helpers, `for_each`.
- `<incremental/terminal.hpp>`: CLI box drawing (opt-in, not in the umbrella).

Internally the core uses the usual C++ standard headers (`<vector>`, `<unordered_map>`, `<functional>`, `<any>`, `<format>`, `<concepts>`, `<expected>`, and friends). There are **no** third-party packages.

## Features

| Feature | Description |
| :--- | :--- |
| **Header-only library** | Include and compile; no extra link library for the core. |
| **Typed handles** | `NodeHandle<T>` with graph and type checks. |
| **Atomic transactions** | Multi-input `set` then a single `commit`. |
| **Affected-subgraph eval** | BFS invalidation + topo-ordered prepare. |
| **Strong exception safety** | Formula throw ⇒ graph unchanged. |
| **Cycle detection** | Kahn failure on `derive`; DFS helpers for path reporting. |
| **Observers** | Post-publish `ChangeEvent`; queued mutations. |
| **Undo / redo** | Bounded journal of input snapshots, re-committed as transactions. |
| **Generic `for_each`** | Four-protocol compile-time dispatch. |
| **`recalc` CLI** | `demo` walkthrough and `benchmark` vs naive recompute. |
| **Tests** | CTest cases covering graph, incrementality, transactions, cycles, undo, CLI, randomized equivalence. |

## Usage

### Requirements

| Tool | Minimum |
| :--- | :--- |
| C++ standard | C++23 |
| GCC | 13+ |
| Clang | 17+ |
| MSVC | 19.38+ (VS 2022 17.8+) |
| CMake | 3.22+ |

### Compilation

From the `incremental` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Presets (Ninja):

```bash
cmake --preset default
cmake --build --preset default
```

### Running the CLI

On Windows, executables land next to the build tree:

```powershell
.\build\recalc.exe demo
.\build\recalc.exe demo --ascii
.\build\recalc.exe benchmark --nodes=1000 --iterations=50 --csv=results.csv
.\build\spreadsheet_demo.exe
.\build\graph_examples.exe
```

```bash
build/recalc demo
build/recalc benchmark --nodes=1000 --iterations=50 --csv=results.csv
cd build && ctest -j4 --output-on-failure
```

If Unicode section lines look like `ΓöÇ`, the console is not UTF-8. Use `--ascii` or `chcp 65001`.

```
recalc --help
recalc --version
```

| Option | Meaning |
| :--- | :--- |
| `--ascii` | ASCII `+`/`-`/`\|` instead of Unicode boxes |
| `--color=auto\|always\|never` | ANSI color |
| `--nodes=N` | Benchmark graph size (default 1000) |
| `--iterations=N` | Benchmark iterations (default 50) |
| `--seed=N` | RNG seed (default 42) |
| `--csv=<file>` | Write benchmark CSV |

### Library usage

```cpp
#include <incremental/incremental.hpp>
using namespace incremental;

Graph g;
auto revenue = g.input<double>("Revenue", 120.0);
auto costs   = g.input<double>("Costs",   80.0);

auto profit = g.derive<double>("Profit", {revenue.id(), costs.id()},
    [revenue, costs](const auto& view) {
        return view.get(revenue) - view.get(costs);
    });

auto margin = g.derive<double>("Margin", {profit.id(), revenue.id()},
    [profit, revenue](const auto& view) {
        return view.get(profit) / view.get(revenue) * 100.0;
    });

auto tx = g.transaction("revenue+costs change");
tx.set(revenue, 150.0);
tx.set(costs, 90.0);
auto result = tx.commit();
// result.committed == true
// Only affected nodes recomputed

g.undo();

g.subscribe([](const ChangeEvent& ev) {
    // Called after all new values are published
});
```

CMake consumers:

```cmake
target_link_libraries(your_app PRIVATE incremental::incremental)
```

### Running

Ensure a C++23 toolchain and CMake 3.22+ are installed. After a successful build, run the demo (ASCII boxes avoid Windows code-page mojibake):

```powershell
cd incremental
.\build\recalc.exe demo --ascii --color=never
```

```bash
cd incremental
./build/recalc demo --ascii --color=never
```

CMake presets:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Debug and sanitizer presets exist as `debug` and `sanitize` in `CMakePresets.json`. Sanitizers apply to GCC/Clang (`INCREMENTAL_ENABLE_SANITIZERS`).

### Sample Output

`recalc demo --ascii --color=never` prints a boxed banner, then tables after each step. The graph is:

```
Revenue, Costs  (inputs)
    └─ Profit = Revenue - Costs
         ├─ Margin = Profit / Revenue * 100   (throws if Revenue == 0)
         ├─ AxisMaximum = ceil(Profit / 10) * 10
         └─ FormattedProfit = "$x.xx"
              Margin ─► HealthStatus  (Excellent / Good / Fair / Poor)
```

Typical last section (values depend on the observer-queued cost change and undo/redo):

```
-- FINAL STATE ------------------------------------------------
  Revenue            140.00
  Costs               85.00
  Profit              55.00
  Margin              39.29%
  Axis Maximum        60.00
  Formatted Profit    $55.00
  Health Status       Excellent
  Can undo:           yes
  Can redo:           no
[OK] Demo complete.
```

If the console is CP437, Unicode `─` appears as `ΓöÇ`. That is encoding, not a logic bug.

## Spreadsheet Demo Implementation

`cli.cpp::run_demo` is a scripted walkthrough of the public API (not an interactive game loop).

1. **Build**: Two `double` inputs and five derived nodes, including `std::string` cells (`FormattedProfit`, `HealthStatus`) to show heterogeneous `TypeErasedValue`.
2. **Single input**: `Revenue` 120 → 130. Dependents of `Costs` alone are skipped if they are not reachable from `Revenue` (here most nodes are reachable through `Profit`/`Margin`).
3. **Atomic multi-set**: `Revenue` and `Costs` in one transaction; one prepare/publish round.
4. **Rollback**: `Revenue = 0` makes `Margin` throw `std::domain_error`. Prepare aborts; committed revenue stays 130.
5. **Observer queue**: A subscriber `enqueue_mutation`s a second transaction that sets `Costs = 85` after the primary commit (`Revenue = 140`).
6. **Undo / redo**: Journal replays input snapshots through the same commit engine (`ChangeEvent::Origin::Undo` / `Redo`).

Standalone equivalent without CLI chrome: `examples/spreadsheet_demo.cpp`. Extra APIs (`detect_cycle`, `for_each`, `visit_dependents`): `examples/graph_examples.cpp`.

## Benchmark and Naive Baseline

The incremental engine is compared against **`NaiveGraph`** in `src/cli.cpp` (not the empty stub `benchmarks/naive_recalculation.cpp`). Naive storage is a `vector` of nodes. After each mutation it calls `recompute_all()`, which evaluates **every** derived formula in insertion order.

### Graph shape generator

`build_bench_graph(n_inputs, fan, seed)` builds a **wide fan-in tree**:

- `n_inputs` random `double` inputs (uniform 1–100, `std::mt19937`).
- Groups of size `fan` become derived **averages**.
- One **Root** sums all group nodes.

Changing one input therefore dirties one group plus Root in the incremental engine, while naive still walks the entire derived set.

### Workload

`run_one_benchmark` draws `iterations` random `(input_index, value)` pairs, then:

1. Applies them to naive (`set` + `recompute_all`), timed with `std::chrono::high_resolution_clock`.
2. Applies the same sequence as incremental transactions; sums `nodes_recomputed`.
3. Checks `|naive_root - incremental_root| < 1e-9`.

Six shapes: chain (`fan=1`, capped inputs), wide independent branches (`fan = n_inputs`), balanced (`fan=8`), small affected (`fan=64`), high fanout (`fan=4`), dense chain (capped at 100). Results print as a table; `--csv=` writes `scenario,node_count,edge_count,iterations,full_evals,incremental_evals,full_ms,incremental_ms,speedup,results_match`.

`full_evals` is counted as `iterations * naive.nodes.size()` (every node slot per pass). Incremental evals are only derived nodes actually prepared.

## Traversal and Diagnostic APIs

| Function | Algorithm | Notes |
| :--- | :--- | :--- |
| `detect_cycle(storage)` | 3-color DFS on dependents | Returns node-name path or `nullopt`. |
| `format_cycle(path)` | Join with `" -> "` | e.g. `Revenue -> Profit -> Revenue`. |
| `visit_dependents(g, root, cb)` | DFS stack on reverse edges | BFS-documented but implemented with `pop_back` (LIFO). Skips `root`; visits proper descendants. |
| `visit_dependencies(g, leaf, cb)` | LIFO walk of `dependencies` | Ancestors of `leaf`. |
| `compute_affected_nodes` | BFS then `topo_order` sort | Used internally at commit. |

`undo_redo.hpp` declares `graph_undo` / `graph_redo`; the implemented surface on `Graph` is `undo()`, `redo()`, `can_undo()`, `can_redo()`.

## Error Model

Failures use exceptions derived from `incremental::Error` (`std::runtime_error` plus `ErrorCode`), or a non-throwing `CommitResult` when a formula fails during prepare.

| Type | Typical codes / use |
| :--- | :--- |
| `HandleError` | Invalid handle, wrong graph, type mismatch, missing node, undeclared dependency |
| `TopologyError` | Cycle on `derive`; optional `cycle_path` |
| `TransactionError` | `set` on a derived node (`NotAnInputNode`) |
| `FormulaError` | Declared for wrapping formula exceptions; prepare currently returns `CommitResult` instead |
| `ReentrancyError` | Documented for feedback loops; drain currently throws `std::runtime_error` if depth exceeds the limit |

`Result<T>` / `VoidResult` are `std::expected` aliases for optional non-throwing APIs; the graph mutators above throw or return `CommitResult`.

`Graph` constructor: `Graph(journal_limit = 64, reentrancy_limit = 32)`.

## Testing

`incremental_tests` is a custom runner (`--test-case <name>`), registered as individual CTest targets (30s timeout each). Coverage includes:

- Graph construction, handle rejection, cross-graph handles
- Single/multi input change, shared dependency evaluated once, unrelated nodes untouched, long chains
- Incremental results vs full recompute (`incremental.results_match_full_recompute`, `randomized.incremental_equals_full`)
- Transaction abandon, formula throw with no partial state, no notify on failed commit
- Observer consistency, queued mutations, feedback-loop limit
- Cycles (self, two-node, multi-node, diamond OK, rejected add)
- Undo/redo including branch-after-undo
- `for_each` protocol priority
- CLI help/version/demo/ascii

```powershell
cd incremental\build
ctest -j4 --output-on-failure
.\incremental_tests.exe --test-case incremental.single_input_change
```

## Known Limitations

- **Not thread-safe.** All `Graph` access must be on one thread.
- **No node deletion.** Storage grows monotonically; handles stay valid until the graph is destroyed.
- **No nested transactions.** One staging area is committed at a time (queued observer work is sequential, not nested).
- **Staged inputs always invalidate.** There is no `operator==` short-circuit if the new value equals the old.
- **Cycle add is not rolled back in storage.** `derive` throws `TopologyError` if Kahn fails after the node is inserted; the public graph should not be used further after that failure.
- **Drain limit type.** Exceeding reentrancy depth throws `std::runtime_error`, not `ReentrancyError`.
- **No pull/lazy mode, equality memoization, or parallel prepare.**
- **CLI Unicode** depends on a UTF-8 console; use `--ascii` otherwise.

## Project Structure

```
include/incremental/
    incremental.hpp          # Umbrella include
    graph.hpp                # Central Graph class
    transaction.hpp          # RAII transaction + commit engine
    node_handle.hpp          # Typed NodeHandle<T>
    read_view.hpp            # Formula ReadContext
    customization_points.hpp # Generic for_each dispatch
    terminal.hpp             # Terminal rendering utilities
    detail/                  # Storage, topology, journal, type erasure

src/
    main.cpp                 # recalc entry point
    cli.cpp / cli.hpp        # demo + benchmark commands
    terminal.cpp             # Terminal stub

tests/                       # CTest cases
examples/                    # spreadsheet_demo, graph_examples
benchmarks/                  # incremental vs naive
docs/ARCHITECTURE.md
```

## License

MIT — see [LICENSE](LICENSE).
