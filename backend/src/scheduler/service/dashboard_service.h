#pragma once

#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "scheduler/dao/task_dao.h"
#include "scheduler/dao/workflow_dao.h"
#include "scheduler/dao/workflow_instance_dao.h"
#include "scheduler/dao/worker_dao.h"

namespace taskflow::scheduler::service {

// DashboardService provides aggregated statistics for the dashboard UI.
// Fix #123: dedicated dashboard API to avoid N+1 frontend calls and to surface
// metrics (today's executions, success rate) that the frontend cannot compute
// efficiently from list endpoints.
class DashboardService {
public:
    DashboardService();

    // Returns dashboard statistics:
    //   total_tasks, total_workflows, running_instances, online_workers,
    //   today_executions, success_rate, recent_instances (latest 10)
    common::result::Result<nlohmann::json> getStats();

private:
    dao::TaskDao task_dao_;
    dao::WorkflowDao workflow_dao_;
    dao::WorkflowInstanceDao workflow_instance_dao_;
    dao::WorkerDao worker_dao_;
};

}  // namespace taskflow::scheduler::service
