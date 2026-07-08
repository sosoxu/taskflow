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
                "SELECT * FROM users WHERE id = $1 AND deleted_at IS NULL",
                id);

            if (res.empty()) {
                return common::result::Result<common::models::User>::failure("用户不存在");
            }

            return common::models::User::fromRow(res[0]);
        });
}

common::result::Result<std::unordered_map<std::string, std::string>> UserDao::findByIds(
    const std::vector<std::string>& ids) {

    if (ids.empty()) {
        return std::unordered_map<std::string, std::string>{};
    }

    return common::database::DatabaseManager::instance().withReadTransaction<std::unordered_map<std::string, std::string>>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<std::unordered_map<std::string, std::string>> {
            // Build parameterized IN clause: WHERE id IN ($1, $2, ...)
            std::string sql = "SELECT id, username FROM users WHERE id IN (";
            for (size_t i = 0; i < ids.size(); ++i) {
                if (i > 0) sql += ", ";
                sql += "$" + std::to_string(i + 1);
            }
            sql += ")";

            // pqxx doesn't support variadic exec_params with vector,
            // so we use exec with escaped values for this safe UUID list.
            // Alternatively, build the query string with params.
            std::unordered_map<std::string, std::string> result;
            pqxx::result res;
            switch (ids.size()) {
                case 1: res = txn.exec_params(sql, ids[0]); break;
                case 2: res = txn.exec_params(sql, ids[0], ids[1]); break;
                case 3: res = txn.exec_params(sql, ids[0], ids[1], ids[2]); break;
                case 4: res = txn.exec_params(sql, ids[0], ids[1], ids[2], ids[3]); break;
                case 5: res = txn.exec_params(sql, ids[0], ids[1], ids[2], ids[3], ids[4]); break;
                case 6: res = txn.exec_params(sql, ids[0], ids[1], ids[2], ids[3], ids[4], ids[5]); break;
                case 7: res = txn.exec_params(sql, ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6]); break;
                case 8: res = txn.exec_params(sql, ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7]); break;
                case 9: res = txn.exec_params(sql, ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7], ids[8]); break;
                case 10: res = txn.exec_params(sql, ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7], ids[8], ids[9]); break;
                default: {
                    // For larger sets, use a temporary table approach
                    // Simple fallback: query one by one (unlikely to have >10 unique creators per page)
                    for (const auto& uid : ids) {
                        auto r = txn.exec_params("SELECT id, username FROM users WHERE id = $1", uid);
                        if (!r.empty()) {
                            result[r[0][0].as<std::string>()] = r[0][1].as<std::string>();
                        }
                    }
                    return result;
                }
            }

            for (const auto& row : res) {
                result[row[0].as<std::string>()] = row[1].as<std::string>();
            }
            return result;
        });
}

common::result::Result<common::models::User> UserDao::findByUsername(const std::string& username) {
    return common::database::DatabaseManager::instance().withReadTransaction<common::models::User>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<common::models::User> {
            auto res = txn.exec_params(
                "SELECT * FROM users WHERE username = $1 AND deleted_at IS NULL",
                username);

            if (res.empty()) {
                return common::result::Result<common::models::User>::failure("用户不存在");
            }

            return common::models::User::fromRow(res[0]);
        });
}

common::result::Result<common::models::User> UserDao::findByUsernameIncludeDeleted(const std::string& username) {
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
                "UPDATE users SET role = $1, updated_at = NOW() "
                "WHERE id = $2 AND deleted_at IS NULL",
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
                "SELECT * FROM users WHERE deleted_at IS NULL ORDER BY created_at DESC LIMIT $1 OFFSET $2",
                limit, offset);

            std::vector<common::models::User> users;
            for (const auto& row : res) {
                users.push_back(common::models::User::fromRow(row));
            }

            return users;
        });
}

common::result::Result<int> UserDao::count() {
    return common::database::DatabaseManager::instance().withReadTransaction<int>(
        [&](pqxx::nontransaction& txn) -> common::result::Result<int> {
            auto res = txn.exec_params(
                "SELECT COUNT(*) FROM users WHERE deleted_at IS NULL");
            return static_cast<int>(res[0][0].as<long>());
        });
}

common::result::Result<void> UserDao::softDelete(const std::string& id) {
    return common::database::DatabaseManager::instance().withTransaction<void>(
        [&](pqxx::work& txn) -> common::result::Result<void> {
            auto res = txn.exec_params(
                "UPDATE users SET deleted_at = NOW(), updated_at = NOW() WHERE id = $1 AND deleted_at IS NULL",
                id);

            if (res.affected_rows() == 0) {
                return common::result::Result<void>::failure("用户不存在或已删除");
            }

            return common::result::Result<void>();
        });
}

}  // namespace taskflow::scheduler::dao
