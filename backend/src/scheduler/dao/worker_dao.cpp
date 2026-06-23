#include "scheduler/dao/worker_dao.h"

#include "common/database/database_manager.h"
#include "common/util/uuid.h"

namespace taskflow::scheduler::dao {

common::result::Result<std::string> WorkerDao::create(
    const std::string& name,
    const std::string& address,
    int max_tasks,
    const nlohmann::json& resource_tags) {

    auto id = common::util::generateUuid();

    auto result = common::database::DatabaseManager::instance().withTransaction<std::string>(
        [&](pqxx::work& txn) -> common::result::Result<std::string> {
            // 先检查名称是否已存在
            auto check = txn.exec_params(
                "SELECT id FROM workers WHERE name = $1", name);

            if (!check.empty()) {
                // 名称已存在，更新现有记录
                // Fix #189: Reset running_tasks to 0 on re-registration. A worker
                // that restarts with the same name inherits the old record's
                // running_tasks counter, which is now stale (the restarted
                // worker has no running tasks). Without this reset,
                // LoadBalanceDispatcher sees inflated load and may starve the
                // worker.
                auto res = txn.exec_params(
                    "UPDATE workers SET address = $1, max_tasks = $2, "
                    "resource_tags = $3::jsonb, status = 'online', "
                    "running_tasks = 0, last_heartbeat = NOW() "
                    "WHERE name = $4 RETURNING id",
                    address, max_tasks, resource_tags.dump(), name);

                if (res.empty()) {
                    return common::result::Result<std::string>::failure("更新 Worker 失败");
                }

                return std::string(res[0][0].as<std::string>());
            }

            // 插入新记录
            auto res = txn.exec_params(
                "INSERT INTO workers (id, name, address, max_tasks, resource_tags, status) "
                "VALUES ($1, $2, $3, $4, $5::jsonb, 'online') RETURNING id",
                id, name, address, max_tasks, resource_tags.dump());

            if (res.empty()) {
                return common::result::Result<std::string>::failure("创建 Worker 失败");
            }

            return std::string(res[0][0].as<std::string>());
        });

    return result;
}

common::result::Result<common::models::WorkerInfo> WorkerDao::findById(const std::string& id) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::WorkerInfo>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::WorkerInfo> {
            auto res = txn.exec_params(
                "SELECT * FROM workers WHERE id = $1", id);

            if (res.empty()) {
                return common::result::Result<common::models::WorkerInfo>::failure("Worker 不存在");
            }

            return common::models::WorkerInfo::fromRow(res[0]);
        });
}

common::result::Result<common::models::WorkerInfo> WorkerDao::findByName(const std::string& name) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::WorkerInfo>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::WorkerInfo> {
            auto res = txn.exec_params(
                "SELECT * FROM workers WHERE name = $1", name);

            if (res.empty()) {
                return common::result::Result<common::models::WorkerInfo>::failure("Worker 不存在");
            }

            return common::models::WorkerInfo::fromRow(res[0]);
        });
}

common::result::Result<void> WorkerDao::updateHeartbeat(
    const std::string& id,
    double cpu_usage,
    double memory_usage,
    int running_tasks) {

    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            // Fix #315: Set status = 'online' when a heartbeat is received.
            // Without this, a worker marked 'offline' (e.g. after a scheduler
            // restart or transient network partition) can never recover its
            // 'online' status, because HeartbeatChecker only inspects workers
            // that are already 'online'. This left workers permanently offline
            // even though they were actively sending heartbeats, causing
            // DagDriver to find zero online workers and fail every task.
            auto res = txn.exec_params(
                "UPDATE workers SET cpu_usage = $1, memory_usage = $2, "
                "running_tasks = $3, last_heartbeat = NOW(), status = 'online' "
                "WHERE id = $4",
                cpu_usage, memory_usage, running_tasks, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("Worker 不存在，更新心跳失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> WorkerDao::updateStatus(const std::string& id,
                                                      const std::string& status) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workers SET status = $1 WHERE id = $2",
                status, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("Worker 不存在，更新状态失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<std::vector<common::models::WorkerInfo>> WorkerDao::listOnline() {
    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::WorkerInfo>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::WorkerInfo>> {
            auto res = txn.exec_params(
                "SELECT * FROM workers WHERE status = 'online' ORDER BY name");

            std::vector<common::models::WorkerInfo> workers;
            for (const auto& row : res) {
                workers.push_back(common::models::WorkerInfo::fromRow(row));
            }

            return workers;
        });
}

common::result::Result<std::vector<common::models::WorkerInfo>> WorkerDao::listAll() {
    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::WorkerInfo>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::WorkerInfo>> {
            auto res = txn.exec_params(
                "SELECT * FROM workers ORDER BY registered_at DESC");

            std::vector<common::models::WorkerInfo> workers;
            for (const auto& row : res) {
                workers.push_back(common::models::WorkerInfo::fromRow(row));
            }

            return workers;
        });
}

common::result::Result<void> WorkerDao::updateRunningTasks(const std::string& id, int running_tasks) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workers SET running_tasks = $1 WHERE id = $2",
                running_tasks, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("Worker 不存在，更新运行任务数失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> WorkerDao::decrementRunningTasks(const std::string& id) {
    // Fix #121: Atomically decrement running_tasks using SQL, avoiding negative values.
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workers SET running_tasks = GREATEST(running_tasks - 1, 0) "
                "WHERE id = $1",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("Worker 不存在，递减运行任务数失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> WorkerDao::incrementRunningTasks(const std::string& id) {
    // Fix #154: Atomically increment running_tasks using SQL to avoid the
    // read-modify-write race with HeartbeatChecker (updateHeartbeat) and
    // ReportTaskResult (decrementRunningTasks).
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workers SET running_tasks = running_tasks + 1 WHERE id = $1",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("Worker 不存在，递增运行任务数失败");
            }

            return common::result::Result<void>();
        });
}

}  // namespace taskflow::scheduler::dao
