#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace taskflow::common::models {

struct TaskInstance {
    std::string id;
    std::string workflow_instance_id;
    std::string task_id;
    int task_version = 0;
    std::string task_name;
    std::string status;  // PENDING, DISPATCHED, RUNNING, SUCCESS, FAILED, UPSTREAM_FAILED, TIMEOUT, CANCELLED, NODE_OFFLINE
    std::string worker_id;
    int retry_count = 0;
    std::string started_at;
    std::string finished_at;
    int exit_code = 0;
    std::string error_message;
    std::string created_at;

    static TaskInstance fromRow(const pqxx::row& row) {
        TaskInstance instance;
        instance.id = row["id"].as<std::string>();
        instance.workflow_instance_id = row["workflow_instance_id"].as<std::string>();
        instance.task_id = row["task_id"].as<std::string>();
        instance.task_version = row["task_version"].as<int>();
        instance.task_name = row["task_name"].as<std::string>();
        instance.status = row["status"].as<std::string>();
        instance.worker_id = row["worker_id"].is_null()
            ? "" : row["worker_id"].as<std::string>();
        instance.retry_count = row["retry_count"].as<int>();
        instance.started_at = row["started_at"].is_null()
            ? "" : row["started_at"].as<std::string>();
        instance.finished_at = row["finished_at"].is_null()
            ? "" : row["finished_at"].as<std::string>();
        instance.exit_code = row["exit_code"].is_null()
            ? 0 : row["exit_code"].as<int>();
        instance.error_message = row["error_message"].is_null()
            ? "" : row["error_message"].as<std::string>();
        instance.created_at = row["created_at"].as<std::string>();
        return instance;
    }

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"workflow_instance_id", workflow_instance_id},
            {"task_id", task_id},
            {"task_version", task_version},
            {"task_name", task_name},
            {"status", status},
            {"worker_id", worker_id},
            {"retry_count", retry_count},
            {"started_at", started_at},
            {"finished_at", finished_at},
            {"exit_code", exit_code},
            {"error_message", error_message},
            {"created_at", created_at}
        };
    }
};

} // namespace taskflow::common::models
