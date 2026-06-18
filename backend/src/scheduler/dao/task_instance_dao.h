#pragma once
#include <string>
#include <vector>
#include <tuple>
#include "common/result/result.h"
#include "common/models/task_instance.h"

namespace taskflow::scheduler::dao {

class TaskInstanceDao {
public:
    common::result::Result<std::string> create(
        const std::string& workflow_instance_id,
        const std::string& task_id,
        int task_version,
        const std::string& task_name,
        const std::string& node_id = "");

    common::result::Result<common::models::TaskInstance> findById(const std::string& id);

    common::result::Result<void> updateStatus(const std::string& id,
                                               const std::string& status);

    common::result::Result<void> dispatch(const std::string& id,
                                           const std::string& worker_id);

    common::result::Result<void> markRunning(const std::string& id);

    common::result::Result<void> markFinished(const std::string& id,
                                               const std::string& status,
                                               int exit_code,
                                               const std::string& error_message);

    common::result::Result<void> resetForRetry(const std::string& id);

    common::result::Result<std::vector<common::models::TaskInstance>> listByWorkflowInstance(
        const std::string& workflow_instance_id);

    common::result::Result<std::vector<common::models::TaskInstance>> listByWorkerId(
        const std::string& worker_id);

    common::result::Result<std::vector<std::string>> batchCreate(
        const std::string& workflow_instance_id,
        const std::vector<std::tuple<std::string, std::string, int, std::string>>& tasks);
};

}  // namespace taskflow::scheduler::dao
