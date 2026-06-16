#include "scheduler/engine/dag_driver.h"

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "common/models/task.h"
#include "common/models/task_instance.h"
#include "common/models/worker_info.h"
#include "common/models/workflow.h"
#include "common/models/workflow_instance.h"
#include "common/result/result.h"
#include "common/util/crypto_util.h"
#include "taskflow.grpc.pb.h"

namespace taskflow::scheduler::engine {

DagDriver::DagDriver(int drive_interval, const std::string& aes_key)
    : drive_interval_(drive_interval), aes_key_(aes_key) {}

void DagDriver::start() {
    running_ = true;
    thread_ = std::thread(&DagDriver::driveLoop, this);
}

void DagDriver::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void DagDriver::driveLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(drive_interval_));

        if (!running_) {
            break;
        }

        auto instances_result = workflow_instance_dao_.listActive();
        if (!instances_result.ok()) {
            spdlog::error("DagDriver: failed to list active workflow instances: {}",
                          instances_result.error());
            continue;
        }

        for (const auto& instance : instances_result.value()) {
            try {
                driveInstance(instance);
            } catch (const std::exception& e) {
                spdlog::error("DagDriver: exception driving workflow instance {}: {}",
                              instance.id, e.what());
            }
        }
    }
}

void DagDriver::driveInstance(const common::models::WorkflowInstance& instance) {
    // 1. Get all TaskInstances for this WorkflowInstance
    auto tasks_result = task_instance_dao_.listByWorkflowInstance(instance.id);
    if (!tasks_result.ok()) {
        spdlog::error("DagDriver: failed to list task instances for workflow instance {}: {}",
                      instance.id, tasks_result.error());
        return;
    }

    const auto& task_instances = tasks_result.value();
    if (task_instances.empty()) {
        return;
    }

    // 2. Build a map of node_id -> status from TaskInstances
    std::map<std::string, std::string> task_statuses;
    // Also build node_id -> TaskInstance mapping for dispatch
    std::map<std::string, common::models::TaskInstance> node_to_instance;
    for (const auto& ti : task_instances) {
        task_statuses[ti.task_id] = ti.status;
        node_to_instance[ti.task_id] = ti;
    }

    // 3. Get the Workflow by workflow_id to get dag_json and schedule_strategy
    auto workflow_result = workflow_dao_.findById(instance.workflow_id);
    if (!workflow_result.ok()) {
        spdlog::error("DagDriver: failed to find workflow {}: {}",
                      instance.workflow_id, workflow_result.error());
        return;
    }

    const auto& workflow = workflow_result.value();
    const auto& dag_json = workflow.dag_json;

    // 4. Find ready tasks (all upstream SUCCESS)
    auto ready_tasks = DagEngine::findReadyTasks(dag_json, task_statuses);

    // 5. For each ready task, dispatch it
    for (const auto& node_id : ready_tasks) {
        auto it = node_to_instance.find(node_id);
        if (it == node_to_instance.end()) {
            spdlog::warn("DagDriver: ready node {} not found in task instances", node_id);
            continue;
        }

        auto dispatch_result = dispatchTask(it->second, workflow);
        if (!dispatch_result.ok()) {
            spdlog::error("DagDriver: failed to dispatch task instance {} (node {}): {}",
                          it->second.id, node_id, dispatch_result.error());
        }
    }

    // 6. Find blocked tasks (upstream failed) and mark them UPSTREAM_FAILED
    auto blocked_tasks = DagEngine::findBlockedTasks(dag_json, task_statuses);
    for (const auto& node_id : blocked_tasks) {
        auto it = node_to_instance.find(node_id);
        if (it == node_to_instance.end()) {
            continue;
        }

        auto update_result = task_instance_dao_.updateStatus(it->second.id, "UPSTREAM_FAILED");
        if (!update_result.ok()) {
            spdlog::error("DagDriver: failed to mark task instance {} as UPSTREAM_FAILED: {}",
                          it->second.id, update_result.error());
        } else {
            spdlog::info("DagDriver: marked task instance {} (node {}) as UPSTREAM_FAILED",
                         it->second.id, node_id);
        }
    }

    // 7. If all tasks finished, update WorkflowInstance status accordingly
    // Re-build task_statuses after updates
    auto updated_tasks_result = task_instance_dao_.listByWorkflowInstance(instance.id);
    if (!updated_tasks_result.ok()) {
        return;
    }

    std::map<std::string, std::string> updated_statuses;
    for (const auto& ti : updated_tasks_result.value()) {
        updated_statuses[ti.task_id] = ti.status;
    }

    if (DagEngine::allTasksFinished(updated_statuses)) {
        bool all_success = true;
        for (const auto& [id, status] : updated_statuses) {
            if (status != "SUCCESS") {
                all_success = false;
                break;
            }
        }

        std::string final_status = all_success ? "SUCCESS" : "FAILED";
        auto finish_result = workflow_instance_dao_.markFinished(instance.id, final_status);
        if (!finish_result.ok()) {
            spdlog::error("DagDriver: failed to mark workflow instance {} as {}: {}",
                          instance.id, final_status, finish_result.error());
        } else {
            spdlog::info("DagDriver: workflow instance {} finished with status {}",
                         instance.id, final_status);
        }
    } else if (instance.status == "PENDING") {
        // Mark workflow instance as RUNNING if it's still PENDING
        auto run_result = workflow_instance_dao_.markRunning(instance.id);
        if (!run_result.ok()) {
            spdlog::error("DagDriver: failed to mark workflow instance {} as RUNNING: {}",
                          instance.id, run_result.error());
        }
    }
}

common::result::Result<void> DagDriver::dispatchTask(
    const common::models::TaskInstance& task_instance,
    const common::models::Workflow& workflow) {
    // 1. Get task info from task_dao_
    auto task_result = task_dao_.findById(task_instance.task_id);
    if (!task_result.ok()) {
        return common::result::Result<void>::failure(
            "Task not found: " + task_instance.task_id);
    }

    const auto& task = task_result.value();

    // 2. Create appropriate Dispatcher based on schedule_strategy
    std::unique_ptr<Dispatcher> dispatcher;
    if (workflow.schedule_strategy == "random") {
        dispatcher = std::make_unique<RandomDispatcher>();
    } else if (workflow.schedule_strategy == "load_balance") {
        dispatcher = std::make_unique<LoadBalanceDispatcher>();
    } else if (workflow.schedule_strategy == "specified") {
        dispatcher = std::make_unique<SpecifiedDispatcher>(workflow.target_worker_id);
    } else {
        dispatcher = std::make_unique<RandomDispatcher>();
    }

    // 3. Select a worker
    auto workers_result = worker_dao_.listOnline();
    if (!workers_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to list online workers: " + workers_result.error());
    }

    auto worker_result = dispatcher->selectWorker(workers_result.value());
    if (!worker_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to select worker: " + worker_result.error());
    }

    const auto& worker = worker_result.value();

    // 4. Update TaskInstance status to DISPATCHED and set worker_id
    auto dispatch_result = task_instance_dao_.dispatch(task_instance.id, worker.id);
    if (!dispatch_result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to update task instance status: " + dispatch_result.error());
    }

    // 5. Call Worker's DispatchTask gRPC
    auto channel = grpc::CreateChannel(worker.address, grpc::InsecureChannelCredentials());
    auto stub = taskflow::v1::WorkerService::NewStub(channel);

    taskflow::v1::TaskDispatchRequest request;
    request.set_task_instance_id(task_instance.id);
    request.set_task_type(task.type);
    request.set_timeout(task.timeout);
    request.set_retry_count(task_instance.retry_count);

    // For SQL tasks, decrypt db_password before sending
    nlohmann::json config = task.config_json;
    if (task.type == "sql" && config.contains("db_password")) {
        auto decrypt_result = common::util::CryptoUtil::decrypt(
            config["db_password"].get<std::string>(), aes_key_);
        if (decrypt_result.ok()) {
            config["db_password"] = decrypt_result.value();
        } else {
            spdlog::warn("DagDriver: failed to decrypt db_password for task instance {}: {}",
                         task_instance.id, decrypt_result.error());
        }
    }
    request.set_config_json(config.dump());

    taskflow::v1::TaskDispatchResponse response;
    grpc::ClientContext context;
    auto grpc_status = stub->DispatchTask(&context, request, &response);

    if (!grpc_status.ok()) {
        return common::result::Result<void>::failure(
            "gRPC DispatchTask failed: " + grpc_status.error_message());
    }

    if (!response.accepted()) {
        return common::result::Result<void>::failure(
            "Worker rejected task: " + response.error_message());
    }

    spdlog::info("DagDriver: dispatched task instance {} to worker {} ({})",
                 task_instance.id, worker.id, worker.name);

    return common::result::Result<void>();
}

}  // namespace taskflow::scheduler::engine
