// examples/spreadsheet_demo.cpp
// Standalone demo program showing library usage. Mirrors the CLI demo but
// uses raw API calls (no CLI argument parsing, no fancy terminal output).
#include <incremental/incremental.hpp>
#include <iostream>
#include <format>
#include <cmath>

int main() {
    using namespace incremental;

    std::cout << "=== incremental library - Spreadsheet Demo ===\n\n";

    // Build the graph
    Graph graph;
    auto revenue = graph.input<double>("Revenue", 120.0);
    auto costs   = graph.input<double>("Costs", 80.0);

    auto profit = graph.derive<double>("Profit", {revenue.id(), costs.id()},
        [revenue, costs](const auto& v){
            return v.get(revenue) - v.get(costs);
        });

    auto margin = graph.derive<double>("Margin", {profit.id(), revenue.id()},
        [profit, revenue](const auto& v){
            return v.get(profit) / v.get(revenue) * 100.0;
        });

    auto axis_max = graph.derive<double>("AxisMaximum", {profit.id()},
        [profit](const auto& v){
            return std::ceil(v.get(profit) / 10.0) * 10.0;
        });

    std::cout << "Initial values:\n";
    std::cout << std::format("  Revenue:     {:.2f}\n", graph.get(revenue));
    std::cout << std::format("  Costs:       {:.2f}\n", graph.get(costs));
    std::cout << std::format("  Profit:      {:.2f}\n", graph.get(profit));
    std::cout << std::format("  Margin:      {:.2f}%\n", graph.get(margin));
    std::cout << std::format("  AxisMaximum: {:.2f}\n", graph.get(axis_max));

    // Multi-input transaction
    std::cout << "\nTransaction: Revenue=130, Costs=90\n";
    auto tx = graph.transaction();
    tx.set(revenue, 130.0);
    tx.set(costs, 90.0);
    auto res = tx.commit();
    std::cout << std::format("  Committed: {}\n", res.committed);
    std::cout << std::format("  Profit:  {:.2f}\n", graph.get(profit));
    std::cout << std::format("  Margin:  {:.2f}%\n", graph.get(margin));
    std::cout << std::format("  Nodes recomputed: {}\n", res.counters.nodes_recomputed);
    std::cout << std::format("  Nodes skipped:    {}\n", res.counters.nodes_skipped);

    // Undo
    std::cout << "\nUndo...\n";
    auto undo_res = graph.undo();
    if (undo_res.committed) {
        std::cout << std::format("  Revenue: {:.2f}\n", graph.get(revenue));
        std::cout << std::format("  Costs:   {:.2f}\n", graph.get(costs));
        std::cout << std::format("  Profit:  {:.2f}\n", graph.get(profit));
    }

    std::cout << "\nDemo complete.\n";
    return 0;
}
