#include "scheduler/dao/task_dao.h"

#include "common/database/database_manager.h"
#include "common/util/uuid.h"

namespace taskflow::scheduler::dao {

common::result::Result<std::string> TaskDao::create(
    const std::string& name,
    const std::string& type,
    const nlohmann::json& config_json,
    const std::string& description,
    int timeout,
    int max_retries,
    int retry_interval,
    const nlohmann::json& resource_tags,
    const nlohmann::json& parameters_json,
    const std::string& creator_id) {

    auto id = common::util::generateUuid();

    auto result = common::database::DatabaseManager::instance().withTransaction<std::string>(
        [&](pqxx::work& txn) -> common::result::Result<std::string> {
            auto res = txn.exec_params(
                "INSERT INTO tasks (id, name, type, config_json, description, "
                "timeout, max_retries, retry_interval, resource_tags, parameters_json, creator_id) "
                "VALUES ($1, $2, $3, $4::jsonb, $5, $6, $7, $8, $9::jsonb, $10::jsonb, $11) "
                "RETURNING id",
                id, name, type, config_json.dump(), description,
                timeout, max_retries, retry_interval,
                resource_tags.dump(), parameters_json.dump(), creator_id);

            if (res.empty()) {
                return common::result::Result<std::string>::failure("创建任务失败");
            }

            return std::string(res[0][0].as<std::string>());
        });

    return result;
}

common::result::Result<common::models::Task> TaskDao::findById(const std::string& id) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::Task>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::Task> {
            auto res = txn.exec_params(
                "SELECT * FROM tasks WHERE id = $1",
                id);

            if (res.empty()) {
                return common::result::Result<common::models::Task>::failure("任务不存在");
            }

            return common::models::Task::fromRow(res[0]);
        });
}

common::result::Result<common::models::Task> TaskDao::findByName(const std::string& name) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::Task>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::Task> {
            auto res = txn.exec_params(
                "SELECT * FROM tasks WHERE name = $1 AND deleted = false",
                name);

            if (res.empty()) {
                return common::result::Result<common::models::Task>::failure("任务不存在");
            }

            return common::models::Task::fromRow(res[0]);
        });
}

common::result::Result<void> TaskDao::update(
    const std::string& id,
    const std::string& name,
    const std::string& type,
    const nlohmann::json& config_json,
    const std::string& description,
    int timeout,
    int max_retries,
    int retry_interval,
    const nlohmann::json& resource_tags) {

    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE tasks SET name = $1, type = $2, config_json = $3::jsonb, "
                "description = $4, timeout = $5, max_retries = $6, "
                "retry_interval = $7, resource_tags = $8::jsonb, "
                "version = version + 1, updated_at = NOW() "
                "WHERE id = $9 AND deleted = false",
                name, type, config_json.dump(), description,
                timeout, max_retries, retry_interval,
                resource_tags.dump(), id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("任务不存在或已删除，更新失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> TaskDao::softDelete(const std::string& id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE tasks SET deleted = true, updated_at = NOW() WHERE id = $1",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("任务不存在，软删除失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<std::vector<common::models::Task>> TaskDao::list(
    int offset, int limit,
    const std::string& type_filter,
    const std::string& keyword,
    const std::string& creator_id) {

    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::Task>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::Task>> {
            std::string sql = "SELECT * FROM tasks WHERE deleted = false";
            int param_idx = 1;
            std::vector<std::string> params;

            if (!type_filter.empty()) {
                sql += " AND type = $" + std::to_string(param_idx++);
                params.push_back(type_filter);
            }

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

            // Build parameter list for exec_params
            // We need to pass parameters individually, so we handle the known max count
            // Max params: type_filter, keyword, creator_id, limit, offset = 5
            pqxx::result res;
            int pcount = params.size();

            if (pcount == 0) {
                res = txn.exec_params(sql, limit, offset);
            } else if (pcount == 1) {
                res = txn.exec_params(sql, params[0], limit, offset);
            } else if (pcount == 2) {
                res = txn.exec_params(sql, params[0], params[1], limit, offset);
            } else if (pcount == 3) {
                res = txn.exec_params(sql, params[0], params[1], params[2], limit, offset);
            }

            std::vector<common::models::Task> tasks;
            for (const auto& row : res) {
                tasks.push_back(common::models::Task::fromRow(row));
            }

            return tasks;
        });
}

}  // namespace taskflow::scheduler::dao
