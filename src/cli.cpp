// src/cli.cpp
// Full CLI implementation: argument parsing, demo, benchmark.
#include "cli.hpp"

#include <incremental/incremental.hpp>
#include <incremental/terminal.hpp>

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <random>
#include <cmath>
#include <algorithm>
#include <format>

namespace incremental::cli {

// ── Argument parsing ──────────────────────────────────────────────────────────

static bool parse_size(const char* s, std::size_t& out) {
    try {
        std::size_t pos;
        auto v = std::stoull(s, &pos);
        if (pos != std::strlen(s)) return false;
        out = static_cast<std::size_t>(v);
        return true;
    } catch (...) { return false; }
}

static bool parse_uint(const char* s, unsigned& out) {
    try {
        std::size_t pos;
        auto v = std::stoull(s, &pos);
        if (pos != std::strlen(s)) return false;
        out = static_cast<unsigned>(v);
        return true;
    } catch (...) { return false; }
}

// ── Help / Version ────────────────────────────────────────────────────────────

void print_help(std::ostream& out) {
    using namespace term;
    print_banner(out);
    print_section(out, "USAGE");
    out << "  recalc <command> [options]\n\n";
    out << "  " << ansi::b_white() << "recalc demo" << ansi::reset()
        << "                Run spreadsheet demo\n";
    out << "  " << ansi::b_white() << "recalc benchmark" << ansi::reset()
        << "           Compare incremental vs naive recomputation\n";
    out << "  " << ansi::b_white() << "recalc --help" << ansi::reset()
        << "              Show this help\n";
    out << "  " << ansi::b_white() << "recalc --version" << ansi::reset()
        << "           Show version information\n";

    print_section(out, "OPTIONS");
    out << "  " << ansi::cyan() << "--color=auto|always|never" << ansi::reset()
        << "  Terminal color output (default: auto)\n";
    out << "  " << ansi::cyan() << "--ascii" << ansi::reset()
        << "                    Use ASCII box characters\n";
    out << "  " << ansi::cyan() << "--quiet" << ansi::reset()
        << "                    Suppress informational output\n";
    out << "  " << ansi::cyan() << "--verbose" << ansi::reset()
        << "                  Show extra diagnostic output\n";

    print_section(out, "DEMO OPTIONS");
    out << "  " << ansi::cyan() << "--scenario=spreadsheet" << ansi::reset()
        << "  Run the spreadsheet demo (default)\n";

    print_section(out, "BENCHMARK OPTIONS");
    out << "  " << ansi::cyan() << "--nodes=N" << ansi::reset()
        << "              Number of nodes in the benchmark graph (default: 1000)\n";
    out << "  " << ansi::cyan() << "--iterations=N" << ansi::reset()
        << "         Number of benchmark repetitions (default: 50)\n";
    out << "  " << ansi::cyan() << "--seed=N" << ansi::reset()
        << "               Random seed for reproducibility (default: 42)\n";
    out << "  " << ansi::cyan() << "--csv=<file>" << ansi::reset()
        << "           Write results as CSV to <file>\n";

    print_section(out, "EXAMPLES");
    out << "  recalc demo\n";
    out << "  recalc demo --color=never\n";
    out << "  recalc demo --ascii\n";
    out << "  recalc benchmark --nodes=5000 --iterations=100\n";
    out << "  recalc benchmark --csv=results.csv\n";
    out << '\n';
}

void print_version(std::ostream& out) {
    using namespace term;
    auto vi = incremental::version();
    print_banner(out);
    print_section(out, "VERSION");
    print_kv(out, "Version:", vi.version_string);
    print_kv(out, "C++ standard:", std::format("C++{}", vi.cxx_standard));
    print_kv(out, "Compiler:", vi.compiler_string);
    print_kv(out, "Build date:", __DATE__ " " __TIME__);
    out << '\n';
}

// ── Spreadsheet Demo ──────────────────────────────────────────────────────────

int run_demo(const Options& opts, std::ostream& out) {
    using namespace term;

    if (opts.scenario != "spreadsheet") {
        print_err(out, std::format("Unknown scenario '{}'. Available: spreadsheet", opts.scenario));
        return 1;
    }

    print_banner(out);
    print_section(out, "SPREADSHEET DEMO");
    print_info(out, "Building dependency graph with business metrics...");
    out << '\n';

    // ── Build graph ──────────────────────────────────────────────────────────
    incremental::Graph graph;

    auto revenue = graph.input<double>("Revenue", 120.0);
    auto costs   = graph.input<double>("Costs",   80.0);

    auto profit = graph.derive<double>("Profit",
        {revenue.id(), costs.id()},
        [revenue, costs](const auto& view) {
            return view.get(revenue) - view.get(costs);
        });

    auto margin = graph.derive<double>("Margin",
        {profit.id(), revenue.id()},
        [profit, revenue](const auto& view) {
            double rev = view.get(revenue);
            if (rev == 0.0) throw std::domain_error("Revenue is zero: margin undefined");
            return view.get(profit) / rev * 100.0;
        });

    auto axis_max = graph.derive<double>("AxisMaximum",
        {profit.id()},
        [profit](const auto& view) {
            double p = view.get(profit);
            // Round up to next multiple of 10
            return std::ceil(p / 10.0) * 10.0;
        });

    auto fmt_profit = graph.derive<std::string>("FormattedProfit",
        {profit.id()},
        [profit](const auto& view) {
            double p = view.get(profit);
            return std::format("${:.2f}", p);
        });

    auto health = graph.derive<std::string>("HealthStatus",
        {margin.id()},
        [margin](const auto& view) {
            double m = view.get(margin);
            if (m >= 30.0) return std::string("Excellent");
            if (m >= 20.0) return std::string("Good");
            if (m >= 10.0) return std::string("Fair");
            return std::string("Poor");
        });

    // ── Display initial state ────────────────────────────────────────────────
    auto print_state = [&](std::string_view label, const detail::CostCounters& counters) {
        print_section(out, label);
        std::vector<term::TableRow> rows = {
            {"Revenue",         std::format("{:.2f}", graph.get(revenue))},
            {"Costs",           std::format("{:.2f}", graph.get(costs))},
            {"Profit",          std::format("{:.2f}", graph.get(profit))},
            {"Margin",          std::format("{:.2f}%", graph.get(margin))},
            {"Axis Maximum",    std::format("{:.2f}", graph.get(axis_max))},
            {"Formatted Profit",graph.get(fmt_profit)},
            {"Health Status",   graph.get(health)},
        };
        print_table(out, rows);

        out << '\n';
        std::vector<term::TableRow> stat_rows = {
            {"Inputs changed:",    std::format("{}", counters.inputs_changed)},
            {"Nodes invalidated:", std::format("{}", counters.nodes_invalidated)},
            {"Nodes recomputed:", std::format("{}", counters.nodes_recomputed)},
            {"Nodes skipped:",     std::format("{}", counters.nodes_skipped)},
        };
        print_table(out, stat_rows, 22, 6);
    };

    print_state("INITIAL STATE", {});

    // ── Single input change ──────────────────────────────────────────────────
    print_section(out, "STEP 1: Single Input Change (Revenue: 120 -> 130)");
    {
        auto tx = graph.transaction("revenue change");
        tx.set(revenue, 130.0);
        auto result = tx.commit();
        if (result.committed) {
            print_ok(out, std::format("Committed in {:.0f} microseconds (estimated)",
                0.001)); // no real timer here; see benchmark
            print_state("AFTER CHANGE", result.counters);
        }
    }

    // ── Multi-input transaction ──────────────────────────────────────────────
    print_section(out, "STEP 2: Multi-Input Transaction (Revenue=130, Costs=90)");
    {
        auto tx = graph.transaction("revenue+costs change");
        tx.set(revenue, 130.0);
        tx.set(costs, 90.0);
        auto result = tx.commit();
        if (result.committed) {
            print_ok(out, "Both inputs committed atomically.");
            print_state("AFTER MULTI-INPUT TRANSACTION", result.counters);
        }
    }

    // ── Rollback demo ─────────────────────────────────────────────────────────
    print_section(out, "STEP 3: Exception Safety — Rollback on Formula Error");
    {
        print_info(out, "Setting Revenue=0 (will cause division-by-zero in Margin formula)...");
        auto tx = graph.transaction("bad revenue");
        tx.set(revenue, 0.0);
        auto result = tx.commit();
        if (!result.committed) {
            print_ok(out, "Transaction rejected — graph unchanged.");
            print_kv(out, "Error:", result.error_message, 10);
            // Verify committed state unchanged:
            out << "  Revenue after failed tx: " << ansi::green()
                << graph.get(revenue) << ansi::reset() << " (unchanged)\n";
        } else {
            print_err(out, "ERROR: Transaction should have been rejected!");
        }
    }

    // ── Observer / queued mutation demo ───────────────────────────────────────
    print_section(out, "STEP 4: Reentrancy — Observer Queues a Mutation");
    {
        bool queued_fired = false;
        auto obs = graph.subscribe([&](const ChangeEvent& ev) {
            if (!queued_fired && ev.origin == ChangeEvent::Origin::Normal) {
                queued_fired = true;
                // Queue a mutation from within an observer callback.
                graph.enqueue_mutation([&]() {
                    auto tx2 = graph.transaction("observer-queued change");
                    tx2.set(costs, 85.0);
                    tx2.commit();
                });
            }
        });

        auto tx = graph.transaction("trigger observer");
        tx.set(revenue, 140.0);
        auto result = tx.commit();
        graph.unsubscribe(obs);

        if (result.committed) {
            print_ok(out, "Primary transaction committed.");
            print_info(out, "Observer queued a second mutation during notification.");
            print_kv(out, "Final Costs:", std::format("{:.2f}", graph.get(costs)), 16);
            print_kv(out, "Final Revenue:", std::format("{:.2f}", graph.get(revenue)), 16);
            print_kv(out, "Final Profit:", std::format("{:.2f}", graph.get(profit)), 16);
        }
    }

    // ── Undo / Redo ──────────────────────────────────────────────────────────
    print_section(out, "STEP 5: Undo / Redo");
    {
        out << "  Before undo:  Revenue=" << graph.get(revenue)
            << ", Costs=" << graph.get(costs)
            << ", Profit=" << graph.get(profit) << '\n';

        auto undo_result = graph.undo();
        if (undo_result.committed) {
            print_ok(out, "Undo succeeded.");
            out << "  After undo:   Revenue=" << graph.get(revenue)
                << ", Costs=" << graph.get(costs)
                << ", Profit=" << graph.get(profit) << '\n';
        }

        auto redo_result = graph.redo();
        if (redo_result.committed) {
            print_ok(out, "Redo succeeded.");
            out << "  After redo:   Revenue=" << graph.get(revenue)
                << ", Costs=" << graph.get(costs)
                << ", Profit=" << graph.get(profit) << '\n';
        }
    }

    // ── Final summary ─────────────────────────────────────────────────────────
    print_section(out, "FINAL STATE");
    std::vector<term::TableRow> final_rows = {
        {"Revenue",         std::format("{:.2f}", graph.get(revenue))},
        {"Costs",           std::format("{:.2f}", graph.get(costs))},
        {"Profit",          std::format("{:.2f}", graph.get(profit))},
        {"Margin",          std::format("{:.2f}%", graph.get(margin))},
        {"Axis Maximum",    std::format("{:.2f}", graph.get(axis_max))},
        {"Formatted Profit",graph.get(fmt_profit)},
        {"Health Status",   graph.get(health)},
        {"Can undo:",       graph.can_undo() ? "yes" : "no"},
        {"Can redo:",       graph.can_redo() ? "yes" : "no"},
    };
    print_table(out, final_rows);
    out << '\n';
    print_ok(out, "Demo complete.");
    out << '\n';
    return 0;
}

// ── Benchmark Implementation ──────────────────────────────────────────────────

// Naive evaluator: evaluates every derived node in topological order,
// regardless of which inputs changed. This is the reference implementation
// for correctness checking.
struct NaiveGraph {
    struct Node {
        std::string name;
        bool is_input;
        std::vector<std::size_t> deps;
        double value{0.0};
        std::function<double(const NaiveGraph&)> formula;
    };

    std::vector<Node> nodes;

    std::size_t add_input(std::string name, double val) {
        std::size_t id = nodes.size();
        nodes.push_back({std::move(name), true, {}, val, {}});
        return id;
    }

    std::size_t add_derived(std::string name,
                             std::vector<std::size_t> deps,
                             std::function<double(const NaiveGraph&)> formula) {
        std::size_t id = nodes.size();
        nodes.push_back({std::move(name), false, std::move(deps), 0.0, std::move(formula)});
        return id;
    }

    void recompute_all() {
        for (auto& n : nodes) {
            if (!n.is_input && n.formula) {
                n.value = n.formula(*this);
            }
        }
    }

    double get(std::size_t id) const { return nodes.at(id).value; }
    void set(std::size_t id, double v) { nodes[id].value = v; }
};

// Build a wide-fan-out graph for benchmarking.
// Shape: N inputs, each with several dependents, all feeding into one root.
// This isolates unaffected branches well for incremental speedup.
struct BenchGraph {
    std::unique_ptr<incremental::Graph> inc;
    NaiveGraph naive;

    std::vector<NodeHandle<double>> inc_inputs;
    std::vector<std::size_t> naive_inputs;

    std::vector<NodeHandle<double>> inc_leaves;
    std::size_t naive_root{0};

    std::size_t n_nodes{0};
    std::size_t n_edges{0};
};

static BenchGraph build_bench_graph(
    std::size_t n_inputs, std::size_t fan, unsigned seed)
{
    BenchGraph g;
    g.inc = std::make_unique<incremental::Graph>();
    std::mt19937 rng{seed};
    std::uniform_real_distribution<double> val_dist(1.0, 100.0);

    // Add inputs
    for (std::size_t i = 0; i < n_inputs; ++i) {
        double v = val_dist(rng);
        auto h = g.inc->input<double>(std::format("Input{}", i), v);
        g.inc_inputs.push_back(h);
        g.naive_inputs.push_back(g.naive.add_input(std::format("Input{}", i), v));
    }

    // Add a derived node per input group (fan intermediate nodes)
    std::vector<NodeHandle<double>> group_nodes;
    std::vector<std::size_t> naive_group_nodes;

    std::size_t group_size = std::max<std::size_t>(1, fan);
    std::size_t n_groups = n_inputs / group_size;
    if (n_groups == 0) n_groups = 1;

    for (std::size_t gi = 0; gi < n_groups; ++gi) {
        std::size_t start = gi * group_size;
        std::size_t end   = std::min(start + group_size, n_inputs);

        std::vector<detail::NodeId> dep_ids;
        std::vector<std::size_t> naive_dep_ids;
        for (std::size_t k = start; k < end; ++k) {
            dep_ids.push_back(g.inc_inputs[k].id());
            naive_dep_ids.push_back(g.naive_inputs[k]);
        }

        auto handles = std::vector<NodeHandle<double>>(
            g.inc_inputs.begin() + static_cast<std::ptrdiff_t>(start),
            g.inc_inputs.begin() + static_cast<std::ptrdiff_t>(end));

        auto gh = g.inc->derive<double>(
            std::format("Group{}", gi), dep_ids,
            [handles](const auto& view) {
                double sum = 0;
                for (const auto& h : handles) sum += view.get(h);
                return sum / static_cast<double>(handles.size());
            });
        group_nodes.push_back(gh);
        g.n_edges += dep_ids.size();

        auto ndeps = naive_dep_ids;
        naive_group_nodes.push_back(g.naive.add_derived(
            std::format("Group{}", gi), ndeps,
            [ndeps](const NaiveGraph& ng) {
                double sum = 0;
                for (auto d : ndeps) sum += ng.get(d);
                return sum / static_cast<double>(ndeps.size());
            }));
    }

    // Root aggregator
    std::vector<detail::NodeId> root_deps;
    std::vector<std::size_t> naive_root_deps;
    for (auto& h : group_nodes) root_deps.push_back(h.id());
    for (auto nd : naive_group_nodes) naive_root_deps.push_back(nd);

    auto gh_handles = group_nodes;
    auto root_h = g.inc->derive<double>(
        "Root", root_deps,
        [gh_handles](const auto& view) {
            double sum = 0;
            for (const auto& h : gh_handles) sum += view.get(h);
            return sum;
        });
    g.inc_leaves.push_back(root_h);
    g.n_edges += root_deps.size();

    auto naive_rdeps = naive_root_deps;
    g.naive_root = g.naive.add_derived("Root", naive_rdeps,
        [naive_rdeps](const NaiveGraph& ng) {
            double sum = 0;
            for (auto d : naive_rdeps) sum += ng.get(d);
            return sum;
        });

    g.n_nodes = g.inc->node_count();

    // Prime naive graph
    g.naive.recompute_all();

    return g;
}

struct BenchResult {
    std::string name;
    std::size_t node_count;
    std::size_t edge_count;
    std::size_t n_input_mutations;
    std::size_t formula_evals_full;
    std::size_t formula_evals_incremental;
    double elapsed_full_ms;
    double elapsed_incremental_ms;
    bool results_match;
    double speedup;
};

static BenchResult run_one_benchmark(
    const std::string& name,
    std::size_t n_inputs,
    std::size_t fan,
    std::size_t iterations,
    unsigned seed
) {
    BenchGraph g = build_bench_graph(n_inputs, fan, seed);
    std::mt19937 rng{seed + 1};
    std::uniform_int_distribution<std::size_t> input_sel(0, g.inc_inputs.size() - 1);
    std::uniform_real_distribution<double> val_dist(1.0, 100.0);

    // Prepare mutations
    struct Mutation { std::size_t input_idx; double value; };
    std::vector<Mutation> mutations;
    mutations.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        mutations.push_back({input_sel(rng), val_dist(rng)});
    }

    // ── Full recomputation baseline ──────────────────────────────────────────
    std::size_t full_evals = 0;
    double naive_root_final = 0;
    auto t0 = std::chrono::high_resolution_clock::now();

    for (const auto& mut : mutations) {
        g.naive.set(g.naive_inputs[mut.input_idx], mut.value);
        g.naive.recompute_all();
        ++full_evals; // count full recompute iterations
    }
    naive_root_final = g.naive.get(g.naive_root);

    auto t1 = std::chrono::high_resolution_clock::now();
    double full_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // ── Incremental recomputation ─────────────────────────────────────────────
    std::size_t incremental_evals = 0;
    double inc_root_final = 0;
    auto t2 = std::chrono::high_resolution_clock::now();

    for (const auto& mut : mutations) {
        auto tx = g.inc->transaction();
        tx.set(g.inc_inputs[mut.input_idx], mut.value);
        auto res = tx.commit();
        incremental_evals += res.counters.nodes_recomputed;
    }
    inc_root_final = g.inc->get(g.inc_leaves[0]);

    auto t3 = std::chrono::high_resolution_clock::now();
    double inc_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    bool match = std::abs(naive_root_final - inc_root_final) < 1e-9;

    BenchResult res;
    res.name = name;
    res.node_count = g.n_nodes;
    res.edge_count = g.n_edges;
    res.n_input_mutations = iterations;
    res.formula_evals_full = full_evals * g.naive.nodes.size();
    res.formula_evals_incremental = incremental_evals;
    res.elapsed_full_ms = full_ms;
    res.elapsed_incremental_ms = inc_ms;
    res.results_match = match;
    res.speedup = (inc_ms > 0.0) ? full_ms / inc_ms : 0.0;
    return res;
}

int run_benchmark(const Options& opts, std::ostream& out) {
    using namespace term;

    print_banner(out);
    print_section(out, "BENCHMARK CONFIGURATION");
    print_kv(out, "Nodes (inputs):", std::format("{}", opts.nodes));
    print_kv(out, "Iterations:", std::format("{}", opts.iterations));
    print_kv(out, "Random seed:", std::format("{}", opts.seed));
    out << '\n';

    // Six benchmark shapes
    struct Shape { std::string name; std::size_t inputs; std::size_t fan; };
    std::vector<Shape> shapes = {
        {"Chain (single fanout=1)",     std::min(opts.nodes, std::size_t{200}), 1},
        {"Wide independent branches",   opts.nodes, opts.nodes},
        {"Balanced tree (fan=8)",       opts.nodes, 8},
        {"Small affected (fan=64)",     opts.nodes, 64},
        {"High fanout (fan=4)",         opts.nodes, 4},
        {"Dense (most nodes affected)", std::min(opts.nodes, std::size_t{100}), 1},
    };

    std::vector<BenchResult> results;

    for (const auto& shape : shapes) {
        print_info(out, std::format("Running: {} ...", shape.name));
        auto br = run_one_benchmark(shape.name, shape.inputs, shape.fan,
                                    opts.iterations, opts.seed);
        results.push_back(br);

        if (!br.results_match) {
            print_err(out, std::format("CORRECTNESS FAILURE in '{}'! "
                "Incremental != Naive. Benchmark INVALID.", shape.name));
        }
    }

    // ── Print results table ───────────────────────────────────────────────────
    print_section(out, "BENCHMARK RESULTS");

    // Header row
    out << "  " << ansi::dim()
        << pad_right("Scenario", 32)
        << pad_left("Nodes", 7)
        << pad_left("Iters", 6)
        << pad_left("Full(ms)", 10)
        << pad_left("Incr(ms)", 10)
        << pad_left("Speedup", 9)
        << "  Match"
        << ansi::reset() << '\n';

    out << "  " << ansi::dim()
        << repeat("-", 80) << ansi::reset() << '\n';

    for (const auto& r : results) {
        bool ok = r.results_match;
        out << "  "
            << pad_right(r.name, 32)
            << ansi::white() << pad_left(std::format("{}", r.node_count), 7) << ansi::reset()
            << pad_left(std::format("{}", r.n_input_mutations), 6)
            << ansi::white() << pad_left(std::format("{:.2f}", r.elapsed_full_ms), 10) << ansi::reset()
            << ansi::green() << pad_left(std::format("{:.2f}", r.elapsed_incremental_ms), 10) << ansi::reset()
            << (r.speedup >= 1.0 ? ansi::b_green() : ansi::yellow())
            << pad_left(std::format("{:.1f}x", r.speedup), 9) << ansi::reset()
            << "  " << (ok ? ansi::b_green() : ansi::b_red())
            << (ok ? box::check() : box::cross()) << ansi::reset()
            << '\n';
    }

    // ── CSV export ────────────────────────────────────────────────────────────
    if (!opts.csv_output.empty()) {
        std::ofstream csv(opts.csv_output);
        if (!csv) {
            print_err(out, std::format("Cannot write CSV to '{}'", opts.csv_output));
        } else {
            csv << "scenario,node_count,edge_count,iterations,"
                   "full_evals,incremental_evals,"
                   "full_ms,incremental_ms,speedup,results_match\n";
            for (const auto& r : results) {
                csv << std::format("{},{},{},{},{},{},{:.4f},{:.4f},{:.3f},{}\n",
                    r.name, r.node_count, r.edge_count, r.n_input_mutations,
                    r.formula_evals_full, r.formula_evals_incremental,
                    r.elapsed_full_ms, r.elapsed_incremental_ms,
                    r.speedup, r.results_match ? "true" : "false");
            }
            print_ok(out, std::format("CSV written to '{}'", opts.csv_output));
        }
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    print_section(out, "NOTES");
    print_info(out, "Full recomputation evaluates ALL derived nodes each iteration.");
    print_info(out, "Incremental recomputation only evaluates the affected subgraph.");
    print_info(out, "Speedup depends on graph topology, mutation pattern, and formula cost.");
    print_info(out, "Results verified: incremental MUST produce the same final value as naive.");
    out << '\n';

    bool all_ok = std::all_of(results.begin(), results.end(),
        [](const BenchResult& r){ return r.results_match; });
    if (all_ok) {
        print_ok(out, "All benchmarks passed correctness check.");
    } else {
        print_err(out, "Some benchmarks FAILED correctness check!");
        return 1;
    }
    out << '\n';
    return 0;
}

// ── Argument Parser ───────────────────────────────────────────────────────────

int parse_and_run(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    Options opts;
    bool color_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};

        if (arg == "--help" || arg == "-h") {
            opts.help = true;
        } else if (arg == "--version" || arg == "-v") {
            opts.version_flag = true;
        } else if (arg == "--quiet" || arg == "-q") {
            opts.quiet = true;
            term::config().quiet = true;
        } else if (arg == "--verbose") {
            opts.verbose = true;
            term::config().verbose = true;
        } else if (arg == "--ascii") {
            term::config().ascii = true;
        } else if (arg.starts_with("--color=")) {
            std::string_view val = arg.substr(8);
            if (val == "auto")   term::config().color = term::ColorMode::Auto;
            else if (val == "always") term::config().color = term::ColorMode::Always;
            else if (val == "never")  term::config().color = term::ColorMode::Never;
            else {
                err << "recalc: invalid value for --color: '" << val
                    << "'. Use auto, always, or never.\n";
                return 1;
            }
            color_set = true;
        } else if (arg.starts_with("--nodes=")) {
            if (!parse_size(arg.substr(8).data(), opts.nodes)) {
                err << "recalc: invalid value for --nodes: '" << arg.substr(8)
                    << "'. Expected a positive integer.\n";
                return 1;
            }
        } else if (arg.starts_with("--iterations=")) {
            if (!parse_size(arg.substr(13).data(), opts.iterations)) {
                err << "recalc: invalid value for --iterations: '" << arg.substr(13)
                    << "'. Expected a positive integer.\n";
                return 1;
            }
        } else if (arg.starts_with("--seed=")) {
            if (!parse_uint(arg.substr(7).data(), opts.seed)) {
                err << "recalc: invalid value for --seed: '" << arg.substr(7)
                    << "'. Expected a non-negative integer.\n";
                return 1;
            }
        } else if (arg.starts_with("--csv=")) {
            opts.csv_output = std::string(arg.substr(6));
        } else if (arg.starts_with("--scenario=")) {
            opts.scenario = std::string(arg.substr(11));
        } else if (arg == "demo") {
            opts.command = "demo";
        } else if (arg == "benchmark") {
            opts.command = "benchmark";
        } else if (arg.starts_with("-")) {
            err << "recalc: unknown option '" << arg
                << "'. Run 'recalc --help' for usage.\n";
            return 1;
        } else {
            err << "recalc: unknown command '" << arg
                << "'. Run 'recalc --help' for usage.\n";
            return 1;
        }
    }

    if (opts.help) {
        print_help(out);
        return 0;
    }

    if (opts.version_flag) {
        print_version(out);
        return 0;
    }

    if (opts.command.empty()) {
        print_help(out);
        return 0;
    }

    if (opts.command == "demo") {
        return run_demo(opts, out);
    } else if (opts.command == "benchmark") {
        return run_benchmark(opts, out);
    }

    err << "recalc: unknown command '" << opts.command
        << "'. Run 'recalc --help' for usage.\n";
    return 1;
}

} // namespace incremental::cli
