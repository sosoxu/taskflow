#include "scheduler/dao/task_instance_dao.h"

#include "common/database/database_manager.h"
#include "common/util/uuid.h"

namespace taskflow::scheduler::dao {

common::result::Result<std::string> TaskInstanceDao::create(
    const std::string& workflow_instance_id,
    const std::string& task_id,
    int task_version,
    const std::string& task_name,
    const std::string& node_id) {

    auto id = common::util::generateUuid();

    auto result = common::database::DatabaseManager::instance().withTransaction<std::string>(
        [&](pqxx::work& txn) -> common::result::Result<std::string> {
            auto res = txn.exec_params(
                "INSERT INTO task_instances "
                "(id, workflow_instance_id, task_id, task_version, task_name, node_id, status) "
                "VALUES ($1, $2, $3, $4, $5, $6, 'PENDING') "
                "RETURNING id",
                id, workflow_instance_id, task_id, task_version, task_name, node_id);

            if (res.empty()) {
                return common::result::Result<std::string>::failure("创建任务实例失败");
            }

            return std::string(res[0][0].as<std::string>());
        });

    return result;
}

common::result::Result<common::models::TaskInstance> TaskInstanceDao::findById(const std::string& id) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::TaskInstance>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::TaskInstance> {
            auto res = txn.exec_params(
                "SELECT * FROM task_instances WHERE id = $1",
                id);

            if (res.empty()) {
                return common::result::Result<common::models::TaskInstance>::failure("任务实例不存在");
            }

            return common::models::TaskInstance::fromRow(res[0]);
        });
}

common::result::Result<void> TaskInstanceDao::updateStatus(const std::string& id,
                                                            const std::string& status) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE task_instances SET status = $1 WHERE id = $2",
                status, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("任务实例不存在，更新状态失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> TaskInstanceDao::dispatch(const std::string& id,
                                                        const std::string& worker_id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            // Fix #156: Only dispatch if the task is still PENDING. This prevents
            // racing with cancelInstance/retryTask which may have changed the
            // status between DagDriver's snapshot and this dispatch.
            auto res = txn.exec_params(
                "UPDATE task_instances SET status = 'DISPATCHED', worker_id = $1 "
                "WHERE id = $2 AND status = 'PENDING'",
                worker_id, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure(
                    "任务实例不存在或状态已变更（非 PENDING），调度失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> TaskInstanceDao::markRunning(const std::string& id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            // Fix #183: Only allow transition to RUNNING from DISPATCHED. Without
            // this guard, a racing cancelInstance (which sets CANCELLED) can be
            // overwritten by a late markRunning, resurrecting a cancelled task.
            auto res = txn.exec_params(
                "UPDATE task_instances SET status = 'RUNNING', started_at = NOW() "
                "WHERE id = $1 AND status = 'DISPATCHED'",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure(
                    "任务状态已变更（非 DISPATCHED），标记运行失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> TaskInstanceDao::markFinished(const std::string& id,
                                                            const std::string& status,
                                                            int exit_code,
                                                            const std::string& error_message) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            // Fix #184: Only allow finishing tasks in DISPATCHED or RUNNING
            // state. This prevents a late worker ReportTaskResult from
            // overwriting a PENDING status set by resetForRetry (which would
            // clobber the retry with a stale SUCCESS). Callers that need to
            // transition a PENDING task to a terminal state (e.g. cancelInstance
            // cancelling a PENDING task, or DagDriver failing a PENDING task
            // that cannot be dispatched) must use updateStatus instead.
            auto res = txn.exec_params(
                "UPDATE task_instances SET status = $1, finished_at = NOW(), "
                "exit_code = $2, error_message = $3 WHERE id = $4 "
                "AND status IN ('DISPATCHED', 'RUNNING')",
                status, exit_code,
                error_message.empty() ? nullptr : error_message.c_str(),
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure(
                    "任务状态已变更（非 DISPATCHED/RUNNING），标记完成失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<std::vector<common::models::TaskInstance>> TaskInstanceDao::listByWorkflowInstance(
    const std::string& workflow_instance_id) {

    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::TaskInstance>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::TaskInstance>> {
            auto res = txn.exec_params(
                "SELECT * FROM task_instances WHERE workflow_instance_id = $1 "
                "ORDER BY created_at ASC",
                workflow_instance_id);

            std::vector<common::models::TaskInstance> instances;
            for (const auto& row : res) {
                instances.push_back(common::models::TaskInstance::fromRow(row));
            }

            return instances;
        });
}

common::result::Result<std::vector<common::models::TaskInstance>> TaskInstanceDao::listByWorkerId(
    const std::string& worker_id) {

    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::TaskInstance>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::TaskInstance>> {
            auto res = txn.exec_params(
                "SELECT * FROM task_instances WHERE worker_id = $1 "
                "AND status IN ('RUNNING', 'DISPATCHED') "
                "ORDER BY created_at ASC",
                worker_id);

            std::vector<common::models::TaskInstance> instances;
            for (const auto& row : res) {
                instances.push_back(common::models::TaskInstance::fromRow(row));
            }

            return instances;
        });
}

common::result::Result<std::vector<std::string>> TaskInstanceDao::batchCreate(
    const std::string& workflow_instance_id,
    const std::vector<std::tuple<std::string, std::string, int, std::string>>& tasks) {

    return common::database::DatabaseManager::instance().withTransaction<std::vector<std::string>>(
        [&](pqxx::work& txn) -> common::result::Result<std::vector<std::string>> {
            std::vector<std::string> ids;

            for (const auto& [task_id, task_name, task_version, node_id] : tasks) {
                auto id = common::util::generateUuid();

                auto res = txn.exec_params(
                    "INSERT INTO task_instances "
                    "(id, workflow_instance_id, task_id, task_version, task_name, node_id, status) "
                    "VALUES ($1, $2, $3, $4, $5, $6, 'PENDING') "
                    "RETURNING id",
                    id, workflow_instance_id, task_id, task_version, task_name, node_id);

                if (res.empty()) {
                    return common::result::Result<std::vector<std::string>>::failure("批量创建任务实例失败");
                }

                ids.push_back(res[0][0].as<std::string>());
            }

            return ids;
        });
}

common::result::Result<void> TaskInstanceDao::resetForRetry(const std::string& id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE task_instances SET status = 'PENDING', "
                "retry_count = retry_count + 1, "
                "worker_id = NULL, started_at = NULL, finished_at = NULL, "
                "exit_code = NULL, error_message = NULL "
                "WHERE id = $1",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("任务实例不存在，重置重试失败");
            }

            return common::result::Result<void>();
        });
}

}  // namespace taskflow::scheduler::dao
