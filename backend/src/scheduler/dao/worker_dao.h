#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "common/models/worker_info.h"

namespace taskflow::scheduler::dao {

class WorkerDao {
public:
    // 注册新 Worker，返回 worker_id
    common::result::Result<std::string> create(
        const std::string& name,
        const std::string& address,
        int max_tasks,
        const nlohmann::json& resource_tags);

    // 按 ID 查找
    common::result::Result<common::models::WorkerInfo> findById(const std::string& id);

    // 按名称查找
    common::result::Result<common::models::WorkerInfo> findByName(const std::string& name);

    // 更新心跳和资源信息
    common::result::Result<void> updateHeartbeat(
        const std::string& id,
        double cpu_usage,
        double memory_usage,
        int running_tasks);

    // 更新状态（online/offline）
    common::result::Result<void> updateStatus(const std::string& id,
                                               const std::string& status);

    // 查询所有在线 Worker
    common::result::Result<std::vector<common::models::WorkerInfo>> listOnline();

    // 查询所有 Worker
    common::result::Result<std::vector<common::models::WorkerInfo>> listAll();

    // 更新运行任务数
    common::result::Result<void> updateRunningTasks(const std::string& id, int running_tasks);

    // 递减运行任务数（Fix #121: 任务完成时调用，避免负载均衡策略失效）
    // 使用 SQL GREATEST(running_tasks - 1, 0) 避免负数
    common::result::Result<void> decrementRunningTasks(const std::string& id);
};

}  // namespace taskflow::scheduler::dao
