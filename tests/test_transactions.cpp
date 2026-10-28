// tests/test_transactions.cpp
#include "test_main.hpp"
#include <incremental/incremental.hpp>
using namespace incremental;

TEST(transactions, commit_valid) {
    Graph g;
    auto a = g.input<int>("A", 1);
    auto tx = g.transaction();
    tx.set(a, 42);
    auto res = tx.commit();
    ASSERT_TRUE(res.committed);
    ASSERT_EQ(g.get(a), 42);
}

TEST(transactions, abandon_uncommitted) {
    Graph g;
    auto a = g.input<int>("A", 1);
    {
        auto tx = g.transaction();
        tx.set(a, 99);
        // tx destroyed without commit
    }
    ASSERT_EQ(g.get(a), 1); // unchanged
}

TEST(transactions, reject_invalid_input_changes) {
    Graph g;
    auto a = g.input<double>("A", 1.0);
    // Try setting a double node with wrong type — ASSERT_THROWS
    // We can't easily do type mismatch at compile time between different Ts,
    // but we can test setting a derived node.
    auto b = g.derive<double>("B", {a.id()}, [a](const auto& v){ return v.get(a); });
    auto tx = g.transaction();
    ASSERT_THROWS(tx.set(b, 2.0), TransactionError);
}

TEST(transactions, formula_throws_no_partial_state) {
    Graph g;
    auto x = g.input<double>("X", 10.0);
    auto y = g.derive<double>("Y", {x.id()},
        [x](const auto& v){
            if (v.get(x) <= 0) throw std::domain_error("x must be positive");
            return v.get(x) * 2;
        });

    // Commit valid first
    {
        auto tx = g.transaction();
        tx.set(x, 5.0);
        auto res = tx.commit();
        ASSERT_TRUE(res.committed);
        ASSERT_NEAR(g.get(y), 10.0, 1e-9);
    }

    // Now trigger formula exception
    {
        auto tx = g.transaction();
        tx.set(x, -1.0);
        auto res = tx.commit();
        ASSERT_FALSE(res.committed);
        // Committed state must be unchanged
        ASSERT_NEAR(g.get(x), 5.0, 1e-9);
        ASSERT_NEAR(g.get(y), 10.0, 1e-9);
    }
}

TEST(transactions, no_notify_on_failed_commit) {
    Graph g;
    auto x = g.input<double>("X", 1.0);
    auto y = g.derive<double>("Y", {x.id()},
        [x](const auto& v){
            if (v.get(x) < 0) throw std::runtime_error("negative");
            return v.get(x);
        });

    int notify_count = 0;
    g.subscribe([&](const ChangeEvent&){ ++notify_count; });

    auto tx = g.transaction();
    tx.set(x, -5.0);
    auto res = tx.commit();

    ASSERT_FALSE(res.committed);
    ASSERT_EQ(notify_count, 0); // No notification on failed commit
}

TEST(transactions, notification_order) {
    // Observers get called only after all values are published.
    Graph g;
    auto a = g.input<double>("A", 1.0);
    auto b = g.derive<double>("B", {a.id()}, [a](const auto& v){ return v.get(a) * 2; });

    double seen_a = 0, seen_b = 0;
    g.subscribe([&](const ChangeEvent&){
        // At notification time, both committed values must reflect new state.
        seen_a = g.get(a);
        seen_b = g.get(b);
    });

    auto tx = g.transaction();
    tx.set(a, 5.0);
    tx.commit();

    ASSERT_NEAR(seen_a, 5.0, 1e-9);
    ASSERT_NEAR(seen_b, 10.0, 1e-9);
}
