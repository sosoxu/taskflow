#include "scheduler/dao/cron_job_dao.h"

#include "common/database/database_manager.h"
#include "common/util/uuid.h"

namespace taskflow::scheduler::dao {

common::result::Result<std::string> CronJobDao::create(
    const std::string& workflow_id,
    const std::string& cron_expression,
    const std::string& next_trigger_time) {

    auto id = common::util::generateUuid();

    auto result = common::database::DatabaseManager::instance().withTransaction<std::string>(
        [&](pqxx::work& txn) -> common::result::Result<std::string> {
            pqxx::result res;
            if (next_trigger_time.empty()) {
                res = txn.exec_params(
                    "INSERT INTO cron_jobs (id, workflow_id, cron_expression, enabled, next_trigger_time) "
                    "VALUES ($1, $2, $3, true, NULL) RETURNING id",
                    id, workflow_id, cron_expression);
            } else {
                res = txn.exec_params(
                    "INSERT INTO cron_jobs (id, workflow_id, cron_expression, enabled, next_trigger_time) "
                    "VALUES ($1, $2, $3, true, $4::timestamptz) RETURNING id",
                    id, workflow_id, cron_expression, next_trigger_time);
            }

            if (res.empty()) {
                return common::result::Result<std::string>::failure("创建 CronJob 失败");
            }

            return std::string(res[0][0].as<std::string>());
        });

    return result;
}

common::result::Result<common::models::CronJob> CronJobDao::findById(const std::string& id) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::CronJob>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::CronJob> {
            auto res = txn.exec_params(
                "SELECT * FROM cron_jobs WHERE id = $1", id);

            if (res.empty()) {
                return common::result::Result<common::models::CronJob>::failure("CronJob 不存在");
            }

            return common::models::CronJob::fromRow(res[0]);
        });
}

common::result::Result<common::models::CronJob> CronJobDao::findByWorkflowId(const std::string& workflow_id) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::CronJob>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::CronJob> {
            auto res = txn.exec_params(
                "SELECT * FROM cron_jobs WHERE workflow_id = $1", workflow_id);

            if (res.empty()) {
                return common::result::Result<common::models::CronJob>::failure("CronJob 不存在");
            }

            return common::models::CronJob::fromRow(res[0]);
        });
}

common::result::Result<void> CronJobDao::update(
    const std::string& id,
    const std::string& cron_expression,
    bool enabled) {

    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE cron_jobs SET cron_expression = $1, enabled = $2, "
                "updated_at = NOW() WHERE id = $3",
                cron_expression, enabled, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("CronJob 不存在，更新失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> CronJobDao::toggleEnabled(const std::string& id, bool enabled) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE cron_jobs SET enabled = $1, updated_at = NOW() WHERE id = $2",
                enabled, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("CronJob 不存在，切换启用状态失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<void> CronJobDao::updateNextTriggerTime(
    const std::string& id,
    const std::string& next_trigger_time) {

    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            pqxx::result res;

            if (next_trigger_time.empty()) {
                res = txn.exec_params(
                    "UPDATE cron_jobs SET next_trigger_time = NULL, "
                    "updated_at = NOW() WHERE id = $1",
                    id);
            } else {
                res = txn.exec_params(
                    "UPDATE cron_jobs SET next_trigger_time = $1::timestamptz, "
                    "updated_at = NOW() WHERE id = $2",
                    next_trigger_time, id);
            }

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("CronJob 不存在，更新下次触发时间失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<bool> CronJobDao::tryClaimTrigger(
    const std::string& id,
    const std::string& old_trigger_time,
    const std::string& new_trigger_time) {

    return common::database::DatabaseManager::instance().withTransaction<bool>(
        [&](pqxx::work& txn) -> common::result::Result<bool> {
            // 乐观锁：仅当 next_trigger_time 仍等于 old_trigger_time 时才更新。
            // 双 leader 并发时，只有一个 UPDATE 会命中（affected_rows=1），
            // 另一个得到 0 行，从而避免重复创建 WorkflowInstance。
            auto res = txn.exec_params(
                "UPDATE cron_jobs SET next_trigger_time = $1::timestamptz, "
                "updated_at = NOW() WHERE id = $2 "
                "AND next_trigger_time = $3::timestamptz",
                new_trigger_time, id, old_trigger_time);

            return res.affected_rows() > 0;
        });
}

common::result::Result<std::vector<common::models::CronJob>> CronJobDao::listDue(
    const std::string& current_time) {

    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::CronJob>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::CronJob>> {
            auto res = txn.exec_params(
                "SELECT * FROM cron_jobs WHERE enabled = true "
                "AND next_trigger_time <= $1::timestamptz "
                "ORDER BY next_trigger_time",
                current_time);

            std::vector<common::models::CronJob> jobs;
            for (const auto& row : res) {
                jobs.push_back(common::models::CronJob::fromRow(row));
            }

            return jobs;
        });
}

common::result::Result<std::vector<common::models::CronJob>> CronJobDao::listNullTriggerTime() {

    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::CronJob>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::CronJob>> {
            auto res = txn.exec_params(
                "SELECT * FROM cron_jobs WHERE enabled = true "
                "AND next_trigger_time IS NULL");

            std::vector<common::models::CronJob> jobs;
            for (const auto& row : res) {
                jobs.push_back(common::models::CronJob::fromRow(row));
            }

            return jobs;
        });
}

common::result::Result<void> CronJobDao::disableByWorkflowId(const std::string& workflow_id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            txn.exec_params(
                "UPDATE cron_jobs SET enabled = false, updated_at = NOW() "
                "WHERE workflow_id = $1",
                workflow_id);

            return common::result::Result<void>();
        });
}

}  // namespace taskflow::scheduler::dao
