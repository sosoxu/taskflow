#include "scheduler/grpc/scheduler_service.h"

#include <string>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "common/util/grpc_auth_util.h"

namespace taskflow::scheduler::grpc {

SchedulerServiceImpl::SchedulerServiceImpl(const std::string& auth_token)
    : auth_token_(auth_token) {}

::grpc::Status SchedulerServiceImpl::Register(
    ::grpc::ServerContext* context,
    const taskflow::v1::RegisterRequest* request,
    taskflow::v1::RegisterResponse* response) {
    auto auth = common::util::GrpcAuthUtil::checkAuth(context, auth_token_);
    if (!auth.ok()) {
        spdlog::warn("Register rejected: {} (name={})", auth.error_message(), request->name());
        return auth;
    }
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
    ::grpc::ServerContext* context,
    const taskflow::v1::DeregisterRequest* request,
    taskflow::v1::DeregisterResponse* response) {
    auto auth = common::util::GrpcAuthUtil::checkAuth(context, auth_token_);
    if (!auth.ok()) {
        return auth;
    }
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
    ::grpc::ServerContext* context,
    const taskflow::v1::HeartbeatRequest* request,
    taskflow::v1::HeartbeatResponse* response) {
    auto auth = common::util::GrpcAuthUtil::checkAuth(context, auth_token_);
    if (!auth.ok()) {
        return auth;
    }
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
    ::grpc::ServerContext* context,
    const taskflow::v1::TaskResultRequest* request,
    taskflow::v1::TaskResultResponse* response) {
    auto auth = common::util::GrpcAuthUtil::checkAuth(context, auth_token_);
    if (!auth.ok()) {
        return auth;
    }

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
            // Fix #199: Do NOT decrement running_tasks here. The task is already
            // terminal, which means running_tasks was already decremented when it
            // first finished (below), or reset to 0 by heartbeat_checker when the
            // worker went offline. Decrementing again would double-count and could
            // drive the counter negative, misleading LoadBalanceDispatcher.
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
    // Fix #337: 仅当本次调用真正完成状态转换（markFinished 成功）时递减。
    // 此前以 findById 预检查结果为依据——与取消/超时路径并发时，双方都通过
    // 预检查导致 running_tasks 双重递减；markFinished 本身是条件 UPDATE
    // （仅 DISPATCHED/RUNNING 可转换），其成败即互斥判据。
    if (finish_result.ok() && ti_result.ok() && !ti_result.value().worker_id.empty()) {
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
