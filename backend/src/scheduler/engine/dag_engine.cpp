#include "scheduler/engine/dag_engine.h"

#include <map>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/result/result.h"

namespace taskflow::scheduler::engine {

namespace {

const std::unordered_set<std::string> kTerminalStatuses = {
    "SUCCESS", "FAILED", "UPSTREAM_FAILED", "TIMEOUT", "CANCELLED", "NODE_OFFLINE"
};

const std::unordered_set<std::string> kFailedStatuses = {
    "FAILED", "UPSTREAM_FAILED", "TIMEOUT", "CANCELLED", "NODE_OFFLINE"
};

}  // namespace

common::result::Result<std::vector<std::vector<std::string>>> DagEngine::topologicalSort(
    const nlohmann::json& dag_json) {
    if (!dag_json.contains("nodes") || !dag_json["nodes"].is_array()) {
        return common::result::Result<std::vector<std::vector<std::string>>>::failure(
            "DAG must have a 'nodes' array");
    }

    const auto& nodes = dag_json["nodes"];
    if (nodes.empty()) {
        return common::result::Result<std::vector<std::vector<std::string>>>::failure(
            "DAG must have at least 1 node");
    }

    // Collect all node IDs and build adjacency list
    std::unordered_map<std::string, std::vector<std::string>> adj;
    std::unordered_map<std::string, int> in_degree;

    for (const auto& node : nodes) {
        std::string id = node["id"].get<std::string>();
        adj[id] = {};
        in_degree[id] = 0;
    }

    if (dag_json.contains("edges") && dag_json["edges"].is_array()) {
        for (const auto& edge : dag_json["edges"]) {
            std::string source = edge["source"].get<std::string>();
            std::string target = edge["target"].get<std::string>();
            adj[source].push_back(target);
            in_degree[target]++;
        }
    }

    // Kahn's algorithm - process layer by layer
    std::queue<std::string> queue;
    for (const auto& [id, deg] : in_degree) {
        if (deg == 0) {
            queue.push(id);
        }
    }

    std::vector<std::vector<std::string>> layers;
    int processed = 0;

    while (!queue.empty()) {
        // Process all nodes at the current level
        std::vector<std::string> layer;
        size_t layer_size = queue.size();
        for (size_t i = 0; i < layer_size; ++i) {
            std::string node = std::move(queue.front());
            queue.pop();
            layer.push_back(node);
            processed++;
        }

        layers.push_back(std::move(layer));

        // Decrement in-degree for neighbors and enqueue if ready
        for (const auto& node : layers.back()) {
            for (const auto& neighbor : adj[node]) {
                in_degree[neighbor]--;
                if (in_degree[neighbor] == 0) {
                    queue.push(neighbor);
                }
            }
        }
    }

    if (static_cast<int>(in_degree.size()) != processed) {
        return common::result::Result<std::vector<std::vector<std::string>>>::failure(
            "DAG contains a cycle");
    }

    return layers;
}

std::set<std::string> DagEngine::findReadyTasks(
    const nlohmann::json& dag_json,
    const std::map<std::string, std::string>& task_statuses) {
    std::set<std::string> ready;

    if (!dag_json.contains("nodes") || !dag_json["nodes"].is_array()) {
        return ready;
    }

    // Build reverse adjacency: for each node, who are its upstream nodes
    std::map<std::string, std::vector<std::string>> upstream;
    for (const auto& node : dag_json["nodes"]) {
        std::string id = node["id"].get<std::string>();
        upstream[id] = {};
    }

    if (dag_json.contains("edges") && dag_json["edges"].is_array()) {
        for (const auto& edge : dag_json["edges"]) {
            std::string source = edge["source"].get<std::string>();
            std::string target = edge["target"].get<std::string>();
            upstream[target].push_back(source);
        }
    }

    for (const auto& node : dag_json["nodes"]) {
        std::string id = node["id"].get<std::string>();

        auto status_it = task_statuses.find(id);
        if (status_it == task_statuses.end() || status_it->second != "PENDING") {
            continue;
        }

        bool all_upstream_success = true;
        for (const auto& up_id : upstream[id]) {
            auto up_it = task_statuses.find(up_id);
            if (up_it == task_statuses.end() || up_it->second != "SUCCESS") {
                all_upstream_success = false;
                break;
            }
        }

        if (all_upstream_success) {
            ready.insert(id);
        }
    }

    return ready;
}

std::set<std::string> DagEngine::findBlockedTasks(
    const nlohmann::json& dag_json,
    const std::map<std::string, std::string>& task_statuses) {
    std::set<std::string> blocked;

    if (!dag_json.contains("nodes") || !dag_json["nodes"].is_array()) {
        return blocked;
    }

    // Build reverse adjacency
    std::map<std::string, std::vector<std::string>> upstream;
    for (const auto& node : dag_json["nodes"]) {
        std::string id = node["id"].get<std::string>();
        upstream[id] = {};
    }

    if (dag_json.contains("edges") && dag_json["edges"].is_array()) {
        for (const auto& edge : dag_json["edges"]) {
            std::string source = edge["source"].get<std::string>();
            std::string target = edge["target"].get<std::string>();
            upstream[target].push_back(source);
        }
    }

    for (const auto& node : dag_json["nodes"]) {
        std::string id = node["id"].get<std::string>();

        auto status_it = task_statuses.find(id);
        if (status_it == task_statuses.end() || status_it->second != "PENDING") {
            continue;
        }

        for (const auto& up_id : upstream[id]) {
            auto up_it = task_statuses.find(up_id);
            if (up_it != task_statuses.end() &&
                kFailedStatuses.count(up_it->second) > 0) {
                blocked.insert(id);
                break;
            }
        }
    }

    return blocked;
}

bool DagEngine::allTasksFinished(
    const std::map<std::string, std::string>& task_statuses) {
    for (const auto& [id, status] : task_statuses) {
        if (kTerminalStatuses.count(status) == 0) {
            return false;
        }
    }
    return true;
}

}  // namespace taskflow::scheduler::engine
