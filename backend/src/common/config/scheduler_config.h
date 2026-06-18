#pragma once

#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace taskflow::common::config {

struct DatabaseConfig {
    std::string host = "localhost";
    int port = 5432;
    std::string name = "taskflow";
    std::string user = "taskflow";
    std::string password;
    int min_connections = 5;
    int max_connections = 20;

    std::string connectionString() const {
        return "host=" + host + " port=" + std::to_string(port) +
               " dbname=" + name + " user=" + user + " password=" + password;
    }
};

struct AuthConfig {
    std::string jwt_secret;
    int access_token_ttl = 86400;    // 秒
    int refresh_token_ttl = 604800;  // 秒
};

struct EncryptionConfig {
    std::string aes_key;  // 32 字符
};

struct LogConfig {
    std::string level = "info";
    std::string file_path = "logs/scheduler.log";
};

struct ScheduleConfig {
    int dag_drive_interval = 2;          // DAG 驱动间隔（秒）
    int heartbeat_check_interval = 10;   // 心跳检查间隔（秒）
    int heartbeat_timeout = 30;          // 心跳超时（秒）
    int timeout_check_interval = 10;     // 任务超时检查间隔（秒）
    int leader_lease_interval = 5;       // 选主续约间隔（秒）
};

struct TlsConfig {
    bool enabled = false;
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
};

struct ServerConfig {
    int http_port = 8080;
    int grpc_port = 50051;
    TlsConfig tls;
};

// Fix #126: TLS config for scheduler→worker gRPC calls (DispatchTask, CancelTask, GetTaskLog).
// Separate from server.tls (which is for incoming connections to the scheduler).
struct WorkerClientConfig {
    TlsConfig tls;
};

class SchedulerConfig {
public:
    ServerConfig server;
    DatabaseConfig database;
    AuthConfig auth;
    EncryptionConfig encryption;
    LogConfig log;
    ScheduleConfig schedule;
    WorkerClientConfig worker_client;

    static SchedulerConfig load(const std::string& config_path);

    void validate() const;
};

}  // namespace taskflow::common::config
