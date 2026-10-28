// tests/test_incremental_recalculation.cpp
#include "test_main.hpp"
#include <incremental/incremental.hpp>
using namespace incremental;

TEST(incremental, single_input_change) {
    Graph g;
    auto a = g.input<double>("A", 10.0);
    auto b = g.input<double>("B", 5.0);
    auto s = g.derive<double>("S", {a.id(), b.id()},
        [a, b](const auto& v){ return v.get(a) + v.get(b); });

    auto tx = g.transaction();
    tx.set(a, 20.0);
    auto res = tx.commit();

    ASSERT_TRUE(res.committed);
    ASSERT_NEAR(g.get(s), 25.0, 1e-9);
    ASSERT_EQ(res.counters.inputs_changed, 1u);
    ASSERT_EQ(res.counters.nodes_recomputed, 1u);
}

TEST(incremental, multi_input_change) {
    Graph g;
    auto a = g.input<double>("A", 10.0);
    auto b = g.input<double>("B", 5.0);
    auto s = g.derive<double>("S", {a.id(), b.id()},
        [a, b](const auto& v){ return v.get(a) + v.get(b); });

    auto tx = g.transaction();
    tx.set(a, 20.0);
    tx.set(b, 15.0);
    auto res = tx.commit();

    ASSERT_TRUE(res.committed);
    ASSERT_NEAR(g.get(s), 35.0, 1e-9);
    ASSERT_EQ(res.counters.inputs_changed, 2u);
}

TEST(incremental, shared_dependency_once) {
    Graph g;
    auto x = g.input<double>("X", 1.0);
    // Two independent deriveds both depending on x, and one aggregator
    auto a = g.derive<double>("A", {x.id()}, [x](const auto& v){ return v.get(x) * 2; });
    auto b = g.derive<double>("B", {x.id()}, [x](const auto& v){ return v.get(x) * 3; });
    auto s = g.derive<double>("S", {a.id(), b.id()},
        [a, b](const auto& v){ return v.get(a) + v.get(b); });

    auto tx = g.transaction();
    tx.set(x, 2.0);
    auto res = tx.commit();

    ASSERT_TRUE(res.committed);
    ASSERT_NEAR(g.get(s), 10.0, 1e-9); // 4 + 6
    // All three derived nodes should be recomputed (a, b, s)
    ASSERT_EQ(res.counters.nodes_recomputed, 3u);
}

TEST(incremental, unrelated_untouched) {
    Graph g;
    auto x = g.input<double>("X", 1.0);
    auto y = g.input<double>("Y", 100.0);
    auto a = g.derive<double>("A", {x.id()}, [x](const auto& v){ return v.get(x) * 2; });
    auto b = g.derive<double>("B", {y.id()}, [y](const auto& v){ return v.get(y) * 3; });

    double b_before = g.get(b);

    auto tx = g.transaction();
    tx.set(x, 2.0);
    auto res = tx.commit();

    ASSERT_TRUE(res.committed);
    ASSERT_NEAR(g.get(a), 4.0, 1e-9);
    ASSERT_NEAR(g.get(b), b_before, 1e-9); // B unaffected
    ASSERT_EQ(res.counters.nodes_recomputed, 1u); // only A recomputed
    ASSERT_EQ(res.counters.nodes_skipped, 1u); // B skipped
}

TEST(incremental, long_chain) {
    Graph g;
    auto input = g.input<double>("X", 1.0);
    std::vector<NodeHandle<double>> chain;
    chain.push_back(input);

    for (int i = 0; i < 10; ++i) {
        auto prev = chain.back();
        auto next = g.derive<double>("N" + std::to_string(i), {prev.id()},
            [prev](const auto& v){ return v.get(prev) + 1.0; });
        chain.push_back(next);
    }

    auto tx = g.transaction();
    tx.set(input, 0.0);
    auto res = tx.commit();

    ASSERT_TRUE(res.committed);
    ASSERT_NEAR(g.get(chain.back()), 10.0, 1e-9);
    ASSERT_EQ(res.counters.nodes_recomputed, 10u);
}

TEST(incremental, empty_transaction) {
    Graph g;
    auto a = g.input<double>("A", 5.0);
    auto tx = g.transaction();
    // No set() calls - empty transaction
    auto res = tx.commit();
    ASSERT_TRUE(res.committed);
    ASSERT_EQ(res.counters.inputs_changed, 0u);
    ASSERT_NEAR(g.get(a), 5.0, 1e-9);
}

TEST(incremental, results_match_full_recompute) {
    // Build graph, change one input, compare incremental result vs manual computation.
    Graph g;
    auto r = g.input<double>("R", 120.0);
    auto c = g.input<double>("C", 80.0);
    auto p = g.derive<double>("P", {r.id(), c.id()},
        [r, c](const auto& v){ return v.get(r) - v.get(c); });
    auto m = g.derive<double>("M", {p.id(), r.id()},
        [p, r](const auto& v){ return v.get(p) / v.get(r) * 100.0; });

    auto tx = g.transaction();
    tx.set(r, 150.0);
    tx.set(c, 90.0);
    auto res = tx.commit();

    ASSERT_TRUE(res.committed);
    double expected_p = 150.0 - 90.0;
    double expected_m = expected_p / 150.0 * 100.0;
    ASSERT_NEAR(g.get(p), expected_p, 1e-9);
    ASSERT_NEAR(g.get(m), expected_m, 1e-9);
}
