#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "common/models/workflow.h"

namespace taskflow::scheduler::dao {

class WorkflowDao {
public:
    common::result::Result<std::string> create(
        const std::string& name,
        const std::string& description,
        const nlohmann::json& dag_json,
        const std::string& schedule_strategy,
        const std::string& target_worker_id,
        const std::string& cron_expression,
        bool cron_enabled,
        const std::string& creator_id);

    common::result::Result<common::models::Workflow> findById(const std::string& id);
    common::result::Result<common::models::Workflow> findByName(const std::string& name);

    common::result::Result<void> update(
        const std::string& id,
        const std::string& name,
        const std::string& description,
        const nlohmann::json& dag_json,
        const std::string& schedule_strategy,
        const std::string& target_worker_id,
        const std::string& cron_expression,
        bool cron_enabled);

    common::result::Result<void> softDelete(const std::string& id);

    common::result::Result<std::vector<common::models::Workflow>> list(
        int offset, int limit,
        const std::string& keyword,
        const std::string& creator_id);

    // 计数（与 list 相同筛选条件）
    common::result::Result<int> count(
        const std::string& keyword,
        const std::string& creator_id);
};

}  // namespace taskflow::scheduler::dao
