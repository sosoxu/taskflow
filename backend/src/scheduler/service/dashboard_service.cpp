#include "scheduler/service/dashboard_service.h"

#include "common/database/database_manager.h"

namespace taskflow::scheduler::service {

DashboardService::DashboardService()
    : task_dao_(),
      workflow_dao_(),
      workflow_instance_dao_(),
      worker_dao_() {}

common::result::Result<nlohmann::json> DashboardService::getStats() {
    // Aggregate multiple metrics in one service call so the frontend can render
    // the dashboard with a single request (Fix #123).
    nlohmann::json stats = nlohmann::json::object();

    // Total tasks (not soft-deleted)
    auto tasks_count_result = task_dao_.count("", "", "");
    if (!tasks_count_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "统计任务总数失败: " + tasks_count_result.error());
    }
    stats["total_tasks"] = tasks_count_result.value();

    // Total workflows (not soft-deleted)
    auto workflows_count_result = workflow_dao_.count("", "");
    if (!workflows_count_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "统计工作流总数失败: " + workflows_count_result.error());
    }
    stats["total_workflows"] = workflows_count_result.value();

    // Online workers
    auto online_workers_result = worker_dao_.listOnline();
    if (!online_workers_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "统计在线 Worker 失败: " + online_workers_result.error());
    }
    stats["online_workers"] = static_cast<int>(online_workers_result.value().size());

    // Running instances, today's executions, success rate, and recent instances.
    // Computed in a single read transaction to keep the snapshot consistent.
    auto aggregate_result =
        common::database::DatabaseManager::instance()
            .withReadTransaction<nlohmann::json>(
                [&](pqxx::nontransaction& txn) -> common::result::Result<nlohmann::json> {
                    nlohmann::json agg = nlohmann::json::object();

                    // Running workflow instances
                    auto running_row = txn.exec_params(
                        "SELECT COUNT(*) FROM workflow_instances WHERE status = 'RUNNING'");
                    agg["running_instances"] = running_row[0][0].as<int>();

                    // Today's executions (created_at >= midnight today, server local time)
                    auto today_row = txn.exec_params(
                        "SELECT COUNT(*) FROM workflow_instances "
                        "WHERE created_at >= date_trunc('day', NOW())");
                    agg["today_executions"] = today_row[0][0].as<int>();

                    // Success rate over the last 24 hours (finished instances only)
                    auto rate_row = txn.exec_params(
                        "SELECT "
                        "  COUNT(*) FILTER (WHERE status IN ('SUCCESS','FAILED','CANCELLED')) AS finished, "
                        "  COUNT(*) FILTER (WHERE status = 'SUCCESS') AS success "
                        "FROM workflow_instances "
                        "WHERE finished_at >= NOW() - INTERVAL '24 hours'");
                    int finished = rate_row[0]["finished"].as<int>();
                    int success = rate_row[0]["success"].as<int>();
                    double success_rate = (finished > 0)
                        ? (static_cast<double>(success) / finished) * 100.0
                        : 0.0;
                    agg["success_rate"] = success_rate;

                    // Recent 10 instances
                    auto recent_res = txn.exec_params(
                        "SELECT id, workflow_id, status, trigger_type, created_at "
                        "FROM workflow_instances "
                        "ORDER BY created_at DESC LIMIT 10");
                    nlohmann::json recent_items = nlohmann::json::array();
                    for (const auto& row : recent_res) {
                        nlohmann::json item;
                        item["id"] = row["id"].as<std::string>();
                        item["workflow_id"] = row["workflow_id"].as<std::string>();
                        item["status"] = row["status"].as<std::string>();
                        item["trigger_type"] = row["trigger_type"].as<std::string>();
                        item["created_at"] = row["created_at"].as<std::string>();
                        recent_items.push_back(item);
                    }
                    agg["recent_instances"] = recent_items;

                    return agg;
                });

    if (!aggregate_result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "统计实例数据失败: " + aggregate_result.error());
    }

    // Merge aggregate fields into stats
    const auto& agg = aggregate_result.value();
    stats["running_instances"] = agg["running_instances"];
    stats["today_executions"] = agg["today_executions"];
    stats["success_rate"] = agg["success_rate"];
    stats["recent_instances"] = agg["recent_instances"];

    return stats;
}

}  // namespace taskflow::scheduler::service
