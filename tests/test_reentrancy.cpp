// tests/test_reentrancy.cpp
#include "test_main.hpp"
#include <incremental/incremental.hpp>
using namespace incremental;

TEST(reentrancy, callback_reads_consistent) {
    Graph g;
    auto a = g.input<double>("A", 1.0);
    auto b = g.derive<double>("B", {a.id()}, [a](const auto& v){ return v.get(a) * 2; });

    double seen_a = 0, seen_b = 0;
    g.subscribe([&](const ChangeEvent&) {
        seen_a = g.get(a);
        seen_b = g.get(b);
    });

    auto tx = g.transaction();
    tx.set(a, 5.0);
    tx.commit();

    ASSERT_NEAR(seen_a, 5.0, 1e-9);
    ASSERT_NEAR(seen_b, 10.0, 1e-9);
}

TEST(reentrancy, callback_queues_mutation) {
    Graph g;
    auto a = g.input<double>("A", 1.0);
    bool queued = false;

    g.subscribe([&](const ChangeEvent& ev) {
        if (!queued && ev.origin == ChangeEvent::Origin::Normal) {
            queued = true;
            g.enqueue_mutation([&]() {
                auto tx = g.transaction("queued");
                tx.set(a, 99.0);
                tx.commit();
            });
        }
    });

    auto tx = g.transaction();
    tx.set(a, 10.0);
    tx.commit();

    // After drain, queued mutation should have fired
    ASSERT_NEAR(g.get(a), 99.0, 1e-9);
}

TEST(reentrancy, queued_processed_separately) {
    Graph g;
    auto a = g.input<double>("A", 0.0);
    std::vector<double> observed_values;

    g.subscribe([&](const ChangeEvent& ev) {
        observed_values.push_back(g.get(a));
        if (ev.origin == ChangeEvent::Origin::Normal && observed_values.size() == 1) {
            g.enqueue_mutation([&]() {
                auto tx = g.transaction("queued");
                tx.set(a, 200.0);
                tx.commit();
            });
        }
    });

    auto tx = g.transaction();
    tx.set(a, 100.0);
    tx.commit();

    // Observer called twice: once for normal (sees 100), once for queued (sees 200)
    ASSERT_EQ(observed_values.size(), 2u);
    ASSERT_NEAR(observed_values[0], 100.0, 1e-9);
    ASSERT_NEAR(observed_values[1], 200.0, 1e-9);
}

TEST(reentrancy, callback_throws) {
    // Observer exception should not roll back committed state.
    Graph g;
    auto a = g.input<double>("A", 1.0);
    bool threw = false;

    g.subscribe([&](const ChangeEvent&) {
        threw = true;
        throw std::runtime_error("Observer intentional error");
    });

    auto tx = g.transaction();
    tx.set(a, 42.0);
    auto res = tx.commit();

    // Transaction reported as committed (observer throw doesn't undo it)
    ASSERT_TRUE(res.committed);
    ASSERT_NEAR(g.get(a), 42.0, 1e-9);
    ASSERT_TRUE(threw);
}

TEST(reentrancy, feedback_loop_limit) {
    // A graph that keeps queuing mutations should hit the safety limit.
    Graph g;
    auto a = g.input<int>("A", 0);
    int loop_count = 0;

    g.subscribe([&](const ChangeEvent& ev) {
        // Every notification queues another mutation — pathological loop
        if (loop_count < 100) {  // prevent infinite memory
            ++loop_count;
            int next_val = g.get(a) + 1;
            g.enqueue_mutation([&, next_val]() {
                auto tx = g.transaction("loop");
                tx.set(a, next_val);
                tx.commit();
            });
        }
    });

    auto tx = g.transaction();
    tx.set(a, 1);
    // Should throw ReentrancyError when depth > limit (32 by default)
    ASSERT_THROWS_ANY(tx.commit());
}
