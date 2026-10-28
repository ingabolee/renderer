// examples/graph_examples.cpp
// Additional library usage examples.
#include <incremental/incremental.hpp>
#include <incremental/customization_points.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <format>

using namespace incremental;

void example_cycle_detection() {
    std::cout << "\n--- Cycle Detection Example ---\n";
    // Valid acyclic graph
    detail::GraphStorage st;
    auto a = st.add_input("A", detail::TypeErasedValue{1.0});
    auto b = st.add_input("B", detail::TypeErasedValue{2.0});
    st.at(b).dependencies.push_back(a);
    st.at(a).dependents.push_back(b);
    bool ok = st.recompute_topo_order();
    auto cycle = incremental::detect_cycle(st);
    std::cout << std::format("Valid DAG: no cycle = {}\n", !cycle.has_value());
}

// Container with member for_each — must be at namespace scope (not local class)
struct MemberContainer {
    void for_each(std::function<void(int)> cb) const {
        cb(10); cb(20); cb(30);
    }
};

void example_customization_point() {
    std::cout << "\n--- Customization Point Example ---\n";

    // Range protocol (std::vector)
    std::vector<int> v{1, 2, 3, 4, 5};
    int sum = 0;
    incremental::for_each(v, [&](int x){ sum += x; });
    std::cout << std::format("Sum of [1..5] via range protocol: {}\n", sum);

    // Member protocol
    int total = 0;
    MemberContainer c;
    incremental::for_each(c, [&](int x){ total += x; });
    std::cout << std::format("Sum via member protocol: {}\n", total);
}

void example_dependency_traversal() {
    std::cout << "\n--- Dependency Traversal Example ---\n";
    Graph g;
    auto x = g.input<double>("X", 1.0);
    auto a = g.derive<double>("A", {x.id()}, [x](const auto& v){ return v.get(x)*2; });
    auto b = g.derive<double>("B", {x.id()}, [x](const auto& v){ return v.get(x)*3; });
    auto c = g.derive<double>("C", {a.id(), b.id()},
        [a,b](const auto& v){ return v.get(a)+v.get(b); });

    std::cout << "Dependents of X: ";
    incremental::visit_dependents(g.storage(), x.id(), [&](detail::NodeId id){
        std::cout << g.node_name(id) << " ";
    });
    std::cout << '\n';
}

int main() {
    std::cout << "=== incremental library - Additional Examples ===\n";
    example_cycle_detection();
    example_customization_point();
    example_dependency_traversal();
    std::cout << "\nAll examples completed.\n";
    return 0;
}
