#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace taskflow::common::models {

struct WorkerInfo {
    std::string id;
    std::string name;
    std::string address;
    std::string status;  // online, offline
    double cpu_usage = 0.0;
    double memory_usage = 0.0;
    int running_tasks = 0;
    int max_tasks = 10;
    nlohmann::json resource_tags;
    std::string last_heartbeat;
    std::string registered_at;

    static WorkerInfo fromRow(const pqxx::row& row) {
        WorkerInfo worker;
        worker.id = row["id"].as<std::string>();
        worker.name = row["name"].as<std::string>();
        worker.address = row["address"].as<std::string>();
        worker.status = row["status"].as<std::string>();
        worker.cpu_usage = row["cpu_usage"].as<double>();
        worker.memory_usage = row["memory_usage"].as<double>();
        worker.running_tasks = row["running_tasks"].as<int>();
        worker.max_tasks = row["max_tasks"].as<int>();
        worker.resource_tags = nlohmann::json::parse(row["resource_tags"].as<std::string>());
        worker.last_heartbeat = row["last_heartbeat"].as<std::string>();
        worker.registered_at = row["registered_at"].as<std::string>();
        return worker;
    }

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"name", name},
            {"address", address},
            {"status", status},
            {"cpu_usage", cpu_usage},
            {"memory_usage", memory_usage},
            {"running_tasks", running_tasks},
            {"max_tasks", max_tasks},
            {"resource_tags", resource_tags},
            {"last_heartbeat", last_heartbeat},
            {"registered_at", registered_at}
        };
    }
};

} // namespace taskflow::common::models
