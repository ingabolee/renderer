// include/incremental/detail/topology.hpp
// Cycle detection and dependency traversal utilities.
#pragma once

#include <incremental/detail/graph_storage.hpp>
#include <incremental/error.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <optional>
#include <functional>

namespace incremental::detail {

// ── Cycle detection ────────────────────────────────────────────────────────────
// Uses DFS with three-color marking:
//   WHITE (0): not visited
//   GRAY  (1): currently in the recursion stack (potential back-edge)
//   BLACK (2): fully processed

enum class DFSColor : uint8_t { White = 0, Gray = 1, Black = 2 };

// Returns the cycle path (node names) if one is found, or an empty vector.
// The path forms a cycle: last element connects back to an earlier element.
[[nodiscard]] inline std::optional<std::vector<std::string>>
find_cycle(const GraphStorage& g) {
    std::unordered_map<NodeId, DFSColor> color;
    std::unordered_map<NodeId, NodeId>   parent;

    // We'll reconstruct the cycle path using the parent map.
    std::vector<std::string> cycle_path;

    std::function<bool(NodeId)> dfs = [&](NodeId u) -> bool {
        color[u] = DFSColor::Gray;
        for (NodeId v : g.at(u).dependents) {
            if (color[v] == DFSColor::Gray) {
                // Reconstruct cycle: walk back through parents from u to v.
                cycle_path.push_back(g.at(v).name);
                NodeId cur = u;
                while (cur != v) {
                    cycle_path.push_back(g.at(cur).name);
                    cur = parent.count(cur) ? parent.at(cur) : v;
                }
                cycle_path.push_back(g.at(v).name); // close the cycle
                std::reverse(cycle_path.begin(), cycle_path.end());
                return true;
            }
            if (color[v] == DFSColor::White) {
                parent[v] = u;
                if (dfs(v)) return true;
            }
        }
        color[u] = DFSColor::Black;
        return false;
    };

    for (NodeId id : g.insertion_order()) {
        if (color[id] == DFSColor::White) {
            if (dfs(id)) return cycle_path;
        }
    }
    return std::nullopt;
}

// Check if adding edges from 'new_deps' to 'node_id' would introduce a cycle.
// Uses a DFS from node_id following the NEW hypothetical dependent relationships.
[[nodiscard]] inline std::optional<std::vector<std::string>>
would_create_cycle(
    const GraphStorage& g,
    NodeId node_id,
    const std::vector<NodeId>& new_deps)
{
    // Build a view that includes the proposed edges temporarily.
    // We look for a path FROM any dep BACK TO node_id.
    std::unordered_set<NodeId> visited;
    std::vector<std::string> path;

    std::function<bool(NodeId)> dfs = [&](NodeId cur) -> bool {
        if (cur == node_id) {
            path.push_back(g.at(cur).name);
            return true;
        }
        if (visited.count(cur)) return false;
        visited.insert(cur);

        for (NodeId dep_of_cur : g.at(cur).dependents) {
            path.push_back(g.at(cur).name);
            if (dfs(dep_of_cur)) return true;
            path.pop_back();
        }
        return false;
    };

    // If node_id itself depends on any dep that can reach node_id, it's a cycle.
    for (NodeId dep : new_deps) {
        if (dep == node_id) {
            return std::vector<std::string>{g.at(node_id).name, g.at(node_id).name};
        }
        visited.clear(); path.clear();
        path.push_back(g.at(node_id).name);
        if (dfs(dep)) {
            return path;
        }
    }
    return std::nullopt;
}

// ── Affected subgraph traversal ──────────────────────────────────────────────
// Starting from a set of changed input nodes, compute the set of derived nodes
// that need to be re-evaluated. Returns them in topological order.
[[nodiscard]] inline std::vector<NodeId>
compute_affected_nodes(
    const GraphStorage& g,
    const std::vector<NodeId>& changed_inputs)
{
    std::unordered_set<NodeId> affected_set;
    std::vector<NodeId> work_queue;

    // Seed with direct dependents of changed inputs.
    for (NodeId inp : changed_inputs) {
        for (NodeId dep : g.at(inp).dependents) {
            if (!affected_set.count(dep)) {
                affected_set.insert(dep);
                work_queue.push_back(dep);
            }
        }
    }

    // BFS/DFS over transitive dependents.
    for (std::size_t i = 0; i < work_queue.size(); ++i) {
        NodeId cur = work_queue[i];
        for (NodeId dep : g.at(cur).dependents) {
            if (!affected_set.count(dep)) {
                affected_set.insert(dep);
                work_queue.push_back(dep);
            }
        }
    }

    // Sort by topo_order so we evaluate in dependency-respecting order.
    std::vector<NodeId> result(affected_set.begin(), affected_set.end());
    std::sort(result.begin(), result.end(), [&](NodeId a, NodeId b) {
        return g.at(a).topo_order < g.at(b).topo_order;
    });

    return result;
}

} // namespace incremental::detail
