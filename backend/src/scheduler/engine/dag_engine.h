#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/result/result.h"

namespace taskflow::scheduler::engine {

class DagEngine {
public:
    // Parse dag_json and perform topological sort using Kahn's algorithm
    // Returns ordered list of layers (each layer is a set of node IDs that can run in parallel)
    static common::result::Result<std::vector<std::vector<std::string>>> topologicalSort(
        const nlohmann::json& dag_json);

    // Find tasks that are ready to execute (all upstream tasks succeeded)
    // Returns set of node IDs ready for dispatch
    static std::set<std::string> findReadyTasks(
        const nlohmann::json& dag_json,
        const std::map<std::string, std::string>& task_statuses);

    // Find tasks whose upstream has failed, mark them as UPSTREAM_FAILED
    static std::set<std::string> findBlockedTasks(
        const nlohmann::json& dag_json,
        const std::map<std::string, std::string>& task_statuses);

    // Check if all tasks in the DAG are in a terminal state
    static bool allTasksFinished(
        const std::map<std::string, std::string>& task_statuses);
};

}  // namespace taskflow::scheduler::engine
