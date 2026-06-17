#include "scheduler/service/workflow_service.h"

#include "scheduler/service/dag_validator.h"
#include "scheduler/engine/cron_parser.h"

#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace taskflow::scheduler::service {

WorkflowService::WorkflowService() = default;

common::result::Result<nlohmann::json> WorkflowService::createWorkflow(
    const std::string& name, const std::string& description,
    const nlohmann::json& dag_json,
    const std::string& schedule_strategy,
    const std::string& target_worker_id,
    const std::string& cron_expression,
    bool cron_enabled,
    const std::string& creator_id) {

    // Validate workflow name
    if (name.empty()) {
        return common::result::Result<nlohmann::json>::failure("Workflow name cannot be empty");
    }
    if (name.length() > 64) {
        return common::result::Result<nlohmann::json>::failure("Workflow name cannot exceed 64 characters");
    }

    // 1. Validate DAG structure
    auto dagResult = DagValidator::validate(dag_json);
    if (!dagResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(dagResult.error());
    }

    // 1b. Validate that all task_ids exist and are not deleted
    if (dag_json.contains("nodes") && dag_json["nodes"].is_array()) {
        for (const auto& node : dag_json["nodes"]) {
            if (node.contains("task_id") && node["task_id"].is_string()) {
                std::string tid = node["task_id"].get<std::string>();
                auto taskResult = task_dao_.findById(tid);
                if (!taskResult.ok()) {
                    return common::result::Result<nlohmann::json>::failure(
                        "Task not found for node '" + node.value("id", "") + "': " + taskResult.error());
                }
                if (taskResult.value().deleted) {
                    return common::result::Result<nlohmann::json>::failure(
                        "Task '" + taskResult.value().name + "' has been deleted and cannot be used in workflow");
                }
            }
        }
    }

    // 2. Validate schedule_strategy
    if (schedule_strategy != "random" && schedule_strategy != "load_balance" && schedule_strategy != "specified") {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid schedule_strategy: must be one of random, load_balance, specified");
    }

    // 3. If schedule_strategy == "specified", check target_worker_id exists
    if (schedule_strategy == "specified") {
        if (target_worker_id.empty()) {
            return common::result::Result<nlohmann::json>::failure(
                "target_worker_id is required when schedule_strategy is 'specified'");
        }
        auto workerResult = worker_dao_.findById(target_worker_id);
        if (!workerResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Target worker not found: " + workerResult.error());
        }
    }

    // 4. Create workflow via DAO
    auto createResult = workflow_dao_.create(
        name, description, dag_json, schedule_strategy,
        target_worker_id, cron_expression, cron_enabled, creator_id);
    if (!createResult.ok()) {
        std::string error = createResult.error();
        if (error.find("duplicate key") != std::string::npos) {
            return common::result::Result<nlohmann::json>::failure(
                "Workflow name '" + name + "' already exists");
        }
        return common::result::Result<nlohmann::json>::failure(
            "Failed to create workflow: " + error);
    }

    // 5. If cron_enabled && !cron_expression.empty(), validate and create CronJob
    if (cron_enabled && !cron_expression.empty()) {
        // Validate cron expression by computing next trigger time
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now{};
        gmtime_r(&now_time_t, &tm_now);
        std::ostringstream oss;
        oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
        auto cronValidateResult = engine::CronParser::getNextTrigger(cron_expression, oss.str());
        if (!cronValidateResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Invalid cron expression: " + cronValidateResult.error());
        }

        auto cronResult = cron_job_dao_.create(createResult.value(), cron_expression);
        if (!cronResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Failed to create cron job: " + cronResult.error());
        }
    }

    // Fetch the created workflow
    auto fetchResult = workflow_dao_.findById(createResult.value());
    if (!fetchResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to fetch created workflow: " + fetchResult.error());
    }

    return fetchResult.value().toJson();
}

common::result::Result<nlohmann::json> WorkflowService::getWorkflow(const std::string& id) {
    auto result = workflow_dao_.findById(id);
    if (!result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Workflow not found: " + result.error());
    }
    return result.value().toJson();
}

common::result::Result<nlohmann::json> WorkflowService::listWorkflows(
    int page, int page_size, const std::string& keyword, const std::string& creator_id) {

    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;

    int offset = (page - 1) * page_size;

    auto listResult = workflow_dao_.list(offset, page_size, keyword, creator_id);
    if (!listResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to list workflows: " + listResult.error());
    }

    const auto& workflows = listResult.value();
    nlohmann::json items = nlohmann::json::array();
    for (const auto& wf : workflows) {
        items.push_back(wf.toJson());
    }

    nlohmann::json response = {
        {"items", items},
        {"total", static_cast<int>(workflows.size())},
        {"page", page},
        {"page_size", page_size}
    };

    return response;
}

common::result::Result<nlohmann::json> WorkflowService::updateWorkflow(
    const std::string& id, const std::string& name, const std::string& description,
    const nlohmann::json& dag_json,
    const std::string& schedule_strategy,
    const std::string& target_worker_id,
    const std::string& cron_expression,
    bool cron_enabled,
    const std::string& user_id, const std::string& role) {

    // Find existing workflow to check ownership and get defaults
    auto existingWfResult = workflow_dao_.findById(id);
    if (!existingWfResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Workflow not found: " + existingWfResult.error());
    }

    const auto& existing_workflow = existingWfResult.value();
    if (role != "admin" && existing_workflow.creator_id != user_id) {
        return common::result::Result<nlohmann::json>::failure("权限不足，只能编辑自己创建的工作流");
    }

    // Use existing values for fields not provided
    std::string effective_name = name.empty() ? existing_workflow.name : name;
    std::string effective_strategy = schedule_strategy.empty() ? existing_workflow.schedule_strategy : schedule_strategy;
    nlohmann::json effective_dag = dag_json.is_null() ? existing_workflow.dag_json : dag_json;

    // Validate DAG structure
    auto dagResult = DagValidator::validate(effective_dag);
    if (!dagResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(dagResult.error());
    }

    // Validate that all task_ids exist and are not deleted
    if (effective_dag.contains("nodes") && effective_dag["nodes"].is_array()) {
        for (const auto& node : effective_dag["nodes"]) {
            if (node.contains("task_id") && node["task_id"].is_string()) {
                std::string tid = node["task_id"].get<std::string>();
                auto taskResult = task_dao_.findById(tid);
                if (!taskResult.ok()) {
                    return common::result::Result<nlohmann::json>::failure(
                        "Task not found for node '" + node.value("id", "") + "': " + taskResult.error());
                }
                if (taskResult.value().deleted) {
                    return common::result::Result<nlohmann::json>::failure(
                        "Task '" + taskResult.value().name + "' has been deleted and cannot be used in workflow");
                }
            }
        }
    }

    // Validate schedule_strategy
    if (effective_strategy != "random" && effective_strategy != "load_balance" && effective_strategy != "specified") {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid schedule_strategy: must be one of random, load_balance, specified");
    }

    // If schedule_strategy == "specified", check target_worker_id exists
    if (effective_strategy == "specified") {
        if (target_worker_id.empty()) {
            return common::result::Result<nlohmann::json>::failure(
                "target_worker_id is required when schedule_strategy is 'specified'");
        }
        auto workerResult = worker_dao_.findById(target_worker_id);
        if (!workerResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Target worker not found: " + workerResult.error());
        }
    }

    // Update workflow via DAO (version auto-increment is handled in DAO)
    auto updateResult = workflow_dao_.update(
        id, effective_name, description, effective_dag, effective_strategy,
        target_worker_id, cron_expression, cron_enabled);
    if (!updateResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to update workflow: " + updateResult.error());
    }

    // Update or create CronJob
    auto cronJobResult = cron_job_dao_.findByWorkflowId(id);
    if (cron_enabled && !cron_expression.empty()) {
        // Validate cron expression
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now{};
        gmtime_r(&now_time_t, &tm_now);
        std::ostringstream oss;
        oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
        auto cronValidateResult = engine::CronParser::getNextTrigger(cron_expression, oss.str());
        if (!cronValidateResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Invalid cron expression: " + cronValidateResult.error());
        }

        if (cronJobResult.ok()) {
            auto cronUpdateResult = cron_job_dao_.update(cronJobResult.value().id, cron_expression, true);
            if (!cronUpdateResult.ok()) {
                return common::result::Result<nlohmann::json>::failure(
                    "Failed to update cron job: " + cronUpdateResult.error());
            }
        } else {
            auto cronCreateResult = cron_job_dao_.create(id, cron_expression);
            if (!cronCreateResult.ok()) {
                return common::result::Result<nlohmann::json>::failure(
                    "Failed to create cron job: " + cronCreateResult.error());
            }
        }
    } else if (cronJobResult.ok()) {
        auto disableResult = cron_job_dao_.toggleEnabled(cronJobResult.value().id, false);
        if (!disableResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Failed to disable cron job: " + disableResult.error());
        }
    }

    // Fetch the updated workflow
    auto fetchResult = workflow_dao_.findById(id);
    if (!fetchResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to fetch updated workflow: " + fetchResult.error());
    }

    return fetchResult.value().toJson();
}

common::result::Result<void> WorkflowService::deleteWorkflow(
    const std::string& id, const std::string& user_id, const std::string& role) {

    // Find workflow to check ownership
    auto wfResult = workflow_dao_.findById(id);
    if (!wfResult.ok()) {
        return common::result::Result<void>::failure(
            "Workflow not found: " + wfResult.error());
    }

    const auto& workflow = wfResult.value();
    if (role != "admin" && workflow.creator_id != user_id) {
        return common::result::Result<void>::failure("权限不足，只能删除自己创建的工作流");
    }

    // Soft delete the workflow
    auto deleteResult = workflow_dao_.softDelete(id);
    if (!deleteResult.ok()) {
        return common::result::Result<void>::failure(
            "Failed to delete workflow: " + deleteResult.error());
    }

    // Disable associated CronJob
    auto cronJobResult = cron_job_dao_.findByWorkflowId(id);
    if (cronJobResult.ok()) {
        auto disableResult = cron_job_dao_.disableByWorkflowId(id);
        if (!disableResult.ok()) {
            return common::result::Result<void>::failure(
                "Failed to disable cron job: " + disableResult.error());
        }
    }

    return common::result::Result<void>();
}

common::result::Result<nlohmann::json> WorkflowService::triggerWorkflow(
    const std::string& workflow_id, const std::string& creator_id) {

    // 1. Find workflow by id
    auto wfResult = workflow_dao_.findById(workflow_id);
    if (!wfResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Workflow not found: " + wfResult.error());
    }
    const auto& workflow = wfResult.value();

    // 2. Create WorkflowInstance (status=PENDING, trigger_type="manual")
    auto instanceResult = workflow_instance_dao_.create(
        workflow_id, workflow.version, "manual", creator_id);
    if (!instanceResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to create workflow instance: " + instanceResult.error());
    }
    const auto& instance_id = instanceResult.value();

    // 3. Parse dag_json to get all nodes
    const auto& dag = workflow.dag_json;
    if (!dag.contains("nodes") || !dag["nodes"].is_array()) {
        return common::result::Result<nlohmann::json>::failure("Invalid DAG: missing nodes array");
    }

    // 4. For each node, get task info and create TaskInstance
    for (const auto& node : dag["nodes"]) {
        std::string task_id = node["task_id"].get<std::string>();

        auto taskResult = task_dao_.findById(task_id);
        if (!taskResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Task not found for node: " + taskResult.error());
        }
        const auto& task = taskResult.value();

        // 5. Create TaskInstance for each node
        std::string node_id = node.value("id", "");
        auto taskInstanceResult = task_instance_dao_.create(
            instance_id, task_id, task.version, task.name, node_id);
        if (!taskInstanceResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Failed to create task instance: " + taskInstanceResult.error());
        }
    }

    // 6. Return {"instance_id": id, "workflow_instance": instance.toJson()}
    auto fetchInstance = workflow_instance_dao_.findById(instance_id);
    if (!fetchInstance.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to fetch workflow instance: " + fetchInstance.error());
    }

    nlohmann::json response = {
        {"instance_id", instance_id},
        {"workflow_instance", fetchInstance.value().toJson()}
    };

    return response;
}

}  // namespace taskflow::scheduler::service
