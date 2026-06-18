#include "scheduler/dao/workflow_instance_dao.h"

#include "common/database/database_manager.h"
#include "common/util/uuid.h"

namespace taskflow::scheduler::dao {

common::result::Result<std::string> WorkflowInstanceDao::create(
    const std::string& workflow_id,
    int workflow_version,
    const std::string& trigger_type,
    const std::string& creator_id,
    const nlohmann::json& param_overrides) {

    auto id = common::util::generateUuid();

    auto result = common::database::DatabaseManager::instance().withTransaction<std::string>(
        [&](pqxx::work& txn) -> common::result::Result<std::string> {
            auto res = txn.exec_params(
                "INSERT INTO workflow_instances "
                "(id, workflow_id, workflow_version, status, trigger_type, param_overrides, creator_id) "
                "VALUES ($1, $2, $3, 'PENDING', $4, $5::jsonb, $6) "
                "RETURNING id",
                id, workflow_id, workflow_version, trigger_type,
                param_overrides.dump(), creator_id);

            if (res.empty()) {
                return common::result::Result<std::string>::failure("创建工作流实例失败");
            }

            return std::string(res[0][0].as<std::string>());
        });

    return result;
}

common::result::Result<common::models::WorkflowInstance> WorkflowInstanceDao::findById(const std::string& id) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::WorkflowInstance>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::WorkflowInstance> {
            auto res = txn.exec_params(
                "SELECT * FROM workflow_instances WHERE id = $1",
                id);

            if (res.empty()) {
                return common::result::Result<common::models::WorkflowInstance>::failure("工作流实例不存在");
            }

            return common::models::WorkflowInstance::fromRow(res[0]);
        });
}

common::result::Result<void> WorkflowInstanceDao::updateStatus(const std::string& id,
                                                                const std::string& status) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workflow_instances SET status = $1 WHERE id = $2",
                status, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("工作流实例不存在，更新状态失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> WorkflowInstanceDao::markRunning(const std::string& id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workflow_instances SET status = 'RUNNING', started_at = NOW() WHERE id = $1",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("工作流实例不存在，标记运行失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> WorkflowInstanceDao::markFinished(const std::string& id,
                                                                const std::string& status) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workflow_instances SET status = $1, finished_at = NOW() WHERE id = $2",
                status, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("工作流实例不存在，标记完成失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<std::vector<common::models::WorkflowInstance>> WorkflowInstanceDao::listByWorkflow(
    const std::string& workflow_id, int offset, int limit) {

    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::WorkflowInstance>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::WorkflowInstance>> {
            auto res = txn.exec_params(
                "SELECT * FROM workflow_instances WHERE workflow_id = $1 "
                "ORDER BY created_at DESC LIMIT $2 OFFSET $3",
                workflow_id, limit, offset);

            std::vector<common::models::WorkflowInstance> instances;
            for (const auto& row : res) {
                instances.push_back(common::models::WorkflowInstance::fromRow(row));
            }

            return instances;
        });
}

common::result::Result<int> WorkflowInstanceDao::countByWorkflow(const std::string& workflow_id) {
    return common::database::DatabaseManager::instance().withReadTransaction<int>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<int> {
            auto row = txn.exec_params(
                "SELECT COUNT(*) FROM workflow_instances WHERE workflow_id = $1",
                workflow_id);
            int total = row[0][0].as<int>();
            return total;
        });
}

common::result::Result<std::vector<common::models::WorkflowInstance>> WorkflowInstanceDao::listActive() {
    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::WorkflowInstance>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::WorkflowInstance>> {
            auto res = txn.exec_params(
                "SELECT * FROM workflow_instances WHERE status IN ('PENDING', 'RUNNING') "
                "ORDER BY created_at ASC");

            std::vector<common::models::WorkflowInstance> instances;
            for (const auto& row : res) {
                instances.push_back(common::models::WorkflowInstance::fromRow(row));
            }

            return instances;
        });
}

common::result::Result<std::vector<common::models::WorkflowInstance>> WorkflowInstanceDao::listAll(int offset, int limit) {
    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::WorkflowInstance>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::WorkflowInstance>> {
            auto res = txn.exec_params(
                "SELECT * FROM workflow_instances ORDER BY created_at DESC LIMIT $1 OFFSET $2",
                limit, offset);

            std::vector<common::models::WorkflowInstance> instances;
            for (const auto& row : res) {
                instances.push_back(common::models::WorkflowInstance::fromRow(row));
            }

            return instances;
        });
}

common::result::Result<int> WorkflowInstanceDao::countAll() {
    return common::database::DatabaseManager::instance().withReadTransaction<int>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<int> {
            auto row = txn.exec1("SELECT COUNT(*) FROM workflow_instances");
            int total = row[0].as<int>();
            return total;
        });
}

common::result::Result<std::vector<common::models::WorkflowInstance>> WorkflowInstanceDao::listByCreator(
    const std::string& creator_id, int offset, int limit) {
    // Fix #157: Push creator_id filter into SQL so pagination is correct.
    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::WorkflowInstance>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::WorkflowInstance>> {
            auto res = txn.exec_params(
                "SELECT * FROM workflow_instances WHERE creator_id = $1 "
                "ORDER BY created_at DESC LIMIT $2 OFFSET $3",
                creator_id, limit, offset);

            std::vector<common::models::WorkflowInstance> instances;
            for (const auto& row : res) {
                instances.push_back(common::models::WorkflowInstance::fromRow(row));
            }

            return instances;
        });
}

common::result::Result<int> WorkflowInstanceDao::countByCreator(const std::string& creator_id) {
    return common::database::DatabaseManager::instance().withReadTransaction<int>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<int> {
            auto result = txn.exec_params(
                "SELECT COUNT(*) FROM workflow_instances WHERE creator_id = $1",
                creator_id);
            // Fix: exec_params returns a pqxx::result; access the first row's
            // first field (not row.as<int>() which yields std::tuple<int>).
            int total = result[0][0].as<int>();
            return total;
        });
}

}  // namespace taskflow::scheduler::dao
