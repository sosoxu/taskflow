#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "common/models/task.h"

namespace taskflow::scheduler::dao {

class TaskDao {
public:
    common::result::Result<std::string> create(const std::string& name,
                                                const std::string& type,
                                                const nlohmann::json& config_json,
                                                const std::string& description,
                                                int timeout,
                                                int max_retries,
                                                int retry_interval,
                                                const nlohmann::json& resource_tags,
                                                const nlohmann::json& parameters_json,
                                                const std::string& creator_id);

    common::result::Result<common::models::Task> findById(const std::string& id);

    common::result::Result<common::models::Task> findByName(const std::string& name);

    // 更新任务（version 自增）
    common::result::Result<void> update(const std::string& id,
                                         const std::string& name,
                                         const std::string& type,
                                         const nlohmann::json& config_json,
                                         const std::string& description,
                                         int timeout,
                                         int max_retries,
                                         int retry_interval,
                                         const nlohmann::json& resource_tags,
                                         const nlohmann::json& parameters_json);

    // 软删除
    common::result::Result<void> softDelete(const std::string& id);

    // 列表（分页 + 筛选）
    common::result::Result<std::vector<common::models::Task>> list(
        int offset, int limit,
        const std::string& type_filter,
        const std::string& keyword,
        const std::string& creator_id);

    // 计数（与 list 相同筛选条件）
    common::result::Result<int> count(
        const std::string& type_filter,
        const std::string& keyword,
        const std::string& creator_id);
};

}  // namespace taskflow::scheduler::dao
