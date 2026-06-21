#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace taskflow::common::models {

struct User {
    std::string id;
    std::string username;
    std::string password_hash;
    std::string role;  // admin, operator, viewer
    std::string created_at;
    std::string updated_at;
    std::string deleted_at;

    static User fromRow(const pqxx::row& row) {
        User user;
        user.id = row["id"].as<std::string>();
        user.username = row["username"].as<std::string>();
        user.password_hash = row["password_hash"].as<std::string>();
        user.role = row["role"].as<std::string>();
        user.created_at = row["created_at"].as<std::string>();
        user.updated_at = row["updated_at"].as<std::string>();
        user.deleted_at = row["deleted_at"].is_null() ? "" : row["deleted_at"].as<std::string>();
        return user;
    }

    nlohmann::json toJson() const {
        // Fix #289: 不再输出 password_hash，避免误用导致密码哈希泄露
        // 如需包含密码哈希（如 DAO 层内部使用），请直接访问成员变量
        return nlohmann::json{
            {"id", id},
            {"username", username},
            {"role", role},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }

    nlohmann::json toSafeJson() const {
        return nlohmann::json{
            {"id", id},
            {"username", username},
            {"role", role},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

} // namespace taskflow::common::models
