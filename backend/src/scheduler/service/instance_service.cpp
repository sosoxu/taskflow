#include "scheduler/service/instance_service.h"

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "common/models/task_instance.h"
#include "common/models/worker_info.h"
#include "common/models/workflow_instance.h"
#include "common/result/result.h"
#include "scheduler/engine/dag_engine.h"
#include "taskflow.grpc.pb.h"

namespace taskflow::scheduler::service {

InstanceService::InstanceService() = default;

common::result::Result<void> InstanceService::pauseInstance(const std::string& id) {
    auto instance_result = workflow_instance_dao_.findById(id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    const auto& instance = instance_result.value();
    if (instance.status != "RUNNING") {
        return common::result::Result<void>::failure(
            "Cannot pause instance with status: " + instance.status);
    }

    auto update_result = workflow_instance_dao_.updateStatus(id, "PAUSED");
    if (!update_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to pause instance: " + update_result.error());
    }

    return common::result::Result<void>();
}

common::result::Result<void> InstanceService::resumeInstance(const std::string& id) {
    auto instance_result = workflow_instance_dao_.findById(id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    const auto& instance = instance_result.value();
    if (instance.status != "PAUSED") {
        return common::result::Result<void>::failure(
            "Cannot resume instance with status: " + instance.status);
    }

    auto update_result = workflow_instance_dao_.updateStatus(id, "RUNNING");
    if (!update_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to resume instance: " + update_result.error());
    }

    return common::result::Result<void>();
}

common::result::Result<void> InstanceService::cancelInstance(const std::string& id) {
    auto instance_result = workflow_instance_dao_.findById(id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    const auto& instance = instance_result.value();
    if (instance.status == "SUCCESS" || instance.status == "FAILED" || instance.status == "CANCELLED") {
        return common::result::Result<void>::failure(
            "Cannot cancel instance with status: " + instance.status);
    }

    // Update workflow instance status to CANCELLED
    auto update_result = workflow_instance_dao_.updateStatus(id, "CANCELLED");
    if (!update_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to cancel instance: " + update_result.error());
    }

    // Cancel all PENDING and DISPATCHED task instances
    auto tasks_result = task_instance_dao_.listByWorkflowInstance(id);
    if (!tasks_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to list task instances: " + tasks_result.error());
    }

    for (const auto& task_instance : tasks_result.value()) {
        if (task_instance.status == "PENDING" || task_instance.status == "DISPATCHED") {
            auto task_update = task_instance_dao_.updateStatus(task_instance.id, "CANCELLED");
            if (!task_update.ok()) {
                spdlog::warn("InstanceService: failed to cancel task instance {}: {}",
                             task_instance.id, task_update.error());
            }
        } else if (task_instance.status == "RUNNING") {
            // Send CancelTask gRPC to the worker to kill the running process
            if (!task_instance.worker_id.empty()) {
                auto worker_result = worker_dao_.findById(task_instance.worker_id);
                if (worker_result.ok()) {
                    const auto& worker = worker_result.value();
                    auto channel = grpc::CreateChannel(worker.address, grpc::InsecureChannelCredentials());
                    auto stub = taskflow::v1::WorkerService::NewStub(channel);

                    taskflow::v1::TaskCancelRequest request;
                    request.set_task_instance_id(task_instance.id);

                    taskflow::v1::TaskCancelResponse response;
                    grpc::ClientContext context;
                    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
                    auto grpc_status = stub->CancelTask(&context, request, &response);

                    if (!grpc_status.ok()) {
                        spdlog::warn("InstanceService: gRPC CancelTask failed for task instance {}: {}",
                                     task_instance.id, grpc_status.error_message());
                    }
                }
            }
            auto task_update = task_instance_dao_.markFinished(
                task_instance.id, "CANCELLED", -1, "Cancelled by user");
            if (!task_update.ok()) {
                spdlog::warn("InstanceService: failed to cancel running task instance {}: {}",
                             task_instance.id, task_update.error());
            }
        }
    }

    return common::result::Result<void>();
}

common::result::Result<void> InstanceService::retryTask(
    const std::string& instance_id, const std::string& task_instance_id) {

    // Verify the workflow instance exists
    auto instance_result = workflow_instance_dao_.findById(instance_id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    // Find the task instance
    auto task_result = task_instance_dao_.findById(task_instance_id);
    if (!task_result.ok()) {
        return common::result::Result<void>::failure(
            "Task instance not found: " + task_result.error());
    }

    const auto& task_instance = task_result.value();
    if (task_instance.workflow_instance_id != instance_id) {
        return common::result::Result<void>::failure(
            "Task instance does not belong to the specified workflow instance");
    }

    // Only allow retry for failed/timed-out tasks
    if (task_instance.status != "FAILED" && task_instance.status != "TIMEOUT"
        && task_instance.status != "UPSTREAM_FAILED") {
        return common::result::Result<void>::failure(
            "Cannot retry task instance with status: " + task_instance.status
            + ". Only FAILED, TIMEOUT, or UPSTREAM_FAILED tasks can be retried");
    }

    // Reset the task instance to PENDING and increment retry_count
    auto reset_result = task_instance_dao_.resetForRetry(task_instance_id);
    if (!reset_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to reset task instance: " + reset_result.error());
    }

    // Find and reset downstream tasks recursively
    // Get all task instances for this workflow instance
    auto all_tasks_result = task_instance_dao_.listByWorkflowInstance(instance_id);
    if (!all_tasks_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to list task instances: " + all_tasks_result.error());
    }

    // Build task_id -> task_instance mapping
    std::map<std::string, common::models::TaskInstance> task_map;
    for (const auto& ti : all_tasks_result.value()) {
        task_map[ti.task_id] = ti;
    }

    // Get the workflow to access dag_json for downstream traversal
    const auto& instance = instance_result.value();
    // We need the workflow's dag_json to find downstream tasks
    // Use the workflow_instance's workflow_id to get the workflow
    // But we don't have WorkflowDao here. Instead, we can use the DAG engine
    // to find downstream tasks if we had the dag_json.
    // Since we don't have WorkflowDao as a dependency, we'll use a simpler approach:
    // Find downstream tasks by traversing the edges in the dag_json.
    // We need to get the workflow - let's get it through the instance's workflow_id.
    // However, WorkflowDao is not a member. Let's add a different approach:
    // We'll reset downstream tasks by finding all tasks that are in UPSTREAM_FAILED,
    // FAILED, or CANCELLED state that come after this task.

    // For now, we reset all downstream tasks that are in a terminal non-success state.
    // We need the dag_json from the workflow. Since we don't have WorkflowDao,
    // we'll need to get the workflow instance's associated workflow.
    // The simplest approach: get all task instances and reset those that are
    // downstream based on the DAG structure.

    // Since we don't have direct access to WorkflowDao, we'll reset all
    // task instances in the workflow that are in UPSTREAM_FAILED or CANCELLED state
    // (which are likely downstream of the retried task).
    // A more precise approach would require the dag_json.

    // Reset downstream tasks: any task in UPSTREAM_FAILED state should be reset to PENDING
    for (const auto& ti : all_tasks_result.value()) {
        if (ti.status == "UPSTREAM_FAILED" || ti.status == "CANCELLED") {
            auto downstream_reset = task_instance_dao_.resetForRetry(ti.id);
            if (!downstream_reset.ok()) {
                spdlog::warn("InstanceService: failed to reset downstream task instance {}: {}",
                             ti.id, downstream_reset.error());
            }
        }
    }

    // If the workflow instance was in a terminal state, reset it to RUNNING
    if (instance.status == "FAILED" || instance.status == "CANCELLED" || instance.status == "PAUSED") {
        auto instance_update = workflow_instance_dao_.updateStatus(instance_id, "RUNNING");
        if (!instance_update.ok()) {
            spdlog::warn("InstanceService: failed to reset workflow instance status: {}",
                         instance_update.error());
        }
    }

    return common::result::Result<void>();
}

common::result::Result<void> InstanceService::killTask(
    const std::string& instance_id, const std::string& task_instance_id) {

    // Verify the workflow instance exists
    auto instance_result = workflow_instance_dao_.findById(instance_id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    // Find the task instance
    auto task_result = task_instance_dao_.findById(task_instance_id);
    if (!task_result.ok()) {
        return common::result::Result<void>::failure(
            "Task instance not found: " + task_result.error());
    }

    const auto& task_instance = task_result.value();
    if (task_instance.workflow_instance_id != instance_id) {
        return common::result::Result<void>::failure(
            "Task instance does not belong to the specified workflow instance");
    }

    if (task_instance.status != "RUNNING" && task_instance.status != "DISPATCHED") {
        return common::result::Result<void>::failure(
            "Cannot kill task instance with status: " + task_instance.status);
    }

    // If the task has a worker assigned, send CancelTask gRPC to the worker
    if (!task_instance.worker_id.empty()) {
        auto worker_result = worker_dao_.findById(task_instance.worker_id);
        if (worker_result.ok()) {
            const auto& worker = worker_result.value();
            auto channel = grpc::CreateChannel(worker.address, grpc::InsecureChannelCredentials());
            auto stub = taskflow::v1::WorkerService::NewStub(channel);

            taskflow::v1::TaskCancelRequest request;
            request.set_task_instance_id(task_instance_id);

            taskflow::v1::TaskCancelResponse response;
            grpc::ClientContext context;
            context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
            auto grpc_status = stub->CancelTask(&context, request, &response);

            if (!grpc_status.ok()) {
                spdlog::warn("InstanceService: gRPC CancelTask failed for task instance {}: {}",
                             task_instance_id, grpc_status.error_message());
            }
        } else {
            spdlog::warn("InstanceService: worker not found for task instance {}: {}",
                         task_instance_id, worker_result.error());
        }
    }

    // Mark the task instance as FAILED
    auto update_result = task_instance_dao_.markFinished(
        task_instance_id, "FAILED", -1, "Killed by user");
    if (!update_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to mark task instance as FAILED: " + update_result.error());
    }

    return common::result::Result<void>();
}

common::result::Result<nlohmann::json> InstanceService::getInstance(const std::string& id) {
    auto instance_result = workflow_instance_dao_.findById(id);
    if (!instance_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    const auto& instance = instance_result.value();
    nlohmann::json result = instance.toJson();

    // Get all task instances
    auto tasks_result = task_instance_dao_.listByWorkflowInstance(id);
    if (!tasks_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to list task instances: " + tasks_result.error());
    }

    nlohmann::json task_list = nlohmann::json::array();
    for (const auto& task_instance : tasks_result.value()) {
        task_list.push_back(task_instance.toJson());
    }

    result["task_instances"] = task_list;

    return result;
}

common::result::Result<nlohmann::json> InstanceService::listInstances(
    const std::string& workflow_id, int page, int page_size) {

    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;

    int offset = (page - 1) * page_size;

    auto list_result = workflow_instance_dao_.listByWorkflow(workflow_id, offset, page_size);
    if (!list_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to list instances: " + list_result.error());
    }

    const auto& instances = list_result.value();
    nlohmann::json items = nlohmann::json::array();
    for (const auto& inst : instances) {
        items.push_back(inst.toJson());
    }

    // Get total count
    auto countResult = workflow_instance_dao_.countByWorkflow(workflow_id);
    int total = countResult.ok() ? countResult.value() : static_cast<int>(instances.size());

    nlohmann::json response = {
        {"items", items},
        {"total", total},
        {"page", page},
        {"page_size", page_size}
    };

    return response;
}

common::result::Result<nlohmann::json> InstanceService::listAllInstances(int page, int page_size) {
    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;

    int offset = (page - 1) * page_size;
    auto result = workflow_instance_dao_.listAll(offset, page_size);
    if (!result.ok()) {
        return common::result::Result<nlohmann::json>::failure(result.error());
    }

    auto countResult = workflow_instance_dao_.countAll();
    int total = countResult.ok() ? countResult.value() : 0;

    nlohmann::json instances = nlohmann::json::array();
    for (const auto& inst : result.value()) {
        instances.push_back(inst.toJson());
    }

    return nlohmann::json{
        {"items", instances},
        {"page", page},
        {"page_size", page_size},
        {"total", total}
    };
}

common::result::Result<std::string> InstanceService::getTaskLog(
    const std::string& instance_id, const std::string& task_instance_id) {

    // Verify the workflow instance exists
    auto instance_result = workflow_instance_dao_.findById(instance_id);
    if (!instance_result.ok()) {
        return common::result::Result<std::string>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    // Find the task instance
    auto task_result = task_instance_dao_.findById(task_instance_id);
    if (!task_result.ok()) {
        return common::result::Result<std::string>::failure(
            "Task instance not found: " + task_result.error());
    }

    const auto& task_instance = task_result.value();
    if (task_instance.workflow_instance_id != instance_id) {
        return common::result::Result<std::string>::failure(
            "Task instance does not belong to the specified workflow instance");
    }

    if (task_instance.worker_id.empty()) {
        return common::result::Result<std::string>::failure(
            "Task instance has not been dispatched to a worker");
    }

    // Get the worker info
    auto worker_result = worker_dao_.findById(task_instance.worker_id);
    if (!worker_result.ok()) {
        return common::result::Result<std::string>::failure(
            "Worker not found: " + worker_result.error());
    }

    const auto& worker = worker_result.value();

    // Connect to worker via gRPC and call GetTaskLog
    auto channel = grpc::CreateChannel(worker.address, grpc::InsecureChannelCredentials());
    auto stub = taskflow::v1::WorkerService::NewStub(channel);

    taskflow::v1::TaskLogRequest request;
    request.set_task_instance_id(task_instance_id);
    request.set_follow(false);

    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(300));
    auto reader = stub->GetTaskLog(&context, request);

    std::string log_content;
    taskflow::v1::LogChunk chunk;
    while (reader->Read(&chunk)) {
        log_content.append(chunk.data().begin(), chunk.data().end());
        if (chunk.eof()) {
            break;
        }
    }

    auto grpc_status = reader->Finish();
    if (!grpc_status.ok()) {
        return common::result::Result<std::string>::failure(
            "gRPC GetTaskLog failed: " + grpc_status.error_message());
    }

    return log_content;
}

common::result::Result<void> InstanceService::validateTaskInstance(
    const std::string& instance_id, const std::string& task_instance_id) {

    auto instance_result = workflow_instance_dao_.findById(instance_id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    auto task_result = task_instance_dao_.findById(task_instance_id);
    if (!task_result.ok()) {
        return common::result::Result<void>::failure(
            "Task instance not found: " + task_result.error());
    }

    const auto& task_instance = task_result.value();
    if (task_instance.workflow_instance_id != instance_id) {
        return common::result::Result<void>::failure(
            "Task instance does not belong to the specified workflow instance");
    }

    return common::result::Result<void>();
}

common::result::Result<std::string> InstanceService::getTaskWorkerAddress(
    const std::string& /*instance_id*/, const std::string& task_instance_id) {

    auto task_result = task_instance_dao_.findById(task_instance_id);
    if (!task_result.ok()) {
        return common::result::Result<std::string>::failure(
            "Task instance not found: " + task_result.error());
    }

    const auto& task_instance = task_result.value();
    if (task_instance.worker_id.empty()) {
        return common::result::Result<std::string>::failure(
            "Task instance has not been dispatched to a worker");
    }

    auto worker_result = worker_dao_.findById(task_instance.worker_id);
    if (!worker_result.ok()) {
        return common::result::Result<std::string>::failure(
            "Worker not found: " + worker_result.error());
    }

    return worker_result.value().address;
}

}  // namespace taskflow::scheduler::service
