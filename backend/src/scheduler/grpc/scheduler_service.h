#pragma once

#include <grpcpp/grpcpp.h>
#include "taskflow.grpc.pb.h"
#include "scheduler/dao/worker_dao.h"
#include "scheduler/dao/task_instance_dao.h"

namespace taskflow::scheduler::grpc {

class SchedulerServiceImpl final
    : public taskflow::v1::SchedulerService::Service {
public:
    SchedulerServiceImpl();

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
};

}  // namespace taskflow::scheduler::grpc
