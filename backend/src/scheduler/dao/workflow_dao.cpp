#include "scheduler/dao/workflow_dao.h"

#include "common/database/database_manager.h"
#include "common/util/uuid.h"

namespace taskflow::scheduler::dao {

common::result::Result<std::string> WorkflowDao::create(
    const std::string& name,
    const std::string& description,
    const nlohmann::json& dag_json,
    const std::string& schedule_strategy,
    const std::string& target_worker_id,
    const std::string& cron_expression,
    bool cron_enabled,
    const std::string& creator_id) {

    auto id = common::util::generateUuid();

    auto result = common::database::DatabaseManager::instance().withTransaction<std::string>(
        [&](pqxx::work& txn) -> common::result::Result<std::string> {
            auto res = txn.exec_params(
                "INSERT INTO workflows (id, name, description, dag_json, schedule_strategy, "
                "target_worker_id, cron_expression, cron_enabled, creator_id) "
                "VALUES ($1, $2, $3, $4::jsonb, $5, $6, $7, $8, $9) "
                "RETURNING id",
                id, name, description, dag_json.dump(), schedule_strategy,
                target_worker_id.empty() ? nullptr : target_worker_id.c_str(),
                cron_expression.empty() ? nullptr : cron_expression.c_str(),
                cron_enabled, creator_id);

            if (res.empty()) {
                return common::result::Result<std::string>::failure("创建工作流失败");
            }

            return std::string(res[0][0].as<std::string>());
        });

    return result;
}

common::result::Result<common::models::Workflow> WorkflowDao::findById(const std::string& id) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::Workflow>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::Workflow> {
            auto res = txn.exec_params(
                "SELECT * FROM workflows WHERE id = $1",
                id);

            if (res.empty()) {
                return common::result::Result<common::models::Workflow>::failure("工作流不存在");
            }

            return common::models::Workflow::fromRow(res[0]);
        });
}

common::result::Result<common::models::Workflow> WorkflowDao::findByName(const std::string& name) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::Workflow>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::Workflow> {
            auto res = txn.exec_params(
                "SELECT * FROM workflows WHERE name = $1 AND deleted = false",
                name);

            if (res.empty()) {
                return common::result::Result<common::models::Workflow>::failure("工作流不存在");
            }

            return common::models::Workflow::fromRow(res[0]);
        });
}

common::result::Result<void> WorkflowDao::update(
    const std::string& id,
    const std::string& name,
    const std::string& description,
    const nlohmann::json& dag_json,
    const std::string& schedule_strategy,
    const std::string& target_worker_id,
    const std::string& cron_expression,
    bool cron_enabled) {

    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workflows SET name = $1, description = $2, dag_json = $3::jsonb, "
                "schedule_strategy = $4, target_worker_id = $5, cron_expression = $6, "
                "cron_enabled = $7, version = version + 1, updated_at = NOW() "
                "WHERE id = $8 AND deleted = false",
                name, description, dag_json.dump(), schedule_strategy,
                target_worker_id.empty() ? nullptr : target_worker_id.c_str(),
                cron_expression.empty() ? nullptr : cron_expression.c_str(),
                cron_enabled, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("工作流不存在或已删除，更新失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> WorkflowDao::softDelete(const std::string& id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE workflows SET deleted = true, updated_at = NOW() WHERE id = $1",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("工作流不存在，软删除失败");
            }

            // 同时禁用关联的 cron 任务
            txn.exec_params(
                "UPDATE cron_jobs SET enabled = false WHERE workflow_id = $1",
                id);

            return common::result::Result<void>();
        });
}

common::result::Result<std::vector<common::models::Workflow>> WorkflowDao::list(
    int offset, int limit,
    const std::string& keyword,
    const std::string& creator_id) {

    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::Workflow>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::Workflow>> {
            std::string sql = "SELECT * FROM workflows WHERE deleted = false";
            int param_idx = 1;
            std::vector<std::string> params;

            if (!keyword.empty()) {
                sql += " AND name ILIKE $" + std::to_string(param_idx++);
                params.push_back("%" + keyword + "%");
            }

            if (!creator_id.empty()) {
                sql += " AND creator_id = $" + std::to_string(param_idx++);
                params.push_back(creator_id);
            }

            sql += " ORDER BY created_at DESC LIMIT $" + std::to_string(param_idx++);
            sql += " OFFSET $" + std::to_string(param_idx++);

            pqxx::result res;
            int pcount = params.size();

            if (pcount == 0) {
                res = txn.exec_params(sql, limit, offset);
            } else if (pcount == 1) {
                res = txn.exec_params(sql, params[0], limit, offset);
            } else if (pcount == 2) {
                res = txn.exec_params(sql, params[0], params[1], limit, offset);
            }

            std::vector<common::models::Workflow> workflows;
            for (const auto& row : res) {
                workflows.push_back(common::models::Workflow::fromRow(row));
            }

            return workflows;
        });
}

}  // namespace taskflow::scheduler::dao
