// include/incremental/cycle_detection.hpp
// Public API for cycle detection diagnostics.
// (The algorithm lives in detail/topology.hpp)
#pragma once

#include <incremental/detail/topology.hpp>
#include <string>
#include <vector>
#include <optional>

namespace incremental {

/// Check if the graph contains any dependency cycles.
/// Returns the cycle path (node names) or std::nullopt if the graph is acyclic.
[[nodiscard]] inline std::optional<std::vector<std::string>>
detect_cycle(const detail::GraphStorage& g) {
    return detail::find_cycle(g);
}

/// Format a cycle path into a human-readable string.
/// Example: "Revenue -> Profit -> Revenue"
[[nodiscard]] inline std::string
format_cycle(const std::vector<std::string>& path) {
    if (path.empty()) return "(empty cycle)";
    std::string result;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i > 0) result += " -> ";
        result += path[i];
    }
    return result;
}

} // namespace incremental
