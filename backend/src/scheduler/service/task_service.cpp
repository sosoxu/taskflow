#include "scheduler/service/task_service.h"

namespace taskflow::scheduler::service {

TaskService::TaskService(const std::string& aes_key)
    : aes_key_(aes_key) {}

common::result::Result<nlohmann::json> TaskService::createTask(
    const std::string& name, const std::string& type,
    const nlohmann::json& config_json, const std::string& description,
    int timeout, int max_retries, int retry_interval,
    const nlohmann::json& resource_tags, const std::string& creator_id) {

    // Validate type
    if (type != "command" && type != "script" && type != "sql") {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid task type: must be one of command, script, sql");
    }

    // Validate config
    auto validateResult = validateConfig(type, config_json);
    if (!validateResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(validateResult.error());
    }

    // Encrypt sensitive fields
    auto encryptResult = encryptSensitiveFields(config_json, type);
    if (!encryptResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(encryptResult.error());
    }

    const auto& encrypted_config = encryptResult.value();

    // Create task via DAO
    auto createResult = task_dao_.create(
        name, type, encrypted_config, description,
        timeout, max_retries, retry_interval, resource_tags, creator_id);
    if (!createResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to create task: " + createResult.error());
    }

    // Fetch the created task
    auto taskResult = task_dao_.findById(createResult.value());
    if (!taskResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to fetch created task: " + taskResult.error());
    }

    auto taskJson = taskResult.value().toJson();
    taskJson["config_json"] = maskSensitiveFields(taskJson["config_json"], type);

    return taskJson;
}

common::result::Result<nlohmann::json> TaskService::getTask(const std::string& id) {
    auto taskResult = task_dao_.findById(id);
    if (!taskResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Task not found: " + taskResult.error());
    }

    const auto& task = taskResult.value();
    auto taskJson = task.toJson();
    taskJson["config_json"] = maskSensitiveFields(taskJson["config_json"], task.type);

    return taskJson;
}

common::result::Result<nlohmann::json> TaskService::listTasks(
    int page, int page_size, const std::string& type_filter,
    const std::string& keyword, const std::string& creator_id) {

    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;

    int offset = (page - 1) * page_size;

    auto listResult = task_dao_.list(offset, page_size, type_filter, keyword, creator_id);
    if (!listResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to list tasks: " + listResult.error());
    }

    const auto& tasks = listResult.value();
    nlohmann::json items = nlohmann::json::array();
    for (const auto& task : tasks) {
        auto taskJson = task.toJson();
        taskJson["config_json"] = maskSensitiveFields(taskJson["config_json"], task.type);
        items.push_back(taskJson);
    }

    nlohmann::json response = {
        {"items", items},
        {"total", static_cast<int>(tasks.size())},
        {"page", page},
        {"page_size", page_size}
    };

    return response;
}

common::result::Result<nlohmann::json> TaskService::updateTask(
    const std::string& id, const std::string& name, const std::string& type,
    const nlohmann::json& config_json, const std::string& description,
    int timeout, int max_retries, int retry_interval,
    const nlohmann::json& resource_tags) {

    // Validate type
    if (type != "command" && type != "script" && type != "sql") {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid task type: must be one of command, script, sql");
    }

    // Validate config
    auto validateResult = validateConfig(type, config_json);
    if (!validateResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(validateResult.error());
    }

    // Encrypt sensitive fields
    auto encryptResult = encryptSensitiveFields(config_json, type);
    if (!encryptResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(encryptResult.error());
    }

    const auto& encrypted_config = encryptResult.value();

    // Update task via DAO (version auto-increment is handled in DAO)
    auto updateResult = task_dao_.update(
        id, name, type, encrypted_config, description,
        timeout, max_retries, retry_interval, resource_tags);
    if (!updateResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to update task: " + updateResult.error());
    }

    // Fetch the updated task
    auto taskResult = task_dao_.findById(id);
    if (!taskResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to fetch updated task: " + taskResult.error());
    }

    auto taskJson = taskResult.value().toJson();
    taskJson["config_json"] = maskSensitiveFields(taskJson["config_json"], type);

    return taskJson;
}

common::result::Result<void> TaskService::deleteTask(const std::string& id) {
    auto result = task_dao_.softDelete(id);
    if (!result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to delete task: " + result.error());
    }
    return common::result::Result<void>();
}

common::result::Result<void> TaskService::validateConfig(
    const std::string& type, const nlohmann::json& config) {

    if (type == "command") {
        if (!config.contains("command") || !config["command"].is_string()) {
            return common::result::Result<void>::failure(
                "Config for command type must have 'command' field");
        }
    } else if (type == "script") {
        if (!config.contains("script_content") || !config["script_content"].is_string()) {
            return common::result::Result<void>::failure(
                "Config for script type must have 'script_content' field");
        }
    } else if (type == "sql") {
        static const std::array<std::string, 6> required_fields = {
            "db_host", "db_port", "db_name", "db_user", "db_password", "sql_statement"
        };
        for (const auto& field : required_fields) {
            if (!config.contains(field) || !config[field].is_string()) {
                return common::result::Result<void>::failure(
                    "Config for sql type must have '" + field + "' field");
            }
        }
    }

    return common::result::Result<void>();
}

nlohmann::json TaskService::maskSensitiveFields(
    const nlohmann::json& config, const std::string& type) {

    if (type != "sql" || !config.is_object()) {
        return config;
    }

    nlohmann::json masked = config;
    if (masked.contains("db_password")) {
        masked["db_password"] = "***";
    }
    return masked;
}

common::result::Result<nlohmann::json> TaskService::encryptSensitiveFields(
    const nlohmann::json& config, const std::string& type) {

    if (type != "sql" || !config.is_object()) {
        return config;
    }

    nlohmann::json encrypted = config;
    if (encrypted.contains("db_password") && encrypted["db_password"].is_string()) {
        auto encryptResult = common::util::CryptoUtil::encrypt(
            encrypted["db_password"].get<std::string>(), aes_key_);
        if (!encryptResult.ok()) {
            return common::result::Result<nlohmann::json>::failure(
                "Failed to encrypt db_password: " + encryptResult.error());
        }
        encrypted["db_password"] = encryptResult.value();
    }

    return encrypted;
}

}  // namespace taskflow::scheduler::service
