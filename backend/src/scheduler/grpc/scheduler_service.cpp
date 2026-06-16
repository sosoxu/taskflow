#include "scheduler/grpc/scheduler_service.h"

#include <nlohmann/json.hpp>

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
    task_instance_dao_.markFinished(request->task_instance_id(),
                                    request->status(), request->exit_code(),
                                    request->error_message());

    response->set_acknowledged(true);
    return ::grpc::Status::OK;
}

}  // namespace taskflow::scheduler::grpc
