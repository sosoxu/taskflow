#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "scheduler/dao/task_dao.h"
#include "scheduler/dao/task_instance_dao.h"
#include "scheduler/dao/worker_dao.h"
#include "scheduler/dao/workflow_dao.h"
#include "scheduler/dao/workflow_instance_dao.h"
#include "scheduler/engine/dag_engine.h"
#include "scheduler/engine/dispatcher.h"
#include "scheduler/grpc/leader_election.h"

namespace taskflow::scheduler::engine {

class DagDriver {
public:
    DagDriver(int drive_interval, const std::string& aes_key,
              std::shared_ptr<grpc::LeaderElection> leader_election);

    void start();
    void stop();

private:
    void driveLoop();
    void driveInstance(const common::models::WorkflowInstance& instance);
    common::result::Result<void> dispatchTask(
        const common::models::TaskInstance& task_instance,
        const common::models::Workflow& workflow);

    int drive_interval_;
    std::string aes_key_;
    std::shared_ptr<grpc::LeaderElection> leader_election_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    dao::WorkflowInstanceDao workflow_instance_dao_;
    dao::TaskInstanceDao task_instance_dao_;
    dao::WorkerDao worker_dao_;
    dao::TaskDao task_dao_;
    dao::WorkflowDao workflow_dao_;
};

}  // namespace taskflow::scheduler::engine
