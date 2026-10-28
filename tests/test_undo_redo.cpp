// tests/test_undo_redo.cpp
#include "test_main.hpp"
#include <incremental/incremental.hpp>
using namespace incremental;

TEST(undo_redo, undo_one) {
    Graph g;
    auto a = g.input<double>("A", 1.0);

    auto tx = g.transaction("set to 5");
    tx.set(a, 5.0);
    tx.commit();
    ASSERT_NEAR(g.get(a), 5.0, 1e-9);

    auto r = g.undo();
    ASSERT_TRUE(r.committed);
    ASSERT_NEAR(g.get(a), 1.0, 1e-9);
}

TEST(undo_redo, redo_one) {
    Graph g;
    auto a = g.input<double>("A", 1.0);

    auto tx = g.transaction("set to 5");
    tx.set(a, 5.0);
    tx.commit();

    g.undo();
    ASSERT_NEAR(g.get(a), 1.0, 1e-9);

    auto r = g.redo();
    ASSERT_TRUE(r.committed);
    ASSERT_NEAR(g.get(a), 5.0, 1e-9);
}

TEST(undo_redo, undo_multi) {
    Graph g;
    auto a = g.input<double>("A", 0.0);

    {auto tx = g.transaction(); tx.set(a, 1.0); tx.commit();}
    {auto tx = g.transaction(); tx.set(a, 2.0); tx.commit();}
    {auto tx = g.transaction(); tx.set(a, 3.0); tx.commit();}

    ASSERT_NEAR(g.get(a), 3.0, 1e-9);
    g.undo();
    ASSERT_NEAR(g.get(a), 2.0, 1e-9);
    g.undo();
    ASSERT_NEAR(g.get(a), 1.0, 1e-9);
    g.undo();
    ASSERT_NEAR(g.get(a), 0.0, 1e-9);

    // Nothing more to undo
    auto r = g.undo();
    ASSERT_FALSE(r.committed);
}

TEST(undo_redo, branch_after_undo) {
    Graph g;
    auto a = g.input<double>("A", 0.0);

    {auto tx = g.transaction(); tx.set(a, 1.0); tx.commit();}
    {auto tx = g.transaction(); tx.set(a, 2.0); tx.commit();}

    g.undo(); // back to 1.0

    // Branch: new edit invalidates redo history
    {auto tx = g.transaction(); tx.set(a, 99.0); tx.commit();}
    ASSERT_NEAR(g.get(a), 99.0, 1e-9);

    // Cannot redo the old 2.0 branch
    auto r = g.redo();
    ASSERT_FALSE(r.committed);
}

TEST(undo_redo, derived_recomputed) {
    Graph g;
    auto a = g.input<double>("A", 10.0);
    auto b = g.derive<double>("B", {a.id()}, [a](const auto& v){ return v.get(a) * 2; });

    {auto tx = g.transaction(); tx.set(a, 20.0); tx.commit();}
    ASSERT_NEAR(g.get(b), 40.0, 1e-9);

    g.undo();
    ASSERT_NEAR(g.get(a), 10.0, 1e-9);
    ASSERT_NEAR(g.get(b), 20.0, 1e-9); // Derived recomputed via undo transaction
}
