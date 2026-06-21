#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace taskflow::common::models {

struct Task {
    std::string id;
    std::string name;
    std::string type;  // command, script, sql
    nlohmann::json config_json;
    std::string description;
    int timeout = 3600;
    int max_retries = 0;
    int retry_interval = 60;
    nlohmann::json resource_tags;
    nlohmann::json parameters_json;
    std::string creator_id;
    int version = 1;
    bool deleted = false;
    std::string created_at;
    std::string updated_at;

    static Task fromRow(const pqxx::row& row) {
        Task task;
        task.id = row["id"].as<std::string>();
        task.name = row["name"].as<std::string>();
        task.type = row["type"].as<std::string>();
        // Fix #290: 处理 NULL 字段，避免 pqxx::conversion_error 崩溃
        task.config_json = row["config_json"].is_null()
            ? nlohmann::json::object()
            : nlohmann::json::parse(row["config_json"].as<std::string>());
        task.description = row["description"].is_null()
            ? "" : row["description"].as<std::string>();
        task.timeout = row["timeout"].as<int>();
        task.max_retries = row["max_retries"].as<int>();
        task.retry_interval = row["retry_interval"].as<int>();
        task.resource_tags = row["resource_tags"].is_null()
            ? nlohmann::json::object()
            : nlohmann::json::parse(row["resource_tags"].as<std::string>());
        task.parameters_json = row["parameters_json"].is_null()
            ? nlohmann::json::object()
            : nlohmann::json::parse(row["parameters_json"].as<std::string>());
        task.creator_id = row["creator_id"].is_null()
            ? "" : row["creator_id"].as<std::string>();
        task.version = row["version"].as<int>();
        task.deleted = row["deleted"].as<bool>();
        task.created_at = row["created_at"].as<std::string>();
        task.updated_at = row["updated_at"].as<std::string>();
        return task;
    }

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"name", name},
            {"type", type},
            {"config_json", config_json},
            {"description", description},
            {"timeout", timeout},
            {"max_retries", max_retries},
            {"retry_interval", retry_interval},
            {"resource_tags", resource_tags},
            {"parameters_json", parameters_json},
            {"creator_id", creator_id},
            {"version", version},
            {"deleted", deleted},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

} // namespace taskflow::common::models
