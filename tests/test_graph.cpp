// tests/test_graph.cpp
#include "test_main.hpp"
#include <incremental/incremental.hpp>
using namespace incremental;

TEST(graph, create_input_nodes) {
    Graph g;
    auto h = g.input<double>("Revenue", 120.0);
    ASSERT_TRUE(h.valid());
    ASSERT_NEAR(g.get(h), 120.0, 1e-9);
    ASSERT_EQ(g.node_count(), 1u);
}

TEST(graph, create_derived_nodes) {
    Graph g;
    auto a = g.input<double>("A", 10.0);
    auto b = g.input<double>("B", 5.0);
    auto sum = g.derive<double>("Sum", {a.id(), b.id()},
        [a, b](const auto& v){ return v.get(a) + v.get(b); });
    ASSERT_TRUE(sum.valid());
    ASSERT_NEAR(g.get(sum), 15.0, 1e-9);
}

TEST(graph, read_computed_values) {
    Graph g;
    auto a = g.input<int>("A", 3);
    auto b = g.input<int>("B", 4);
    auto s = g.derive<int>("S", {a.id(), b.id()},
        [a, b](const auto& v){ return v.get(a) + v.get(b); });
    ASSERT_EQ(g.get(s), 7);
}

TEST(graph, reject_invalid_handles) {
    Graph g;
    NodeHandle<double> bad_handle;
    ASSERT_THROWS(g.get(bad_handle), HandleError);
}

TEST(graph, cross_graph_handle_rejection) {
    Graph g1, g2;
    auto h1 = g1.input<double>("X", 1.0);
    // h1 belongs to g1; using it with g2's transaction should throw
    auto tx = g2.transaction();
    ASSERT_THROWS(tx.set(h1, 2.0), HandleError);
}
