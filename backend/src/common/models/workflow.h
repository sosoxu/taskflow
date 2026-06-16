#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace taskflow::common::models {

struct Workflow {
    std::string id;
    std::string name;
    std::string description;
    nlohmann::json dag_json;
    std::string schedule_strategy;  // random, load_balance, specified
    std::string target_worker_id;
    std::string cron_expression;
    bool cron_enabled = false;
    std::string creator_id;
    int version = 1;
    bool deleted = false;
    std::string created_at;
    std::string updated_at;

    static Workflow fromRow(const pqxx::row& row) {
        Workflow workflow;
        workflow.id = row["id"].as<std::string>();
        workflow.name = row["name"].as<std::string>();
        workflow.description = row["description"].as<std::string>();
        workflow.dag_json = nlohmann::json::parse(row["dag_json"].as<std::string>());
        workflow.schedule_strategy = row["schedule_strategy"].as<std::string>();
        workflow.target_worker_id = row["target_worker_id"].is_null()
            ? "" : row["target_worker_id"].as<std::string>();
        workflow.cron_expression = row["cron_expression"].is_null()
            ? "" : row["cron_expression"].as<std::string>();
        workflow.cron_enabled = row["cron_enabled"].as<bool>();
        workflow.creator_id = row["creator_id"].as<std::string>();
        workflow.version = row["version"].as<int>();
        workflow.deleted = row["deleted"].as<bool>();
        workflow.created_at = row["created_at"].as<std::string>();
        workflow.updated_at = row["updated_at"].as<std::string>();
        return workflow;
    }

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"name", name},
            {"description", description},
            {"dag_json", dag_json},
            {"schedule_strategy", schedule_strategy},
            {"target_worker_id", target_worker_id},
            {"cron_expression", cron_expression},
            {"cron_enabled", cron_enabled},
            {"creator_id", creator_id},
            {"version", version},
            {"deleted", deleted},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

} // namespace taskflow::common::models
