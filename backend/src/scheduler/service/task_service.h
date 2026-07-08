#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "common/models/task.h"
#include "common/util/crypto_util.h"
#include "scheduler/dao/task_dao.h"
#include "scheduler/dao/user_dao.h"

namespace taskflow::scheduler::service {

class TaskService {
public:
    TaskService(const std::string& aes_key);

    // Create task: validate type is one of command/script/sql, validate config fields, encrypt SQL password
    common::result::Result<nlohmann::json> createTask(
        const std::string& name, const std::string& type,
        const nlohmann::json& config_json, const std::string& description,
        int timeout, int max_retries, int retry_interval,
        const nlohmann::json& resource_tags, const nlohmann::json& parameters_json,
        const std::string& creator_id);

    // Get task by ID (SQL password masked as "***")
    // Fix #324: resource-level permission check - non-admin users can only
    // view their own tasks.
    common::result::Result<nlohmann::json> getTask(
        const std::string& id, const std::string& user_id, const std::string& role);

    // List tasks with pagination and filters
    common::result::Result<nlohmann::json> listTasks(
        int page, int page_size, const std::string& type_filter,
        const std::string& keyword, const std::string& creator_id);

    // Update task (version auto-increment)
    common::result::Result<nlohmann::json> updateTask(
        const std::string& id, const std::string& name, const std::string& type,
        const nlohmann::json& config_json, const std::string& description,
        int timeout, int max_retries, int retry_interval,
        const nlohmann::json& resource_tags, const nlohmann::json& parameters_json,
        const std::string& user_id, const std::string& role);

    // Delete task (soft delete)
    common::result::Result<void> deleteTask(
        const std::string& id, const std::string& user_id, const std::string& role);

private:
    std::string aes_key_;
    dao::TaskDao task_dao_;
    dao::UserDao user_dao_;

    // Validate task config based on type
    common::result::Result<void> validateConfig(const std::string& type, const nlohmann::json& config);

    // Mask SQL password in config_json for display
    nlohmann::json maskSensitiveFields(const nlohmann::json& config, const std::string& type);

    // Encrypt SQL password in config_json for storage
    common::result::Result<nlohmann::json> encryptSensitiveFields(
        const nlohmann::json& config, const std::string& type);
};

}  // namespace taskflow::scheduler::service
