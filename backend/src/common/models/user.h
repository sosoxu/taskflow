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

    static User fromRow(const pqxx::row& row) {
        User user;
        user.id = row["id"].as<std::string>();
        user.username = row["username"].as<std::string>();
        user.password_hash = row["password_hash"].as<std::string>();
        user.role = row["role"].as<std::string>();
        user.created_at = row["created_at"].as<std::string>();
        user.updated_at = row["updated_at"].as<std::string>();
        return user;
    }

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"username", username},
            {"password_hash", password_hash},
            {"role", role},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }

    nlohmann::json toSafeJson() const {
        return nlohmann::json{
            {"id", id},
            {"username", username},
            {"password_hash", "***"},
            {"role", role},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

} // namespace taskflow::common::models
