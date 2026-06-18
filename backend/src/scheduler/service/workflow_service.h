#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "scheduler/dao/workflow_dao.h"
#include "scheduler/dao/workflow_instance_dao.h"
#include "scheduler/dao/task_instance_dao.h"
#include "scheduler/dao/task_dao.h"
#include "scheduler/dao/cron_job_dao.h"
#include "scheduler/dao/worker_dao.h"

namespace taskflow::scheduler::service {

class WorkflowService {
public:
    WorkflowService();

    // Create workflow: validate DAG, validate schedule_strategy, create CronJob if needed
    common::result::Result<nlohmann::json> createWorkflow(
        const std::string& name, const std::string& description,
        const nlohmann::json& dag_json,
        const std::string& schedule_strategy,
        const std::string& target_worker_id,
        const std::string& cron_expression,
        bool cron_enabled,
        const std::string& creator_id);

    // Get workflow by ID
    common::result::Result<nlohmann::json> getWorkflow(const std::string& id);

    // List workflows with pagination
    common::result::Result<nlohmann::json> listWorkflows(
        int page, int page_size, const std::string& keyword, const std::string& creator_id);

    // Update workflow (version auto-increment)
    common::result::Result<nlohmann::json> updateWorkflow(
        const std::string& id, const std::string& name, const std::string& description,
        const nlohmann::json& dag_json,
        const std::string& schedule_strategy,
        const std::string& target_worker_id,
        const std::string& cron_expression,
        std::optional<bool> cron_enabled,
        const std::string& user_id, const std::string& role);

    // Delete workflow (soft delete, disable CronJob)
    common::result::Result<void> deleteWorkflow(
        const std::string& id, const std::string& user_id, const std::string& role);

    // Trigger workflow execution: create WorkflowInstance + TaskInstances
    common::result::Result<nlohmann::json> triggerWorkflow(
        const std::string& workflow_id, const std::string& creator_id,
        const nlohmann::json& param_overrides = nlohmann::json::object());

private:
    dao::WorkflowDao workflow_dao_;
    dao::WorkflowInstanceDao workflow_instance_dao_;
    dao::TaskInstanceDao task_instance_dao_;
    dao::TaskDao task_dao_;
    dao::CronJobDao cron_job_dao_;
    dao::WorkerDao worker_dao_;
};

}  // namespace taskflow::scheduler::service
