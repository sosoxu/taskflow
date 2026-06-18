#include "scheduler/grpc/scheduler_service.h"

#include <string>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace taskflow::scheduler::grpc {

SchedulerServiceImpl::SchedulerServiceImpl() = default;

::grpc::Status SchedulerServiceImpl::Register(
    ::grpc::ServerContext* /*context*/,
    const taskflow::v1::RegisterRequest* request,
    taskflow::v1::RegisterResponse* response) {
    nlohmann::json resource_tags = nlohmann::json::array();
    for (int i = 0; i < request->resource_tags_size(); ++i) {
        resource_tags.push_back(request->resource_tags(i));
    }

    auto result = worker_dao_.create(request->name(), request->address(),
                                     request->max_tasks(), resource_tags);

    if (!result.ok()) {
        response->set_success(false);
        response->set_error_message(result.error());
        return ::grpc::Status::OK;
    }

    response->set_success(true);
    response->set_worker_id(result.value());
    return ::grpc::Status::OK;
}

::grpc::Status SchedulerServiceImpl::Deregister(
    ::grpc::ServerContext* /*context*/,
    const taskflow::v1::DeregisterRequest* request,
    taskflow::v1::DeregisterResponse* response) {
    // Fix #124: Mark the worker offline on graceful shutdown so the dispatcher
    // stops sending new tasks to it immediately (rather than waiting for the
    // heartbeat timeout). Running tasks are left to finish; the worker is
    // expected to wait for them before exiting.
    auto result = worker_dao_.updateStatus(request->worker_id(), "offline");
    if (!result.ok()) {
        spdlog::warn("Deregister failed for worker {}: {}", request->worker_id(), result.error());
        response->set_success(false);
        response->set_error_message(result.error());
        return ::grpc::Status::OK;
    }
    spdlog::info("Worker {} deregistered, marked offline", request->worker_id());
    response->set_success(true);
    return ::grpc::Status::OK;
}

::grpc::Status SchedulerServiceImpl::Heartbeat(
    ::grpc::ServerContext* /*context*/,
    const taskflow::v1::HeartbeatRequest* request,
    taskflow::v1::HeartbeatResponse* response) {
    auto result = worker_dao_.updateHeartbeat(
        request->worker_id(), request->cpu_usage(),
        request->memory_usage(), request->running_tasks());

    if (!result.ok()) {
        response->set_acknowledged(false);
        return ::grpc::Status::OK;
    }

    response->set_acknowledged(true);
    return ::grpc::Status::OK;
}

::grpc::Status SchedulerServiceImpl::ReportTaskResult(
    ::grpc::ServerContext* /*context*/,
    const taskflow::v1::TaskResultRequest* request,
    taskflow::v1::TaskResultResponse* response) {

    const std::string& ti_id = request->task_instance_id();
    const std::string& status = request->status();

    // Fix #159: Validate that the reported status is a legal terminal status.
    static const std::unordered_set<std::string> kValidStatuses = {
        "SUCCESS", "FAILED", "TIMEOUT", "CANCELLED", "NODE_OFFLINE"
    };
    if (kValidStatuses.find(status) == kValidStatuses.end()) {
        spdlog::warn("ReportTaskResult: invalid status '{}' for task instance {}", status, ti_id);
        response->set_acknowledged(false);
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "Invalid task status: " + status);
    }

    // Fix #151: Do not overwrite an existing terminal state. If the task was
    // already CANCELLED/TIMEOUT/etc. (e.g. by cancelInstance or DagDriver
    // timeout), ignore the worker's late ReportTaskResult to avoid resurrecting
    // a cancelled task as SUCCESS and triggering wrong downstream dispatch.
    auto ti_result = task_instance_dao_.findById(ti_id);
    if (ti_result.ok()) {
        const auto& ti = ti_result.value();
        static const std::unordered_set<std::string> kTerminal = {
            "SUCCESS", "FAILED", "TIMEOUT", "CANCELLED", "NODE_OFFLINE", "UPSTREAM_FAILED"
        };
        if (kTerminal.count(ti.status) > 0) {
            spdlog::info("ReportTaskResult: ignoring late report for task instance {} "
                         "(current status={}, reported status={})",
                         ti_id, ti.status, status);
            // Still decrement running_tasks if the worker had a task assigned.
            if (!ti.worker_id.empty()) {
                worker_dao_.decrementRunningTasks(ti.worker_id);
            }
            response->set_acknowledged(true);
            return ::grpc::Status::OK;
        }
    }

    // Fix #159: Check markFinished return value instead of ignoring it.
    auto finish_result = task_instance_dao_.markFinished(
        ti_id, status, request->exit_code(), request->error_message());
    if (!finish_result.ok()) {
        spdlog::warn("ReportTaskResult: markFinished failed for task instance {}: {}",
                     ti_id, finish_result.error());
    }

    // Fix #121: Decrement the worker's running_tasks counter when a task finishes.
    // Without this, LoadBalanceDispatcher sees stale (inflated) load between heartbeats.
    if (ti_result.ok() && !ti_result.value().worker_id.empty()) {
        auto dec_result = worker_dao_.decrementRunningTasks(ti_result.value().worker_id);
        if (!dec_result.ok()) {
            spdlog::warn("ReportTaskResult: failed to decrement running_tasks for worker {}: {}",
                         ti_result.value().worker_id, dec_result.error());
        }
    }

    response->set_acknowledged(true);
    return ::grpc::Status::OK;
}

}  // namespace taskflow::scheduler::grpc
