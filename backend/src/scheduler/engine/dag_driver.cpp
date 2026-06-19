#include "scheduler/engine/dag_driver.h"

#include <chrono>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
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
#include "common/util/grpc_channel_util.h"
#include "taskflow.grpc.pb.h"

namespace taskflow::scheduler::engine {

DagDriver::DagDriver(int drive_interval, const std::string& aes_key,
                     std::shared_ptr<grpc::LeaderElection> leader_election,
                     common::config::TlsConfig worker_tls)
    : drive_interval_(drive_interval), aes_key_(aes_key),
      leader_election_(std::move(leader_election)),
      worker_tls_(std::move(worker_tls)) {}

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

        // 只有 leader 节点才驱动 DAG 执行
        if (leader_election_ && !leader_election_->isLeader()) {
            continue;
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
        task_statuses[ti.node_id] = ti.status;
        node_to_instance[ti.node_id] = ti;
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

    // Fix #155: Build node_id -> param_overrides map so timeout detection can
    // honour node-level timeout overrides (验收标准 4.3).
    std::map<std::string, nlohmann::json> node_param_overrides;
    if (dag_json.contains("nodes") && dag_json["nodes"].is_array()) {
        for (const auto& node : dag_json["nodes"]) {
            if (node.contains("id") && node["id"].is_string() &&
                node.contains("param_overrides") && node["param_overrides"].is_object()) {
                node_param_overrides[node["id"].get<std::string>()] = node["param_overrides"];
            }
        }
    }

    // Fix #168: Batch-cache Task lookups to avoid N+1 queries. Previously each
    // drive cycle issued 3N task_dao_.findById() calls (one per iteration in
    // the timeout/retry/final-state loops). Now we collect unique task_ids
    // once and query each only once, reusing the cache across all three loops.
    std::map<std::string, common::models::Task> task_cache;
    {
        std::set<std::string> unique_task_ids;
        for (const auto& ti : task_instances) {
            unique_task_ids.insert(ti.task_id);
        }
        for (const auto& task_id : unique_task_ids) {
            auto task_result = task_dao_.findById(task_id);
            if (task_result.ok()) {
                task_cache[task_id] = task_result.value();
            }
        }
    }

    // 4. 检查超时的 RUNNING 任务
    for (const auto& ti : task_instances) {
        if (ti.status == "RUNNING" && !ti.started_at.empty()) {
            // 解析 started_at 并检查是否超时
            try {
                std::tm tm_started{};
                std::istringstream iss(ti.started_at);
                iss >> std::get_time(&tm_started, "%Y-%m-%d %H:%M:%S");
                if (!iss.fail()) {
                    auto started_tp = std::chrono::system_clock::from_time_t(timegm(&tm_started));
                    auto now_tp = std::chrono::system_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now_tp - started_tp).count();

                    // 获取任务超时时间 (Fix #168: read from cache instead of N+1 DB query)
                    int timeout = 3600;  // 默认超时
                    auto cache_it = task_cache.find(ti.task_id);
                    if (cache_it != task_cache.end()) {
                        timeout = cache_it->second.timeout;
                    }
                    // Fix #155: Apply node-level timeout override from param_overrides.
                    auto npo_it = node_param_overrides.find(ti.node_id);
                    if (npo_it != node_param_overrides.end()) {
                        const auto& overrides = npo_it->second;
                        if (overrides.contains("timeout") && overrides["timeout"].is_number_integer()) {
                            timeout = overrides["timeout"].get<int>();
                        }
                    }

                    if (elapsed > timeout) {
                        spdlog::warn("DagDriver: task instance {} timed out (elapsed={}s, timeout={}s)",
                                     ti.id, elapsed, timeout);
                        auto timeout_result = task_instance_dao_.markFinished(
                            ti.id, "TIMEOUT", -1, "Task execution timed out");
                        if (!timeout_result.ok()) {
                            spdlog::error("DagDriver: failed to mark task instance {} as TIMEOUT: {}",
                                          ti.id, timeout_result.error());
                        }
                    }
                }
            } catch (const std::exception& e) {
                spdlog::warn("DagDriver: failed to parse started_at for task instance {}: {}",
                             ti.id, e.what());
            }
        }
    }

    // 5. Check for FAILED/TIMEOUT tasks that can be retried
    for (const auto& ti : task_instances) {
        if (ti.status == "FAILED" || ti.status == "TIMEOUT") {
            // Get task info for max_retries and retry_interval (Fix #168: cache)
            auto cache_it = task_cache.find(ti.task_id);
            if (cache_it == task_cache.end()) {
                continue;
            }
            const auto& task_info = cache_it->second;
            int max_retries = task_info.max_retries;
            int retry_interval = task_info.retry_interval;

            if (ti.retry_count >= max_retries) {
                continue;  // No more retries
            }

            // Check if enough time has passed since last failure
            if (!ti.finished_at.empty()) {
                try {
                    std::tm tm_finished{};
                    std::istringstream iss(ti.finished_at);
                    iss >> std::get_time(&tm_finished, "%Y-%m-%d %H:%M:%S");
                    if (!iss.fail()) {
                        auto finished_tp = std::chrono::system_clock::from_time_t(timegm(&tm_finished));
                        auto now_tp = std::chrono::system_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now_tp - finished_tp).count();

                        if (elapsed < retry_interval) {
                            spdlog::info("DagDriver: task instance {} waiting for retry interval ({}s remaining)",
                                         ti.id, retry_interval - elapsed);
                            continue;  // Not yet time to retry
                        }
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("DagDriver: failed to parse finished_at for task instance {}: {}",
                                 ti.id, e.what());
                }
            }

            // Reset task instance for retry
            auto retry_result = task_instance_dao_.resetForRetry(ti.id);
            if (retry_result.ok()) {
                spdlog::info("DagDriver: task instance {} reset for retry (retry_count={}/max_retries={})",
                             ti.id, ti.retry_count + 1, max_retries);
                // Update task_statuses to PENDING so it will be dispatched
                task_statuses[ti.node_id] = "PENDING";
            } else {
                spdlog::error("DagDriver: failed to reset task instance {} for retry: {}",
                              ti.id, retry_result.error());
            }
        }
    }

    // Fix #149: When a previously-failed task succeeds after retry, reset its
    // downstream UPSTREAM_FAILED tasks to PENDING so they can be rescheduled.
    // Without this, downstream tasks stay stuck in UPSTREAM_FAILED forever
    // because findReadyTasks/findBlockedTasks only pick PENDING tasks.
    {
        std::map<std::string, std::vector<std::string>> downstream_adj;
        if (dag_json.contains("nodes") && dag_json["nodes"].is_array()) {
            for (const auto& node : dag_json["nodes"]) {
                if (node.contains("id") && node["id"].is_string()) {
                    downstream_adj[node["id"].get<std::string>()] = {};
                }
            }
        }
        if (dag_json.contains("edges") && dag_json["edges"].is_array()) {
            for (const auto& edge : dag_json["edges"]) {
                if (edge.contains("source") && edge["source"].is_string() &&
                    edge.contains("target") && edge["target"].is_string()) {
                    downstream_adj[edge["source"].get<std::string>()].push_back(
                        edge["target"].get<std::string>());
                }
            }
        }
        for (const auto& [node_id, status] : task_statuses) {
            if (status != "SUCCESS") continue;
            auto adj_it = downstream_adj.find(node_id);
            if (adj_it == downstream_adj.end()) continue;
            for (const auto& down_id : adj_it->second) {
                auto ds_it = task_statuses.find(down_id);
                if (ds_it != task_statuses.end() && ds_it->second == "UPSTREAM_FAILED") {
                    auto inst_it = node_to_instance.find(down_id);
                    if (inst_it != node_to_instance.end()) {
                        auto reset = task_instance_dao_.resetForRetry(inst_it->second.id);
                        if (reset.ok()) {
                            task_statuses[down_id] = "PENDING";
                            spdlog::info("DagDriver: reset downstream task instance {} (node {}) "
                                         "to PENDING after upstream {} succeeded",
                                         inst_it->second.id, down_id, node_id);
                        } else {
                            spdlog::error("DagDriver: failed to reset downstream task instance {}: {}",
                                          inst_it->second.id, reset.error());
                        }
                    }
                }
            }
        }
    }

    // 5b. Find ready tasks (all upstream SUCCESS)
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
    bool has_pending_retry = false;
    for (const auto& ti : updated_tasks_result.value()) {
        updated_statuses[ti.node_id] = ti.status;
        // Check if any failed task still has retries remaining
        if ((ti.status == "FAILED" || ti.status == "TIMEOUT")) {
            // Fix #168: read from cache instead of N+1 DB query
            auto cache_it = task_cache.find(ti.task_id);
            if (cache_it != task_cache.end() && ti.retry_count < cache_it->second.max_retries) {
                has_pending_retry = true;
            }
        }
    }

    // Don't mark workflow as finished if there are tasks pending retry
    if (has_pending_retry) {
        return;
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

    // Check if task has been deleted
    if (task.deleted) {
        auto fail_result = task_instance_dao_.markFinished(
            task_instance.id, "FAILED", -1,
            "Task has been deleted: " + task_instance.task_id);
        if (!fail_result.ok()) {
            spdlog::error("DagDriver: failed to mark task instance {} as FAILED for deleted task: {}",
                         task_instance.id, fail_result.error());
        }
        return common::result::Result<void>::failure(
            "Task has been deleted: " + task_instance.task_id);
    }

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

    // Fix #125: Filter online workers by the task's required resource_tags.
    // A task with resource_tags=["gpu"] can only be dispatched to workers
    // whose resource_tags include "gpu". Tasks with no resource_tags can run
    // on any worker (backward compatible).
    auto eligible_workers = filterByResourceTags(workers_result.value(), task.resource_tags);
    if (eligible_workers.empty()) {
        auto fail_result = task_instance_dao_.markFinished(
            task_instance.id, "FAILED", -1,
            "No online worker with required resource_tags");
        if (!fail_result.ok()) {
            spdlog::error("DagDriver: failed to mark task instance {} as FAILED (no tagged worker): {}",
                         task_instance.id, fail_result.error());
        }
        return common::result::Result<void>::failure(
            "No online worker with required resource_tags for task " + task_instance.id);
    }

    auto worker_result = dispatcher->selectWorker(eligible_workers);
    if (!worker_result.ok()) {
        // Mark task as FAILED when no worker is available (e.g., specified worker offline).
        // Without this, the task stays PENDING and is retried indefinitely.
        auto fail_result = task_instance_dao_.markFinished(
            task_instance.id, "FAILED", -1,
            "No available worker: " + worker_result.error());
        if (!fail_result.ok()) {
            spdlog::error("DagDriver: failed to mark task instance {} as FAILED: {}",
                         task_instance.id, fail_result.error());
        }
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
    // Fix #126: Use TLS-aware channel creation instead of hardcoded insecure credentials.
    auto channel = common::util::createWorkerChannel(worker.address, worker_tls_);
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

    // Merge param_overrides from DAG node
    try {
        const auto& dag = workflow.dag_json;
        if (dag.contains("nodes") && dag["nodes"].is_array()) {
            for (const auto& node : dag["nodes"]) {
                if (node.contains("id") && node["id"].is_string() &&
                    node["id"].get<std::string>() == task_instance.node_id) {
                    if (node.contains("param_overrides") && node["param_overrides"].is_object()) {
                        for (auto& [key, value] : node["param_overrides"].items()) {
                            config[key] = value;
                        }
                    }
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("DagDriver: failed to merge param_overrides: {}", e.what());
    }

    // Fix #168: Cache the WorkflowInstance lookup; it was fetched twice
    // (once for config overrides, once for params overrides).
    auto wf_instance_result = workflow_instance_dao_.findById(task_instance.workflow_instance_id);

    // Merge param_overrides from WorkflowInstance (runtime overrides)
    try {
        if (wf_instance_result.ok() && wf_instance_result.value().param_overrides.is_object()) {
            for (auto& [key, value] : wf_instance_result.value().param_overrides.items()) {
                config[key] = value;
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("DagDriver: failed to merge workflow instance param_overrides: {}", e.what());
    }

    // Resolve ${var} placeholders in config using task.parameters_json + param_overrides
    try {
        nlohmann::json params = task.parameters_json;
        if (!params.is_object()) {
            params = nlohmann::json::object();
        }

        // Merge DAG node param_overrides into params (override default parameters)
        try {
            const auto& dag = workflow.dag_json;
            if (dag.contains("nodes") && dag["nodes"].is_array()) {
                for (const auto& node : dag["nodes"]) {
                    if (node.contains("id") && node["id"].is_string() &&
                        node["id"].get<std::string>() == task_instance.node_id) {
                        if (node.contains("param_overrides") && node["param_overrides"].is_object()) {
                            for (auto& [key, value] : node["param_overrides"].items()) {
                                params[key] = value;
                            }
                        }
                        break;
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("DagDriver: failed to merge DAG node param_overrides into params: {}", e.what());
        }

        // Merge WorkflowInstance param_overrides into params (highest priority, runtime overrides)
        // Fix #168: reuse the cached wf_instance_result instead of querying again.
        try {
            if (wf_instance_result.ok() && wf_instance_result.value().param_overrides.is_object()) {
                for (auto& [key, value] : wf_instance_result.value().param_overrides.items()) {
                    params[key] = value;
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("DagDriver: failed to merge workflow instance param_overrides into params: {}", e.what());
        }

        resolvePlaceholders(config, params);
    } catch (const std::exception& e) {
        spdlog::warn("DagDriver: failed to resolve placeholders: {}", e.what());
    }

    request.set_workflow_instance_id(task_instance.workflow_instance_id);
    request.set_config_json(config.dump());

    // Fix #155: Apply timeout override from param_overrides. The param_overrides
    // (DAG node + instance runtime) have been merged into config above. If a
    // "timeout" key is present, override the gRPC request's timeout field so
    // the worker and scheduler use the node-level value (验收标准 4.3).
    if (config.contains("timeout") && config["timeout"].is_number_integer()) {
        request.set_timeout(config["timeout"].get<int>());
    }

    taskflow::v1::TaskDispatchResponse response;
    // Fix #127: Retry DispatchTask on transient failures (UNAVAILABLE, DEADLINE_EXCEEDED)
    // with exponential backoff. A fresh ClientContext must be created for each
    // attempt because gRPC contexts are single-use.
    auto grpc_status = common::util::retryWorkerRpc([&]() -> ::grpc::Status {
        ::grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        return stub->DispatchTask(&ctx, request, &response);
    });

    if (!grpc_status.ok()) {
        // Fix #148: Roll back task to PENDING so it can be rescheduled on the
        // next drive cycle. Without this the task is stuck in DISPATCHED forever
        // (findReadyTasks only picks PENDING, timeout check only covers RUNNING).
        auto rollback = task_instance_dao_.resetForRetry(task_instance.id);
        if (!rollback.ok()) {
            spdlog::error("DagDriver: failed to roll back task instance {} after gRPC failure: {}",
                          task_instance.id, rollback.error());
        }
        return common::result::Result<void>::failure(
            "gRPC DispatchTask failed: " + grpc_status.error_message());
    }

    if (!response.accepted()) {
        // Fix #148: Roll back task to PENDING when the worker rejects it.
        auto rollback = task_instance_dao_.resetForRetry(task_instance.id);
        if (!rollback.ok()) {
            spdlog::error("DagDriver: failed to roll back task instance {} after worker rejection: {}",
                          task_instance.id, rollback.error());
        }
        return common::result::Result<void>::failure(
            "Worker rejected task: " + response.error_message());
    }

    // Mark task instance as RUNNING and set started_at
    auto running_result = task_instance_dao_.markRunning(task_instance.id);
    if (!running_result.ok()) {
        spdlog::warn("DagDriver: failed to mark task instance {} as RUNNING: {}",
                     task_instance.id, running_result.error());
    }

    // Increment worker's running_tasks immediately for accurate load balancing
    // Fix #154: Use atomic SQL increment instead of read-modify-write to avoid
    // racing with HeartbeatChecker (updateHeartbeat) and ReportTaskResult (decrementRunningTasks).
    auto update_tasks_result = worker_dao_.incrementRunningTasks(worker.id);
    if (!update_tasks_result.ok()) {
        spdlog::warn("DagDriver: failed to update running_tasks for worker {}: {}",
                     worker.id, update_tasks_result.error());
    }

    spdlog::info("DagDriver: dispatched task instance {} to worker {} ({})",
                 task_instance.id, worker.id, worker.name);

    return common::result::Result<void>();
}

std::string DagDriver::resolveString(const std::string& input, const nlohmann::json& params) {
    std::string result;
    size_t i = 0;
    while (i < input.size()) {
        if (i + 1 < input.size() && input[i] == '$' && input[i + 1] == '{') {
            // Find closing brace
            size_t end = input.find('}', i + 2);
            if (end != std::string::npos) {
                std::string var_name = input.substr(i + 2, end - i - 2);
                if (params.contains(var_name)) {
                    if (params[var_name].is_string()) {
                        result += params[var_name].get<std::string>();
                    } else {
                        result += params[var_name].dump();
                    }
                } else {
                    // Variable not found, keep placeholder as-is
                    result += input.substr(i, end - i + 1);
                }
                i = end + 1;
            } else {
                result += input[i];
                i++;
            }
        } else {
            result += input[i];
            i++;
        }
    }
    return result;
}

void DagDriver::resolvePlaceholders(nlohmann::json& config, const nlohmann::json& params) {
    if (config.is_string()) {
        std::string str_val = config.get<std::string>();
        config = resolveString(str_val, params);
    } else if (config.is_object()) {
        for (auto& [key, value] : config.items()) {
            resolvePlaceholders(value, params);
        }
    } else if (config.is_array()) {
        for (auto& item : config) {
            resolvePlaceholders(item, params);
        }
    }
}

}  // namespace taskflow::scheduler::engine
