#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "scheduler/dao/workflow_instance_dao.h"
#include "scheduler/dao/task_instance_dao.h"
#include "scheduler/dao/worker_dao.h"
#include "scheduler/dao/workflow_dao.h"

namespace taskflow::scheduler::service {

class InstanceService {
public:
    InstanceService();

    // Pause workflow instance
    common::result::Result<void> pauseInstance(const std::string& id);

    // Resume workflow instance
    common::result::Result<void> resumeInstance(const std::string& id);

    // Cancel workflow instance and all pending/running tasks
    common::result::Result<void> cancelInstance(const std::string& id);

    // Retry a specific task instance (reset to PENDING, increment retry_count, recursively reset downstream)
    common::result::Result<void> retryTask(const std::string& instance_id, const std::string& task_instance_id);

    // Kill a running task
    common::result::Result<void> killTask(const std::string& instance_id, const std::string& task_instance_id);

    // Get instance details with all task instances
    common::result::Result<nlohmann::json> getInstance(const std::string& id);

    // List workflow instances with pagination
    common::result::Result<nlohmann::json> listInstances(
        const std::string& workflow_id, int page, int page_size);

    // List all workflow instances with pagination
    common::result::Result<nlohmann::json> listAllInstances(int page, int page_size);

    // Get task log content (read from Worker via gRPC)
    common::result::Result<std::string> getTaskLog(
        const std::string& instance_id, const std::string& task_instance_id);

    // Validate that a task instance belongs to the given workflow instance
    common::result::Result<void> validateTaskInstance(
        const std::string& instance_id, const std::string& task_instance_id);

    // Get the worker address for a task instance
    common::result::Result<std::string> getTaskWorkerAddress(
        const std::string& instance_id, const std::string& task_instance_id);

private:
    dao::WorkflowInstanceDao workflow_instance_dao_;
    dao::TaskInstanceDao task_instance_dao_;
    dao::WorkerDao worker_dao_;
    dao::WorkflowDao workflow_dao_;

    // Recursively reset downstream task instances to PENDING based on DAG edges.
    // Fix #113: traverse DAG from the given node_id to find all downstream nodes.
    void resetDownstreamTasks(const nlohmann::json& dag_json,
                              const std::string& start_node_id,
                              const std::map<std::string, common::models::TaskInstance>& node_to_instance);
};

}  // namespace taskflow::scheduler::service
