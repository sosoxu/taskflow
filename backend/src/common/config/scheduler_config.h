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
        // Fix #281: 对含特殊字符（空格、单引号、反斜杠）的值用单引号包裹并转义
        // libpq key=value 格式中，含特殊字符的值需用单引号包裹，内部单引号和反斜杠需转义
        auto escape = [](const std::string& val) -> std::string {
            if (val.empty()) return "''";
            bool need_quote = false;
            for (char c : val) {
                if (c == ' ' || c == '\'' || c == '\\' || c == '\t' || c == '\n') {
                    need_quote = true;
                    break;
                }
            }
            if (!need_quote) return val;
            std::string result = "'";
            for (char c : val) {
                if (c == '\\' || c == '\'') result += '\\';
                result += c;
            }
            result += "'";
            return result;
        };
        return "host=" + escape(host) + " port=" + std::to_string(port) +
               " dbname=" + escape(name) + " user=" + escape(user) +
               " password=" + escape(password);
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
    // Fix #348: 选主 advisory lock ID。此前硬编码 12345，多套环境共用同一
    // PostgreSQL 实例时会互抢主。部署多环境时必须为每套环境配置不同值。
    int advisory_lock_id = 12345;
};

struct TlsConfig {
    bool enabled = false;
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
};

struct ServerConfig {
    int http_port = 8080;
    // Fix #344: HTTP 监听地址。默认 0.0.0.0（容器内所有网卡，由是否发布端口
    // 决定对外暴露）；TLS 部署（nginx 前置终止）时可收紧为容器内网地址，
    // 保证明文 HTTP 端口不出内网。
    std::string http_bind_address = "0.0.0.0";
    int grpc_port = 50051;
    // Fix #312: Configurable drogon IO thread count. Default 16 (was hardcoded 4).
    // bcrypt password verification (~100ms) blocks IO threads; more threads
    // improve concurrent login throughput.
    int thread_num = 16;
    TlsConfig tls;
    // Fix #182: Comma-separated list of allowed CORS origins. When non-empty
    // and the request Origin matches an entry, that Origin is echoed back;
    // otherwise the default "*" is used (dev-friendly).
    std::string cors_origins;
    // Fix #326: scheduler gRPC 服务端校验的内部认证 token。为空表示不启用
    // （兼容旧部署）。生产部署必须设置，且与各 worker 的 worker.yaml 一致。
    std::string grpc_auth_token;
};

// Fix #126: TLS config for scheduler→worker gRPC calls (DispatchTask, CancelTask, GetTaskLog).
// Separate from server.tls (which is for incoming connections to the scheduler).
struct WorkerClientConfig {
    TlsConfig tls;
};

struct StaticFilesConfig {
    bool enabled = true;
    // 静态资源目录路径。为空时默认为 scheduler 可执行程序所在目录。
    std::string path;
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
    StaticFilesConfig static_files;

    static SchedulerConfig load(const std::string& config_path);

    void validate() const;
};

}  // namespace taskflow::common::config
