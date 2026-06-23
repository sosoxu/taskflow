#pragma once
#include <string>
#include <vector>
#include "common/result/result.h"
#include "common/models/workflow_instance.h"

namespace taskflow::scheduler::dao {

class WorkflowInstanceDao {
public:
    common::result::Result<std::string> create(
        const std::string& workflow_id,
        int workflow_version,
        const std::string& trigger_type,
        const std::string& creator_id,
        const nlohmann::json& param_overrides = nlohmann::json::object(),
        const nlohmann::json& dag_snapshot = nlohmann::json());

    common::result::Result<common::models::WorkflowInstance> findById(const std::string& id);

    common::result::Result<void> updateStatus(const std::string& id,
                                               const std::string& status);

    common::result::Result<void> markRunning(const std::string& id);

    common::result::Result<void> markFinished(const std::string& id,
                                               const std::string& status);

    common::result::Result<std::vector<common::models::WorkflowInstance>> listByWorkflow(
        const std::string& workflow_id, int offset, int limit);

    common::result::Result<int> countByWorkflow(const std::string& workflow_id);

    // Fix #203: Count active (PENDING/RUNNING/PAUSED) instances for a workflow.
    // Used by deleteWorkflow to check for running instances without fetching
    // a limited page (which could miss instances beyond the page size).
    common::result::Result<int> countActiveByWorkflow(const std::string& workflow_id);

    common::result::Result<std::vector<common::models::WorkflowInstance>> listAll(int offset, int limit);

    common::result::Result<int> countAll();

    // Fix #157: SQL-level filtering by creator_id for correct pagination.
    common::result::Result<std::vector<common::models::WorkflowInstance>> listByCreator(
        const std::string& creator_id, int offset, int limit);

    common::result::Result<int> countByCreator(const std::string& creator_id);

    // Fix #225: List/count workflow instances that contain a specific task,
    // by JOINing task_instances. Used by TaskDetailView's execution history
    // to do server-side filtering and pagination instead of client-side
    // filtering on a single page (which was always empty because
    // listAllInstances doesn't return the tasks subarray).
    // Fix #225b: Optional creator_id for access control — non-admin users
    // only see their own instances, consistent with listAllInstances.
    common::result::Result<std::vector<common::models::WorkflowInstance>> listByTaskId(
        const std::string& task_id, int offset, int limit,
        const std::string& creator_id = "");
    common::result::Result<int> countByTaskId(
        const std::string& task_id,
        const std::string& creator_id = "");

    common::result::Result<std::vector<common::models::WorkflowInstance>> listActive();
};

}  // namespace taskflow::scheduler::dao
