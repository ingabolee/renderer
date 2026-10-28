// tests/test_exception_safety.cpp
#include "test_main.hpp"
#include <incremental/incremental.hpp>
using namespace incremental;

TEST(exception_safety, strong_guarantee) {
    // Complex graph with a formula that conditionally throws.
    // After a failed commit, all committed values must remain unchanged.
    Graph g;
    auto rev = g.input<double>("Revenue", 100.0);
    auto cost = g.input<double>("Cost", 60.0);
    auto profit = g.derive<double>("Profit", {rev.id(), cost.id()},
        [rev, cost](const auto& v){ return v.get(rev) - v.get(cost); });
    auto margin = g.derive<double>("Margin", {profit.id(), rev.id()},
        [profit, rev](const auto& v){
            double r = v.get(rev);
            if (r == 0.0) throw std::domain_error("Revenue zero");
            return v.get(profit) / r * 100.0;
        });

    // Record initial state
    double p0 = g.get(profit);
    double m0 = g.get(margin);

    // Attempt commit that causes division by zero
    auto tx = g.transaction();
    tx.set(rev, 0.0);
    auto res = tx.commit();

    ASSERT_FALSE(res.committed);
    ASSERT_NEAR(g.get(rev), 100.0, 1e-9);     // unchanged
    ASSERT_NEAR(g.get(cost), 60.0, 1e-9);     // unchanged
    ASSERT_NEAR(g.get(profit), p0, 1e-9);     // unchanged
    ASSERT_NEAR(g.get(margin), m0, 1e-9);     // unchanged
}

TEST(exception_safety, rollback_on_formula_throw) {
    Graph g;
    auto x = g.input<double>("X", 5.0);
    int eval_count = 0;
    auto y = g.derive<double>("Y", {x.id()},
        [x, &eval_count](const auto& v){
            ++eval_count;
            double val = v.get(x);
            if (val < 0) throw std::runtime_error("negative input");
            return val * val;
        });

    ASSERT_NEAR(g.get(y), 25.0, 1e-9);
    int evals_before = eval_count;

    // Failed commit
    auto tx = g.transaction();
    tx.set(x, -3.0);
    auto res = tx.commit();
    ASSERT_FALSE(res.committed);

    // Y should still hold old value
    ASSERT_NEAR(g.get(y), 25.0, 1e-9);
    // X must be unchanged
    ASSERT_NEAR(g.get(x), 5.0, 1e-9);
    // Formula was evaluated at least once (during prepare)
    ASSERT_TRUE(eval_count > evals_before);
}
