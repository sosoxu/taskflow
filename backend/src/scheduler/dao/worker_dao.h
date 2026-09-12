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

    // 原子递增运行任务数（Fix #154: dispatchTask 时调用，避免与心跳/上报竞态）
    common::result::Result<void> incrementRunningTasks(const std::string& id);

    // Fix #338: 原子完成"worker 心跳超时下线"处置——worker 置 offline、
    // running_tasks 归零、其 RUNNING/DISPATCHED 任务实例置 NODE_OFFLINE，
    // 三步在同一事务内完成。此前为三个独立事务，中途失败会产生
    // "worker 已 offline 但实例仍 RUNNING"的不一致状态。
    // 仅当 worker 当前为 online 时执行（幂等：并发检查器重复触发直接返回 0）。
    // 返回被置为 NODE_OFFLINE 的任务实例数。
    common::result::Result<int> markOfflineAtomic(
        const std::string& worker_id,
        const std::string& error_message);
};

}  // namespace taskflow::scheduler::dao
