#pragma once

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "taskflow.grpc.pb.h"
#include "common/result/result.h"

namespace taskflow::worker::grpc {

class WorkerClient {
public:
    explicit WorkerClient(std::shared_ptr<::grpc::Channel> channel);

    taskflow::common::result::Result<std::string> registerWorker(
        const std::string& name, const std::string& address,
        int max_tasks, const std::vector<std::string>& resource_tags);

    taskflow::common::result::Result<void> sendHeartbeat(
        const std::string& worker_id, double cpu_usage,
        double memory_usage, int running_tasks);

    taskflow::common::result::Result<void> reportTaskResult(
        const std::string& task_instance_id, const std::string& status,
        int exit_code, const std::string& error_message);

private:
    std::unique_ptr<taskflow::v1::SchedulerService::Stub> stub_;
};

}  // namespace taskflow::worker::grpc
