#include "scheduler/dao/user_dao.h"

#include "common/database/database_manager.h"
#include "common/util/uuid.h"

namespace taskflow::scheduler::dao {

common::result::Result<std::string> UserDao::create(
    const std::string& username,
    const std::string& password_hash,
    const std::string& role) {

    auto id = common::util::generateUuid();

    auto result = common::database::DatabaseManager::instance().withTransaction<std::string>(
        [&](pqxx::work& txn) -> common::result::Result<std::string> {
            auto res = txn.exec_params(
                "INSERT INTO users (id, username, password_hash, role) "
                "VALUES ($1, $2, $3, $4) RETURNING id",
                id, username, password_hash, role);

            if (res.empty()) {
                return common::result::Result<std::string>::failure("创建用户失败");
            }

            return std::string(res[0][0].as<std::string>());
        });

    return result;
}

common::result::Result<common::models::User> UserDao::findById(const std::string& id) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::User>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::User> {
            auto res = txn.exec_params(
                "SELECT * FROM users WHERE id = $1",
                id);

            if (res.empty()) {
                return common::result::Result<common::models::User>::failure("用户不存在");
            }

            return common::models::User::fromRow(res[0]);
        });
}

common::result::Result<common::models::User> UserDao::findByUsername(const std::string& username) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::User>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::User> {
            auto res = txn.exec_params(
                "SELECT * FROM users WHERE username = $1",
                username);

            if (res.empty()) {
                return common::result::Result<common::models::User>::failure("用户不存在");
            }

            return common::models::User::fromRow(res[0]);
        });
}

common::result::Result<void> UserDao::updateRole(const std::string& id, const std::string& role) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE users SET role = $1, updated_at = NOW() WHERE id = $2",
                role, id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("用户不存在，更新角色失败");
            }

            return common::result::Result<void>();
        });
}

common::result::Result<std::vector<common::models::User>> UserDao::list(int offset, int limit) {
    return common::database::DatabaseManager::instance().withReadTransaction<std::vector<common::models::User>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::vector<common::models::User>> {
            auto res = txn.exec_params(
                "SELECT * FROM users ORDER BY created_at DESC LIMIT $1 OFFSET $2",
                limit, offset);

            std::vector<common::models::User> users;
            for (const auto& row : res) {
                users.push_back(common::models::User::fromRow(row));
            }

            return users;
        });
}

common::result::Result<void> UserDao::remove(const std::string& id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "DELETE FROM users WHERE id = $1",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("用户不存在，删除失败");
            }

            return common::result::Result<void>();
        });
}

}  // namespace taskflow::scheduler::dao
