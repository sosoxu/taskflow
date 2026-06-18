#include "worker/grpc/worker_client.h"

namespace taskflow::worker::grpc {

WorkerClient::WorkerClient(std::shared_ptr<::grpc::Channel> channel)
    : stub_(taskflow::v1::SchedulerService::NewStub(channel)) {}

template<typename Func>
::grpc::Status WorkerClient::retryRpc(Func rpc_call, int max_retries) {
    for (int i = 0; i <= max_retries; ++i) {
        ::grpc::Status status = rpc_call();
        if (status.ok()) {
            return status;
        }
        if (i < max_retries) {
            int delay_ms = 1000 * (1 << i);  // 指数退避: 1s, 2s, 4s
            spdlog::warn("WorkerClient: RPC failed (attempt {}/{}), retrying in {}ms: {}",
                         i + 1, max_retries + 1, delay_ms, status.error_message());
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "All retry attempts exhausted");
}

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

    auto call = [&]() -> ::grpc::Status {
        ::grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        return stub_->Register(&context, request, &response);
    };

    auto status = retryRpc(call);

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

common::result::Result<void> WorkerClient::deregisterWorker(
    const std::string& worker_id) {
    taskflow::v1::DeregisterRequest request;
    request.set_worker_id(worker_id);

    taskflow::v1::DeregisterResponse response;

    auto call = [&]() -> ::grpc::Status {
        ::grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        return stub_->Deregister(&context, request, &response);
    };

    auto status = retryRpc(call);

    if (!status.ok()) {
        return common::result::Result<void>::failure(
            "gRPC Deregister failed: " + status.error_message());
    }

    if (!response.success()) {
        return common::result::Result<void>::failure(
            response.error_message());
    }

    return common::result::Result<void>();
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

    auto call = [&]() -> ::grpc::Status {
        ::grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        return stub_->Heartbeat(&context, request, &response);
    };

    auto status = retryRpc(call);

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

    auto call = [&]() -> ::grpc::Status {
        ::grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        return stub_->ReportTaskResult(&context, request, &response);
    };

    auto grpc_status = retryRpc(call);

    if (!grpc_status.ok()) {
        return common::result::Result<void>::failure(
            "gRPC ReportTaskResult failed: " + grpc_status.error_message());
    }

    return common::result::Result<void>();
}

}  // namespace taskflow::worker::grpc
