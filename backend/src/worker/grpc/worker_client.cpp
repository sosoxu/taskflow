#include "worker/grpc/worker_client.h"

namespace taskflow::worker::grpc {

WorkerClient::WorkerClient(std::shared_ptr<::grpc::Channel> channel)
    : stub_(taskflow::v1::SchedulerService::NewStub(channel)) {}

common::result::Result<std::string> WorkerClient::registerWorker(
    const std::string& name, const std::string& address,
    int max_tasks, const std::vector<std::string>& resource_tags) {
    taskflow::v1::RegisterRequest request;
    request.set_name(name);
    request.set_address(address);
    request.set_max_tasks(max_tasks);
    for (const auto& tag : resource_tags) {
        request.add_resource_tags(tag);
    }

    taskflow::v1::RegisterResponse response;
    ::grpc::ClientContext context;

    auto status = stub_->Register(&context, request, &response);

    if (!status.ok()) {
        return common::result::Result<std::string>::failure(
            "gRPC Register failed: " + status.error_message());
    }

    if (!response.success()) {
        return common::result::Result<std::string>::failure(
            response.error_message());
    }

    return std::string(response.worker_id());
}

common::result::Result<void> WorkerClient::sendHeartbeat(
    const std::string& worker_id, double cpu_usage,
    double memory_usage, int running_tasks) {
    taskflow::v1::HeartbeatRequest request;
    request.set_worker_id(worker_id);
    request.set_cpu_usage(cpu_usage);
    request.set_memory_usage(memory_usage);
    request.set_running_tasks(running_tasks);

    taskflow::v1::HeartbeatResponse response;
    ::grpc::ClientContext context;

    auto status = stub_->Heartbeat(&context, request, &response);

    if (!status.ok()) {
        return common::result::Result<void>::failure(
            "gRPC Heartbeat failed: " + status.error_message());
    }

    if (!response.acknowledged()) {
        return common::result::Result<void>::failure(
            "Heartbeat not acknowledged by scheduler");
    }

    return common::result::Result<void>();
}

common::result::Result<void> WorkerClient::reportTaskResult(
    const std::string& task_instance_id, const std::string& status,
    int exit_code, const std::string& error_message) {
    taskflow::v1::TaskResultRequest request;
    request.set_task_instance_id(task_instance_id);
    request.set_status(status);
    request.set_exit_code(exit_code);
    request.set_error_message(error_message);

    taskflow::v1::TaskResultResponse response;
    ::grpc::ClientContext context;

    auto grpc_status = stub_->ReportTaskResult(&context, request, &response);

    if (!grpc_status.ok()) {
        return common::result::Result<void>::failure(
            "gRPC ReportTaskResult failed: " + grpc_status.error_message());
    }

    return common::result::Result<void>();
}

}  // namespace taskflow::worker::grpc
