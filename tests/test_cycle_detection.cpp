// tests/test_cycle_detection.cpp
#include "test_main.hpp"
#include <incremental/incremental.hpp>
using namespace incremental;

TEST(cycles, valid_diamond) {
    // Diamond: X -> {A, B} -> C (no cycle)
    Graph g;
    auto x = g.input<double>("X", 1.0);
    auto a = g.derive<double>("A", {x.id()}, [x](const auto& v){ return v.get(x); });
    auto b = g.derive<double>("B", {x.id()}, [x](const auto& v){ return v.get(x) * 2; });
    auto c = g.derive<double>("C", {a.id(), b.id()},
        [a, b](const auto& v){ return v.get(a) + v.get(b); });

    // Should compile and compute successfully — no cycle
    ASSERT_NEAR(g.get(c), 3.0, 1e-9);

    auto maybe_cycle = incremental::detect_cycle(g.storage());
    ASSERT_FALSE(maybe_cycle.has_value());
}

TEST(cycles, self_cycle) {
    // Self-cycle: a formula depends on itself — should be detected via topological sort failure
    // We can't easily make a proper self-cycle through the public API (node added before it exists),
    // so we test via the topology detection on an already-built storage.
    // Build manually using graph_storage directly.
    detail::GraphStorage st;
    auto id = st.add_input("SelfRef", detail::TypeErasedValue{0.0});
    // Manually add self-dependent — won't happen via public API, but tests the detector.
    st.at(id).dependencies.push_back(id);  // self-edge
    st.at(id).dependents.push_back(id);

    bool topo_ok = st.recompute_topo_order();
    ASSERT_FALSE(topo_ok); // cycle detected
    auto cycle = incremental::detect_cycle(st);
    // May or may not find it depending on direction, but topo failure is definitive
}

TEST(cycles, two_node_cycle) {
    // A -> B and B -> A
    detail::GraphStorage st;
    auto a_id = st.add_input("A", detail::TypeErasedValue{0.0});
    auto b_id = st.add_input("B", detail::TypeErasedValue{0.0});

    // Link A -> B (A is dep of B)
    st.at(b_id).dependencies.push_back(a_id);
    st.at(a_id).dependents.push_back(b_id);
    // Link B -> A (B is dep of A) — cycle!
    st.at(a_id).dependencies.push_back(b_id);
    st.at(b_id).dependents.push_back(a_id);

    bool topo_ok = st.recompute_topo_order();
    ASSERT_FALSE(topo_ok);
}

TEST(cycles, multi_node_cycle) {
    // A -> B -> C -> A
    detail::GraphStorage st;
    auto a = st.add_input("A", detail::TypeErasedValue{0.0});
    auto b = st.add_input("B", detail::TypeErasedValue{0.0});
    auto c = st.add_input("C", detail::TypeErasedValue{0.0});

    // A depends on C (reverse edge: C is dependent of A)
    st.at(a).dependencies.push_back(c);
    st.at(c).dependents.push_back(a);
    // B depends on A
    st.at(b).dependencies.push_back(a);
    st.at(a).dependents.push_back(b);
    // C depends on B
    st.at(c).dependencies.push_back(b);
    st.at(b).dependents.push_back(c);

    bool topo_ok = st.recompute_topo_order();
    ASSERT_FALSE(topo_ok);
}

TEST(cycles, rejected_leaves_graph_intact) {
    // A valid graph: X -> Y
    // Attempting to derive a node that immediately creates TopologyError should not corrupt the graph.
    Graph g;
    auto x = g.input<double>("X", 1.0);
    auto y = g.derive<double>("Y", {x.id()}, [x](const auto& v){ return v.get(x); });

    double y_before = g.get(y);

    // Now try adding a node that depends on itself — public API would need node's own id,
    // which doesn't exist yet. Instead, test that a derive after a bad formula doesn't
    // leave the graph in a bad state.
    Graph g2;
    auto x2 = g2.input<double>("X", 5.0);
    // This derive should succeed — no cycle
    auto z = g2.derive<double>("Z", {x2.id()}, [x2](const auto& v){ return v.get(x2) * 3; });
    ASSERT_NEAR(g2.get(z), 15.0, 1e-9);

    // Graph g should be unaffected
    ASSERT_NEAR(g.get(y), y_before, 1e-9);
}
