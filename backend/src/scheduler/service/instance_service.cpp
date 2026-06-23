#include "scheduler/service/instance_service.h"

#include <chrono>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "common/models/task_instance.h"
#include "common/models/worker_info.h"
#include "common/models/workflow_instance.h"
#include "common/result/result.h"
#include "common/util/grpc_channel_util.h"
#include "scheduler/engine/dag_engine.h"
#include "taskflow.grpc.pb.h"

namespace taskflow::scheduler::service {

InstanceService::InstanceService(common::config::TlsConfig worker_tls)
    : worker_tls_(std::move(worker_tls)) {}

// Fix #134: Resource-level permission check. Admins bypass; non-admin users
// must own the workflow that created the instance. Empty user_id skips
// (internal calls from DagDriver/CronScheduler).
common::result::Result<void> InstanceService::checkInstanceAccess(
    const std::string& instance_id, const std::string& user_id, const std::string& role) {
    if (user_id.empty() || role == "admin") {
        return common::result::Result<void>();
    }
    auto instance_result = workflow_instance_dao_.findById(instance_id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }
    auto workflow_result = workflow_dao_.findById(instance_result.value().workflow_id);
    if (!workflow_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow not found: " + workflow_result.error());
    }
    if (workflow_result.value().creator_id != user_id) {
        return common::result::Result<void>::failure(
            "Permission denied: you do not own this workflow instance");
    }
    return common::result::Result<void>();
}

common::result::Result<void> InstanceService::pauseInstance(
    const std::string& id, const std::string& user_id, const std::string& role) {
    auto access_result = checkInstanceAccess(id, user_id, role);
    if (!access_result.ok()) {
        return access_result;
    }
    auto instance_result = workflow_instance_dao_.findById(id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    const auto& instance = instance_result.value();
    // 允许 PENDING 和 RUNNING 状态暂停（completed-features.md 7.1 状态转换
    // PENDING→PAUSED, RUNNING→PAUSED）
    if (instance.status != "RUNNING" && instance.status != "PENDING") {
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

common::result::Result<void> InstanceService::resumeInstance(
    const std::string& id, const std::string& user_id, const std::string& role) {
    auto access_result = checkInstanceAccess(id, user_id, role);
    if (!access_result.ok()) {
        return access_result;
    }
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

common::result::Result<void> InstanceService::cancelInstance(
    const std::string& id, const std::string& user_id, const std::string& role) {
    auto access_result = checkInstanceAccess(id, user_id, role);
    if (!access_result.ok()) {
        return access_result;
    }
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
        // Fix #219: Use cancelIfActive for ALL active tasks (PENDING/DISPATCHED/
        // RUNNING) instead of branching on the snapshot status. The old code
        // used updateStatus (no guard) for PENDING and markFinished for
        // DISPATCHED/RUNNING — but the snapshot could be stale: a PENDING task
        // might have been dispatched between the listByWorkflowInstance call
        // and here, causing updateStatus to overwrite DISPATCHED without
        // sending CancelTask, leaking the running_tasks counter.
        //
        // cancelIfActive atomically transitions any active state to CANCELLED.
        // For DISPATCHED/RUNNING tasks we also send CancelTask so the worker
        // stops execution.
        if (task_instance.status == "PENDING" ||
            task_instance.status == "DISPATCHED" ||
            task_instance.status == "RUNNING") {
            // Send CancelTask for DISPATCHED/RUNNING (task may be executing)
            if (task_instance.status == "DISPATCHED" ||
                task_instance.status == "RUNNING") {
                sendCancelTask(task_instance.id, task_instance.worker_id);
            }
            auto cancel_result = task_instance_dao_.cancelIfActive(task_instance.id);
            if (!cancel_result.ok()) {
                spdlog::warn("InstanceService: failed to cancel task instance {}: {}",
                             task_instance.id, cancel_result.error());
            }
        }
    }

    return common::result::Result<void>();
}

common::result::Result<void> InstanceService::retryTask(
    const std::string& instance_id, const std::string& task_instance_id,
    const std::string& user_id, const std::string& role) {

    auto access_result = checkInstanceAccess(instance_id, user_id, role);
    if (!access_result.ok()) {
        return access_result;
    }

    // Verify the workflow instance exists
    auto instance_result = workflow_instance_dao_.findById(instance_id);
    if (!instance_result.ok()) {
        return common::result::Result<void>::failure(
            "Workflow instance not found: " + instance_result.error());
    }

    const auto& instance = instance_result.value();
    // Fix #156: Only allow retry when the instance is in a terminal state
    // (FAILED/CANCELLED) or PAUSED. Retrying while RUNNING races with DagDriver
    // (dispatch overwrites PENDING, or resetForRetry overwrites DISPATCHED).
    if (instance.status == "RUNNING" || instance.status == "PENDING") {
        return common::result::Result<void>::failure(
            "Cannot retry task while instance is " + instance.status +
            ". Wait for the instance to finish or pause it first");
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

    // Find and reset downstream tasks recursively based on DAG structure.
    // Fix #113: Previously this reset ALL UPSTREAM_FAILED/CANCELLED tasks in the
    // workflow, which incorrectly reset unrelated branches. Now we traverse the
    // DAG edges to find only the true downstream nodes of the retried task.
    auto all_tasks_result = task_instance_dao_.listByWorkflowInstance(instance_id);
    if (!all_tasks_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to list task instances: " + all_tasks_result.error());
    }

    // Build node_id -> TaskInstance mapping
    std::map<std::string, common::models::TaskInstance> node_to_instance;
    for (const auto& ti : all_tasks_result.value()) {
        node_to_instance[ti.node_id] = ti;
    }

    // Fix #152: Use dag_snapshot from the instance (captured at trigger time)
    // instead of the live workflow's dag_json, so retry uses the same DAG
    // definition that was active when the instance was created.
    if (!instance.dag_snapshot.is_null() && instance.dag_snapshot.is_object()) {
        resetDownstreamTasks(instance.dag_snapshot, task_instance.node_id, node_to_instance);
    } else {
        // Fallback for instances created before dag_snapshot migration
        auto workflow_result = workflow_dao_.findById(instance.workflow_id);
        if (workflow_result.ok()) {
            const auto& dag_json = workflow_result.value().dag_json;
            resetDownstreamTasks(dag_json, task_instance.node_id, node_to_instance);
        } else {
            spdlog::warn("InstanceService: failed to get workflow {} for DAG traversal: {}",
                         instance.workflow_id, workflow_result.error());
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

void InstanceService::sendCancelTask(const std::string& task_instance_id,
                                     const std::string& worker_id) {
    // Fix #150/#151: Best-effort CancelTask gRPC to stop a running/dispatched task.
    if (worker_id.empty()) return;
    auto worker_result = worker_dao_.findById(worker_id);
    if (!worker_result.ok()) {
        spdlog::warn("InstanceService: worker {} not found for CancelTask (task {}): {}",
                     worker_id, task_instance_id, worker_result.error());
        return;
    }
    const auto& worker = worker_result.value();
    auto channel = common::util::createWorkerChannel(worker.address, worker_tls_);
    auto stub = taskflow::v1::WorkerService::NewStub(channel);

    taskflow::v1::TaskCancelRequest request;
    request.set_task_instance_id(task_instance_id);

    taskflow::v1::TaskCancelResponse response;
    auto grpc_status = common::util::retryWorkerRpc([&]() -> ::grpc::Status {
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        return stub->CancelTask(&ctx, request, &response);
    });

    if (!grpc_status.ok()) {
        spdlog::warn("InstanceService: gRPC CancelTask failed for task instance {}: {}",
                     task_instance_id, grpc_status.error_message());
    }
}

void InstanceService::resetDownstreamTasks(
    const nlohmann::json& dag_json,
    const std::string& start_node_id,
    const std::map<std::string, common::models::TaskInstance>& node_to_instance) {
    // BFS traversal of downstream nodes via DAG edges
    std::set<std::string> visited;
    std::queue<std::string> queue;
    queue.push(start_node_id);
    visited.insert(start_node_id);

    while (!queue.empty()) {
        std::string current = queue.front();
        queue.pop();

        if (!dag_json.contains("edges") || !dag_json["edges"].is_array()) {
            break;
        }

        for (const auto& edge : dag_json["edges"]) {
            if (!edge.contains("source") || !edge.contains("target")) continue;
            if (edge["source"].get<std::string>() != current) continue;

            std::string target = edge["target"].get<std::string>();
            if (visited.count(target)) continue;
            visited.insert(target);

            // Reset this downstream task instance if it exists. All downstream
            // tasks (including SUCCESS) must be reset to PENDING because the
            // retried upstream task may produce different output
            // (completed-features.md 7.4: 下游任务也随之重置为 PENDING 并重新执行)
            auto it = node_to_instance.find(target);
            if (it != node_to_instance.end()) {
                const auto& ti = it->second;
                if (ti.status == "UPSTREAM_FAILED" || ti.status == "CANCELLED" ||
                    ti.status == "FAILED" || ti.status == "TIMEOUT" ||
                    ti.status == "SUCCESS" || ti.status == "DISPATCHED" ||
                    ti.status == "RUNNING" || ti.status == "PENDING" ||
                    ti.status == "NODE_OFFLINE") {
                    // Fix #150: For RUNNING/DISPATCHED tasks, send CancelTask to
                    // the worker BEFORE resetting. Otherwise the worker keeps
                    // executing and its ReportTaskResult overwrites the reset
                    // PENDING state, causing data inconsistency.
                    if (ti.status == "RUNNING" || ti.status == "DISPATCHED") {
                        sendCancelTask(ti.id, ti.worker_id);
                    }
                    auto downstream_reset = task_instance_dao_.resetForRetry(ti.id);
                    if (!downstream_reset.ok()) {
                        spdlog::warn("InstanceService: failed to reset downstream task instance {}: {}",
                                     ti.id, downstream_reset.error());
                    }
                }
            }

            queue.push(target);
        }
    }
}

common::result::Result<void> InstanceService::killTask(
    const std::string& instance_id, const std::string& task_instance_id,
    const std::string& user_id, const std::string& role) {

    auto access_result = checkInstanceAccess(instance_id, user_id, role);
    if (!access_result.ok()) {
        return access_result;
    }

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
    sendCancelTask(task_instance_id, task_instance.worker_id);

    // Mark the task instance as FAILED
    auto update_result = task_instance_dao_.markFinished(
        task_instance_id, "FAILED", -1, "Killed by user");
    if (!update_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to mark task instance as FAILED: " + update_result.error());
    }

    return common::result::Result<void>();
}

common::result::Result<nlohmann::json> InstanceService::getInstance(
    const std::string& id, const std::string& user_id, const std::string& role) {

    auto access_result = checkInstanceAccess(id, user_id, role);
    if (!access_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(access_result.error());
    }
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
    const std::string& workflow_id, int page, int page_size,
    const std::string& user_id, const std::string& role) {

    // Fix #134: non-admin users can only list instances of their own workflows
    if (!user_id.empty() && role != "admin") {
        auto wf_result = workflow_dao_.findById(workflow_id);
        if (!wf_result.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Workflow not found: " + wf_result.error());
        }
        if (wf_result.value().creator_id != user_id) {
            return common::result::Result<nlohmann::json>::failure(
                "Permission denied: you do not own this workflow");
        }
    }

    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;
    if (page_size > 100) page_size = 100;  // Fix #169: page_size 上限 100（约束 §3.3）

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

common::result::Result<nlohmann::json> InstanceService::listAllInstances(
    int page, int page_size, const std::string& user_id, const std::string& role) {
    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;
    if (page_size > 100) page_size = 100;  // Fix #169: page_size 上限 100（约束 §3.3）

    int offset = (page - 1) * page_size;

    // Fix #157: Push creator_id filter into SQL for correct pagination.
    // Previously we fetched a global page then filtered in memory, which
    // returned fewer items than page_size and a wrong total for non-admin users.
    bool filter_by_creator = !user_id.empty() && role != "admin";

    std::vector<common::models::WorkflowInstance> instances;
    int total = 0;

    if (filter_by_creator) {
        auto result = workflow_instance_dao_.listByCreator(user_id, offset, page_size);
        if (!result.ok()) {
            return common::result::Result<nlohmann::json>::failure(result.error());
        }
        instances = std::move(result.value());
        auto countResult = workflow_instance_dao_.countByCreator(user_id);
        total = countResult.ok() ? countResult.value() : 0;
    } else {
        auto result = workflow_instance_dao_.listAll(offset, page_size);
        if (!result.ok()) {
            return common::result::Result<nlohmann::json>::failure(result.error());
        }
        instances = std::move(result.value());
        auto countResult = workflow_instance_dao_.countAll();
        total = countResult.ok() ? countResult.value() : 0;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto& inst : instances) {
        items.push_back(inst.toJson());
    }

    return nlohmann::json{
        {"items", items},
        {"page", page},
        {"page_size", page_size},
        {"total", total}
    };
}

common::result::Result<nlohmann::json> InstanceService::listInstancesByTaskId(
    const std::string& task_id, int page, int page_size,
    const std::string& user_id, const std::string& role) {
    // Fix #225: Server-side filtering by task_id with correct pagination.
    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;
    if (page_size > 100) page_size = 100;

    int offset = (page - 1) * page_size;

    // Fix #225b: Apply the same access control as listAllInstances —
    // non-admin users only see their own instances.
    bool filter_by_creator = !user_id.empty() && role != "admin";
    std::string creator_filter = filter_by_creator ? user_id : "";

    auto list_result = workflow_instance_dao_.listByTaskId(task_id, offset, page_size, creator_filter);
    if (!list_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to list instances by task: " + list_result.error());
    }

    const auto& instances = list_result.value();
    nlohmann::json items = nlohmann::json::array();
    for (const auto& inst : instances) {
        items.push_back(inst.toJson());
    }

    auto countResult = workflow_instance_dao_.countByTaskId(task_id, creator_filter);
    int total = countResult.ok() ? countResult.value() : static_cast<int>(instances.size());

    return nlohmann::json{
        {"items", items},
        {"page", page},
        {"page_size", page_size},
        {"total", total}
    };
}

common::result::Result<std::string> InstanceService::getTaskLog(
    const std::string& instance_id, const std::string& task_instance_id,
    const std::string& user_id, const std::string& role) {

    auto access_result = checkInstanceAccess(instance_id, user_id, role);
    if (!access_result.ok()) {
        return common::result::Result<std::string>::failure(access_result.error());
    }

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
    auto channel = common::util::createWorkerChannel(worker.address, worker_tls_);
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
