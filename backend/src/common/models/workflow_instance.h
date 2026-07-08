#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace taskflow::common::models {

struct WorkflowInstance {
    std::string id;
    std::string workflow_id;
    int workflow_version = 0;
    std::string status;  // PENDING, RUNNING, PAUSED, SUCCESS, FAILED, CANCELLED
    std::string trigger_type;  // manual, cron
    std::string started_at;
    std::string finished_at;
    nlohmann::json param_overrides;
    nlohmann::json dag_snapshot;  // Fix #152: snapshot of dag_json at instance creation time
    std::string creator_id;
    std::string created_at;

    static WorkflowInstance fromRow(const pqxx::row& row) {
        WorkflowInstance instance;
        instance.id = row["id"].as<std::string>();
        instance.workflow_id = row["workflow_id"].as<std::string>();
        instance.workflow_version = row["workflow_version"].as<int>();
        instance.status = row["status"].as<std::string>();
        instance.trigger_type = row["trigger_type"].as<std::string>();
        instance.started_at = row["started_at"].is_null()
            ? "" : row["started_at"].as<std::string>();
        instance.finished_at = row["finished_at"].is_null()
            ? "" : row["finished_at"].as<std::string>();
        instance.param_overrides = row["param_overrides"].is_null()
            ? nlohmann::json::object()
            : nlohmann::json::parse(row["param_overrides"].as<std::string>());
        // dag_snapshot 列可能不存在（旧数据库未执行迁移），容错处理
        try {
            instance.dag_snapshot = row["dag_snapshot"].is_null()
                ? nlohmann::json() : nlohmann::json::parse(row["dag_snapshot"].as<std::string>());
        } catch (const pqxx::argument_error&) {
            instance.dag_snapshot = nlohmann::json();
        }
        instance.creator_id = row["creator_id"].as<std::string>();
        instance.created_at = row["created_at"].as<std::string>();
        return instance;
    }

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"workflow_id", workflow_id},
            {"workflow_version", workflow_version},
            {"status", status},
            {"trigger_type", trigger_type},
            {"started_at", started_at},
            {"finished_at", finished_at},
            {"param_overrides", param_overrides},
            {"dag_snapshot", dag_snapshot},
            {"creator_id", creator_id},
            {"created_at", created_at}
        };
    }
};

} // namespace taskflow::common::models
