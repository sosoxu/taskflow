#pragma once

#include <string>

#include <grpcpp/grpcpp.h>
#include "taskflow.grpc.pb.h"
#include "scheduler/dao/worker_dao.h"
#include "scheduler/dao/task_instance_dao.h"

namespace taskflow::scheduler::grpc {

class SchedulerServiceImpl final
    : public taskflow::v1::SchedulerService::Service {
public:
    // Fix #326: auth_token 非空时启用内部认证（worker 注册/心跳/结果上报
    // 必须携带相同 token），为空保持旧行为（兼容本地开发）。
    explicit SchedulerServiceImpl(const std::string& auth_token = {});

    ::grpc::Status Register(::grpc::ServerContext* context,
                            const taskflow::v1::RegisterRequest* request,
                            taskflow::v1::RegisterResponse* response) override;

    ::grpc::Status Deregister(::grpc::ServerContext* context,
                              const taskflow::v1::DeregisterRequest* request,
                              taskflow::v1::DeregisterResponse* response) override;

    ::grpc::Status Heartbeat(::grpc::ServerContext* context,
                             const taskflow::v1::HeartbeatRequest* request,
                             taskflow::v1::HeartbeatResponse* response) override;

    ::grpc::Status ReportTaskResult(
        ::grpc::ServerContext* context,
        const taskflow::v1::TaskResultRequest* request,
        taskflow::v1::TaskResultResponse* response) override;

private:
    taskflow::scheduler::dao::WorkerDao worker_dao_;
    taskflow::scheduler::dao::TaskInstanceDao task_instance_dao_;
    std::string auth_token_;
};

}  // namespace taskflow::scheduler::grpc
