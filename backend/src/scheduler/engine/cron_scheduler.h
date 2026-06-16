#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "common/models/cron_job.h"
#include "scheduler/dao/cron_job_dao.h"
#include "scheduler/dao/task_dao.h"
#include "scheduler/dao/task_instance_dao.h"
#include "scheduler/dao/workflow_dao.h"
#include "scheduler/dao/workflow_instance_dao.h"
#include "scheduler/grpc/leader_election.h"

namespace taskflow::scheduler::engine {

class CronScheduler {
public:
    explicit CronScheduler(std::shared_ptr<grpc::LeaderElection> leader_election);

    void start();
    void stop();

private:
    void cronLoop();
    void triggerCronJob(const common::models::CronJob& cron_job);

    std::shared_ptr<grpc::LeaderElection> leader_election_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    dao::CronJobDao cron_job_dao_;
    dao::WorkflowDao workflow_dao_;
    dao::WorkflowInstanceDao workflow_instance_dao_;
    dao::TaskInstanceDao task_instance_dao_;
    dao::TaskDao task_dao_;
};

}  // namespace taskflow::scheduler::engine
