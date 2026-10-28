// tests/test_randomized.cpp
// Randomized correctness tests: generate small acyclic graphs and compare
// incremental recomputation with a naive full-recomputation reference.
//
// Strategy: build ONE graph, apply mutations incrementally, then verify by
// computing the expected final value by hand (iterating the node tree directly
// via committed values), not by building a second graph.
#include "test_main.hpp"
#include <incremental/incremental.hpp>
#include <random>
#include <vector>
#include <cmath>
#include <format>
using namespace incremental;

// ── Helpers ───────────────────────────────────────────────────────────────────

// A simple DAG where each node stores:
//   - input value (for inputs)
//   - the index range of parent nodes (for derived: sum / count of parents)
struct SimpleDAG {
    struct Node {
        bool is_input;
        double value{0.0};
        std::size_t parent_start{0};  // index into nodes[]
        std::size_t parent_end{0};
    };
    std::vector<Node> nodes;

    std::size_t add_input(double v) {
        nodes.push_back({true, v, 0, 0});
        return nodes.size() - 1;
    }

    std::size_t add_derived(std::size_t p_start, std::size_t p_end) {
        // Average of parents[p_start..p_end)
        nodes.push_back({false, 0.0, p_start, p_end});
        return nodes.size() - 1;
    }

    void recompute_all() {
        for (auto& n : nodes) {
            if (!n.is_input && n.parent_end > n.parent_start) {
                double sum = 0;
                for (std::size_t j = n.parent_start; j < n.parent_end; ++j)
                    sum += nodes[j].value;
                n.value = sum / static_cast<double>(n.parent_end - n.parent_start);
            }
        }
    }

    void set(std::size_t id, double v) { nodes[id].value = v; }
    double get(std::size_t id) const { return nodes.at(id).value; }
};

// ── Test ──────────────────────────────────────────────────────────────────────

TEST(randomized, incremental_equals_full) {
    const unsigned SEED = 42;
    std::mt19937 rng{SEED};
    std::uniform_real_distribution<double> val_dist(1.0, 100.0);
    std::uniform_int_distribution<int> n_inputs_dist(2, 6);
    std::uniform_int_distribution<int> n_deriveds_dist(1, 8);
    std::uniform_int_distribution<int> n_mutations_dist(1, 10);

    const int N_TRIALS = 50;

    for (int trial = 0; trial < N_TRIALS; ++trial) {
        int n_inputs  = n_inputs_dist(rng);
        int n_derived = n_deriveds_dist(rng);

        // ── Build both graphs with identical structure ─────────────────────
        Graph g;
        SimpleDAG ref;

        std::vector<NodeHandle<double>> handles;
        std::vector<std::size_t>        ref_ids;

        // Inputs
        for (int i = 0; i < n_inputs; ++i) {
            double v = val_dist(rng);
            handles.push_back(g.input<double>("I" + std::to_string(i), v));
            ref_ids.push_back(ref.add_input(v));
        }

        // Derived: node k depends on the sqrt(k)+1 predecessors
        // We always use the last min(n_deps, pool_size) nodes as parents.
        for (int d = 0; d < n_derived; ++d) {
            std::size_t pool = handles.size();
            // n_deps: 1..min(3, pool)
            std::size_t n_deps = static_cast<std::size_t>(
                std::uniform_int_distribution<int>(
                    1, std::min(3, static_cast<int>(pool)))(rng));
            std::size_t start_idx = pool - n_deps;

            // Build incremental derived
            std::vector<detail::NodeId> dep_ids;
            std::vector<NodeHandle<double>> dep_handles;
            for (std::size_t k = start_idx; k < pool; ++k) {
                dep_ids.push_back(handles[k].id());
                dep_handles.push_back(handles[k]);
            }
            auto dh = g.derive<double>("D" + std::to_string(d), dep_ids,
                [dep_handles](const auto& view) {
                    double sum = 0;
                    for (const auto& h : dep_handles) sum += view.get(h);
                    return sum / static_cast<double>(dep_handles.size());
                });
            handles.push_back(dh);

            // Build reference derived with EXACT same parent range
            std::size_t ref_start = ref_ids.size() - n_deps;
            ref_ids.push_back(ref.add_derived(ref_start, ref_ids.size()));
        }

        // Prime reference
        ref.recompute_all();

        // Collect incremental input handles and ref input ids
        std::vector<NodeHandle<double>> inph(handles.begin(),
                                              handles.begin() + n_inputs);
        std::vector<std::size_t> ref_inph(ref_ids.begin(),
                                           ref_ids.begin() + n_inputs);

        std::size_t out_inc = handles.size() - 1;   // last node (derived output)
        std::size_t out_ref = ref_ids.size() - 1;

        // ── Apply mutations ────────────────────────────────────────────────
        std::uniform_int_distribution<std::size_t> inp_sel(0,
            static_cast<std::size_t>(n_inputs) - 1);
        int n_mut = n_mutations_dist(rng);

        for (int m = 0; m < n_mut; ++m) {
            std::size_t idx = inp_sel(rng);
            double new_val  = val_dist(rng);

            // Incremental commit
            auto tx = g.transaction();
            tx.set(inph[idx], new_val);
            auto res = tx.commit();
            ASSERT_TRUE(res.committed);

            // Reference full recompute
            ref.set(ref_inph[idx], new_val);
            ref.recompute_all();
        }

        // ── Compare ────────────────────────────────────────────────────────
        double inc_result = g.get(handles[out_inc]);
        double ref_result = ref.get(out_ref);

        if (std::abs(inc_result - ref_result) > 1e-9) {
            throw TestFailure(std::format(
                "Trial {} (seed={}): incremental={:.15g} vs reference={:.15g} "
                "differ by {:.3e}",
                trial, SEED, inc_result, ref_result,
                std::abs(inc_result - ref_result)));
        }
    }
}
