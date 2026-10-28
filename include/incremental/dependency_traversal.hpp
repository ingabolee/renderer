// include/incremental/dependency_traversal.hpp
// Public helpers for traversing the dependency graph.
#pragma once

#include <incremental/detail/graph_storage.hpp>
#include <vector>
#include <unordered_set>
#include <functional>

namespace incremental {

/// Visit all nodes that transitively depend ON the given root node.
/// Visits in BFS order (breadth-first), not necessarily topological.
/// callback: called with each dependent NodeId.
inline void visit_dependents(
    const detail::GraphStorage& g,
    detail::NodeId root,
    std::function<void(detail::NodeId)> callback)
{
    std::unordered_set<detail::NodeId> visited;
    std::vector<detail::NodeId> queue{root};
    visited.insert(root);
    while (!queue.empty()) {
        detail::NodeId cur = queue.back(); queue.pop_back();
        for (detail::NodeId dep : g.at(cur).dependents) {
            if (!visited.count(dep)) {
                visited.insert(dep);
                callback(dep);
                queue.push_back(dep);
            }
        }
    }
}

/// Visit all nodes that `leaf` transitively depends ON (its ancestors).
inline void visit_dependencies(
    const detail::GraphStorage& g,
    detail::NodeId leaf,
    std::function<void(detail::NodeId)> callback)
{
    std::unordered_set<detail::NodeId> visited;
    std::vector<detail::NodeId> queue;
    for (detail::NodeId dep : g.at(leaf).dependencies) {
        if (!visited.count(dep)) {
            visited.insert(dep);
            callback(dep);
            queue.push_back(dep);
        }
    }
    while (!queue.empty()) {
        detail::NodeId cur = queue.back(); queue.pop_back();
        for (detail::NodeId dep : g.at(cur).dependencies) {
            if (!visited.count(dep)) {
                visited.insert(dep);
                callback(dep);
                queue.push_back(dep);
            }
        }
    }
}

} // namespace incremental
