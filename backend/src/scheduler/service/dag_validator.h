#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/result/result.h"

namespace taskflow::scheduler::service {

class DagValidator {
public:
    DagValidator() = delete;

    // Validate a DAG JSON structure
    // Expected format: {"nodes": [{"id": "n1", "task_id": "t1", "dependencies": []}, ...], "edges": [{"source": "n1", "target": "n2"}, ...]}
    // Dependencies can be specified either via "edges" array or "dependencies" field on each node
    // Validations:
    // 1. Must have "nodes" array with at least 1 node
    // 2. Each node must have "id" and "task_id"
    // 3. All edge source/target must reference existing node IDs
    // 4. No cycles (DFS topological sort)
    // 5. No isolated nodes (every node must have at least one edge, unless there's only 1 node)
    static common::result::Result<void> validate(const nlohmann::json& dag_json) {
        // 1. Must have "nodes" array with at least 1 node
        if (!dag_json.contains("nodes") || !dag_json["nodes"].is_array()) {
            return common::result::Result<void>::failure("DAG must have a 'nodes' array");
        }
        const auto& nodes = dag_json["nodes"];
        if (nodes.empty()) {
            return common::result::Result<void>::failure("DAG must have at least 1 node");
        }

        // 2. Each node must have "id" and "task_id"
        std::unordered_set<std::string> node_ids;
        for (const auto& node : nodes) {
            if (!node.contains("id") || !node["id"].is_string()) {
                return common::result::Result<void>::failure("Each node must have a string 'id' field");
            }
            if (!node.contains("task_id") || !node["task_id"].is_string()) {
                return common::result::Result<void>::failure("Each node must have a string 'task_id' field");
            }
            node_ids.insert(node["id"].get<std::string>());
        }

        // Build adjacency list from edges
        std::unordered_map<std::string, std::vector<std::string>> adj;
        std::unordered_set<std::string> connected_nodes;
        for (const auto& id : node_ids) {
            adj[id] = {};
        }

        if (dag_json.contains("edges") && dag_json["edges"].is_array()) {
            const auto& edges = dag_json["edges"];
            for (const auto& edge : edges) {
                if (!edge.contains("source") || !edge["source"].is_string() ||
                    !edge.contains("target") || !edge["target"].is_string()) {
                    return common::result::Result<void>::failure("Each edge must have 'source' and 'target' string fields");
                }
                std::string source = edge["source"].get<std::string>();
                std::string target = edge["target"].get<std::string>();

                // 3. All edge source/target must reference existing node IDs
                if (node_ids.find(source) == node_ids.end()) {
                    return common::result::Result<void>::failure("Edge source '" + source + "' does not reference an existing node ID");
                }
                if (node_ids.find(target) == node_ids.end()) {
                    return common::result::Result<void>::failure("Edge target '" + target + "' does not reference an existing node ID");
                }

                adj[source].push_back(target);
                connected_nodes.insert(source);
                connected_nodes.insert(target);
            }
        }

        // Also support "dependencies" field on nodes
        for (const auto& node : nodes) {
            if (node.contains("dependencies") && node["dependencies"].is_array()) {
                std::string node_id = node["id"].get<std::string>();
                for (const auto& dep : node["dependencies"]) {
                    if (!dep.is_string()) continue;
                    std::string dep_id = dep.get<std::string>();
                    if (node_ids.find(dep_id) == node_ids.end()) {
                        return common::result::Result<void>::failure(
                            "Dependency '" + dep_id + "' in node '" + node_id +
                            "' does not reference an existing node ID");
                    }
                    adj[dep_id].push_back(node_id);
                    connected_nodes.insert(dep_id);
                    connected_nodes.insert(node_id);
                }
            }
        }

        // 4. No cycles (DFS cycle detection using 3-color marking)
        // WHITE=0, GRAY=1, BLACK=2
        std::unordered_map<std::string, int> color;
        for (const auto& id : node_ids) {
            color[id] = 0;
        }

        for (const auto& id : node_ids) {
            if (color[id] == 0) {
                if (hasCycle(id, adj, color)) {
                    return common::result::Result<void>::failure("DAG contains a cycle");
                }
            }
        }

        // 5. No isolated nodes: a node is isolated if it has no edges AND other nodes DO have edges
        //    (purely parallel DAGs where no nodes have edges are valid)
        if (!connected_nodes.empty()) {
            for (const auto& id : node_ids) {
                if (connected_nodes.find(id) == connected_nodes.end()) {
                    return common::result::Result<void>::failure(
                        "Node '" + id + "' is isolated (has no edges connecting it to other nodes)");
                }
            }
        }

        return common::result::Result<void>();
    }

private:
    static bool hasCycle(const std::string& node,
                         const std::unordered_map<std::string, std::vector<std::string>>& adj,
                         std::unordered_map<std::string, int>& color) {
        color[node] = 1;  // GRAY
        auto it = adj.find(node);
        if (it != adj.end()) {
            for (const auto& neighbor : it->second) {
                if (color[neighbor] == 1) {  // Back edge -> cycle
                    return true;
                }
                if (color[neighbor] == 0 && hasCycle(neighbor, adj, color)) {
                    return true;
                }
            }
        }
        color[node] = 2;  // BLACK
        return false;
    }
};

}  // namespace taskflow::scheduler::service
