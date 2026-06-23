#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "common/config/scheduler_config.h"
#include "common/result/result.h"
#include "scheduler/dao/workflow_instance_dao.h"
#include "scheduler/dao/task_instance_dao.h"
#include "scheduler/dao/worker_dao.h"
#include "scheduler/dao/workflow_dao.h"

namespace taskflow::scheduler::service {

class InstanceService {
public:
    // Fix #126: Accept TLS config for scheduler→worker gRPC calls.
    explicit InstanceService(common::config::TlsConfig worker_tls = {});

    // Pause workflow instance
    // Fix #134: resource-level permission check (creator_id)
    common::result::Result<void> pauseInstance(const std::string& id,
                                               const std::string& user_id = "",
                                               const std::string& role = "");

    // Resume workflow instance
    common::result::Result<void> resumeInstance(const std::string& id,
                                                const std::string& user_id = "",
                                                const std::string& role = "");

    // Cancel workflow instance and all pending/running tasks
    common::result::Result<void> cancelInstance(const std::string& id,
                                                const std::string& user_id = "",
                                                const std::string& role = "");

    // Retry a specific task instance (reset to PENDING, increment retry_count, recursively reset downstream)
    common::result::Result<void> retryTask(const std::string& instance_id,
                                           const std::string& task_instance_id,
                                           const std::string& user_id = "",
                                           const std::string& role = "");

    // Kill a running task
    common::result::Result<void> killTask(const std::string& instance_id,
                                          const std::string& task_instance_id,
                                          const std::string& user_id = "",
                                          const std::string& role = "");

    // Get instance details with all task instances
    common::result::Result<nlohmann::json> getInstance(const std::string& id,
                                                       const std::string& user_id = "",
                                                       const std::string& role = "");

    // List workflow instances with pagination
    common::result::Result<nlohmann::json> listInstances(
        const std::string& workflow_id, int page, int page_size,
        const std::string& user_id = "", const std::string& role = "");

    // List all workflow instances with pagination
    common::result::Result<nlohmann::json> listAllInstances(int page, int page_size,
                                                            const std::string& user_id = "",
                                                            const std::string& role = "");

    // Fix #225: List instances that contain a specific task (for TaskDetailView
    // execution history). Server-side JOIN + pagination replaces the broken
    // client-side filtering.
    common::result::Result<nlohmann::json> listInstancesByTaskId(
        const std::string& task_id, int page, int page_size,
        const std::string& user_id = "", const std::string& role = "");

    // Get task log content (read from Worker via gRPC)
    // Fix #318: follow parameter enables real-time log streaming
    common::result::Result<std::string> getTaskLog(
        const std::string& instance_id, const std::string& task_instance_id,
        const std::string& user_id = "", const std::string& role = "",
        bool follow = false);

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
    // Fix #126: TLS config for scheduler→worker gRPC channels.
    common::config::TlsConfig worker_tls_;

    // Recursively reset downstream task instances to PENDING based on DAG edges.
    // Fix #113: traverse DAG from the given node_id to find all downstream nodes.
    void resetDownstreamTasks(const nlohmann::json& dag_json,
                              const std::string& start_node_id,
                              const std::map<std::string, common::models::TaskInstance>& node_to_instance);

    // Fix #134: Check that the user has access to the given workflow instance.
    // Admins bypass the check; non-admin users must own the workflow that
    // created the instance. Empty user_id skips the check (internal calls).
    common::result::Result<void> checkInstanceAccess(
        const std::string& instance_id, const std::string& user_id, const std::string& role);

    // Fix #150/#151: Send CancelTask gRPC to the worker running a task.
    // Used by cancelInstance, killTask, and resetDownstreamTasks to stop
    // RUNNING/DISPATCHED tasks before resetting their state.
    void sendCancelTask(const std::string& task_instance_id, const std::string& worker_id);
};

}  // namespace taskflow::scheduler::service
