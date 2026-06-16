#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace taskflow::common::models {

struct CronJob {
    std::string id;
    std::string workflow_id;
    std::string cron_expression;
    bool enabled = true;
    std::string next_trigger_time;
    std::string created_at;
    std::string updated_at;

    static CronJob fromRow(const pqxx::row& row) {
        CronJob job;
        job.id = row["id"].as<std::string>();
        job.workflow_id = row["workflow_id"].as<std::string>();
        job.cron_expression = row["cron_expression"].as<std::string>();
        job.enabled = row["enabled"].as<bool>();
        job.next_trigger_time = row["next_trigger_time"].is_null()
            ? "" : row["next_trigger_time"].as<std::string>();
        job.created_at = row["created_at"].as<std::string>();
        job.updated_at = row["updated_at"].as<std::string>();
        return job;
    }

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"workflow_id", workflow_id},
            {"cron_expression", cron_expression},
            {"enabled", enabled},
            {"next_trigger_time", next_trigger_time},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

} // namespace taskflow::common::models
