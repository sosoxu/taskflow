#include "common/config/scheduler_config.h"

#include <stdexcept>
#include <spdlog/spdlog.h>

namespace taskflow::common::config {

SchedulerConfig SchedulerConfig::load(const std::string& config_path) {
    SchedulerConfig config;
    YAML::Node root;

    try {
        root = YAML::LoadFile(config_path);
    } catch (const YAML::BadFile& e) {
        throw std::runtime_error("无法读取配置文件: " + config_path);
    } catch (const YAML::ParserException& e) {
        throw std::runtime_error("配置文件格式错误: " + std::string(e.what()));
    }

    // server
    if (root["server"]) {
        auto s = root["server"];
        if (s["http_port"]) config.server.http_port = s["http_port"].as<int>();
        if (s["grpc_port"]) config.server.grpc_port = s["grpc_port"].as<int>();
    }

    // database
    if (root["database"]) {
        auto db = root["database"];
        if (db["host"]) config.database.host = db["host"].as<std::string>();
        if (db["port"]) config.database.port = db["port"].as<int>();
        if (db["name"]) config.database.name = db["name"].as<std::string>();
        if (db["user"]) config.database.user = db["user"].as<std::string>();
        if (db["password"]) config.database.password = db["password"].as<std::string>();
        if (db["min_connections"]) config.database.min_connections = db["min_connections"].as<int>();
        if (db["max_connections"]) config.database.max_connections = db["max_connections"].as<int>();
    }

    // auth
    if (root["auth"]) {
        auto a = root["auth"];
        if (a["jwt_secret"]) config.auth.jwt_secret = a["jwt_secret"].as<std::string>();
        if (a["access_token_ttl"]) config.auth.access_token_ttl = a["access_token_ttl"].as<int>();
        if (a["refresh_token_ttl"]) config.auth.refresh_token_ttl = a["refresh_token_ttl"].as<int>();
    }

    // encryption
    if (root["encryption"]) {
        auto e = root["encryption"];
        if (e["aes_key"]) config.encryption.aes_key = e["aes_key"].as<std::string>();
    }

    // log
    if (root["log"]) {
        auto l = root["log"];
        if (l["level"]) config.log.level = l["level"].as<std::string>();
        if (l["file_path"]) config.log.file_path = l["file_path"].as<std::string>();
    }

    // schedule
    if (root["schedule"]) {
        auto sc = root["schedule"];
        if (sc["dag_drive_interval"]) config.schedule.dag_drive_interval = sc["dag_drive_interval"].as<int>();
        if (sc["heartbeat_check_interval"]) config.schedule.heartbeat_check_interval = sc["heartbeat_check_interval"].as<int>();
        if (sc["heartbeat_timeout"]) config.schedule.heartbeat_timeout = sc["heartbeat_timeout"].as<int>();
        if (sc["timeout_check_interval"]) config.schedule.timeout_check_interval = sc["timeout_check_interval"].as<int>();
        if (sc["leader_lease_interval"]) config.schedule.leader_lease_interval = sc["leader_lease_interval"].as<int>();
    }

    config.validate();
    return config;
}

void SchedulerConfig::validate() const {
    if (auth.jwt_secret.empty()) {
        throw std::runtime_error("配置错误: auth.jwt_secret 不能为空");
    }
    if (auth.jwt_secret.size() < 32) {
        throw std::runtime_error("配置错误: auth.jwt_secret 长度不能少于 32 字符");
    }
    if (encryption.aes_key.empty()) {
        throw std::runtime_error("配置错误: encryption.aes_key 不能为空");
    }
    if (encryption.aes_key.size() != 32) {
        throw std::runtime_error("配置错误: encryption.aes_key 必须为 32 字符");
    }
    if (server.http_port <= 0 || server.http_port > 65535) {
        throw std::runtime_error("配置错误: server.http_port 无效");
    }
    if (server.grpc_port <= 0 || server.grpc_port > 65535) {
        throw std::runtime_error("配置错误: server.grpc_port 无效");
    }
    if (database.min_connections <= 0) {
        throw std::runtime_error("配置错误: database.min_connections 必须大于 0");
    }
    if (database.max_connections < database.min_connections) {
        throw std::runtime_error("配置错误: database.max_connections 不能小于 min_connections");
    }
}

}  // namespace taskflow::common::config
