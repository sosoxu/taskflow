#include <catch2/catch_test_macros.hpp>
#include <string>
#include <stdexcept>
#include <fstream>
#include <cstdio>

#include "common/config/scheduler_config.h"
#include "common/config/worker_config.h"

using namespace taskflow::common::config;

// ============================================================================
// Fix #244: SchedulerConfig / WorkerConfig validate() 单元测试
// 原测试套件对配置校验逻辑零覆盖。validate() 在启动时被调用，错误的配置
// 会直接抛异常终止进程。这些测试确保各校验分支被覆盖。
// ============================================================================

// Helper: build a SchedulerConfig with all valid defaults
static SchedulerConfig makeValidSchedulerConfig() {
    SchedulerConfig cfg;
    cfg.auth.jwt_secret = "this-is-a-very-secure-jwt-secret-32+";
    cfg.encryption.aes_key = "0123456789abcdef0123456789abcdef";  // 32 chars
    cfg.server.http_port = 8080;
    cfg.server.grpc_port = 50051;
    cfg.database.min_connections = 5;
    cfg.database.max_connections = 20;
    cfg.schedule.dag_drive_interval = 2;
    cfg.schedule.heartbeat_check_interval = 10;
    cfg.schedule.heartbeat_timeout = 30;
    cfg.schedule.leader_lease_interval = 5;
    return cfg;
}

// Helper: build a WorkerConfig with all valid defaults
static WorkerConfig makeValidWorkerConfig() {
    WorkerConfig cfg;
    cfg.server.grpc_port = 50052;
    cfg.scheduler.address = "localhost:50051";
    cfg.worker.max_tasks = 10;
    cfg.task_log.retention_days = 30;
    return cfg;
}

// ---------------------------------------------------------------------------
// SchedulerConfig::validate() - valid config
// ---------------------------------------------------------------------------

TEST_CASE("SchedulerConfig: valid config passes validation", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    REQUIRE_NOTHROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// SchedulerConfig::validate() - jwt_secret
// ---------------------------------------------------------------------------

TEST_CASE("SchedulerConfig: empty jwt_secret throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.jwt_secret = "";
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: jwt_secret shorter than 32 chars throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.jwt_secret = "short";  // 5 chars
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: jwt_secret exactly 32 chars passes", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.jwt_secret = std::string(32, 'a');
    REQUIRE_NOTHROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// SchedulerConfig::validate() - aes_key
// ---------------------------------------------------------------------------

TEST_CASE("SchedulerConfig: empty aes_key throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.encryption.aes_key = "";
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: aes_key not 32 chars throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.encryption.aes_key = "0123456789abcdef";  // 16 chars
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: aes_key 33 chars throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.encryption.aes_key = std::string(33, 'b');
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// SchedulerConfig::validate() - ports
// ---------------------------------------------------------------------------

TEST_CASE("SchedulerConfig: http_port 0 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.server.http_port = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: http_port negative throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.server.http_port = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: http_port 65536 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.server.http_port = 65536;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: grpc_port 0 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.server.grpc_port = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: grpc_port 65536 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.server.grpc_port = 65536;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: port 1 passes (lower bound)", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.server.http_port = 1;
    cfg.server.grpc_port = 1;
    REQUIRE_NOTHROW(cfg.validate());
}

TEST_CASE("SchedulerConfig: port 65535 passes (upper bound)", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.server.http_port = 65535;
    cfg.server.grpc_port = 65535;
    REQUIRE_NOTHROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// SchedulerConfig::validate() - database connections
// ---------------------------------------------------------------------------

TEST_CASE("SchedulerConfig: min_connections 0 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.database.min_connections = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: min_connections negative throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.database.min_connections = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: max_connections less than min throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.database.min_connections = 10;
    cfg.database.max_connections = 5;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: max_connections equal to min passes", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.database.min_connections = 10;
    cfg.database.max_connections = 10;
    REQUIRE_NOTHROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// SchedulerConfig::validate() - schedule parameters
// ---------------------------------------------------------------------------

TEST_CASE("SchedulerConfig: dag_drive_interval 0 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.dag_drive_interval = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: dag_drive_interval negative throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.dag_drive_interval = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: heartbeat_check_interval 0 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.heartbeat_check_interval = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: heartbeat_timeout 0 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.heartbeat_timeout = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: leader_lease_interval 0 throws", "[config_scheduler]") {
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.leader_lease_interval = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// WorkerConfig::validate() - valid config
// ---------------------------------------------------------------------------

TEST_CASE("WorkerConfig: valid config passes validation", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    REQUIRE_NOTHROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// WorkerConfig::validate() - grpc_port
// ---------------------------------------------------------------------------

TEST_CASE("WorkerConfig: grpc_port 0 throws", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.server.grpc_port = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("WorkerConfig: grpc_port 65536 throws", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.server.grpc_port = 65536;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("WorkerConfig: grpc_port 1 passes (lower bound)", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.server.grpc_port = 1;
    REQUIRE_NOTHROW(cfg.validate());
}

TEST_CASE("WorkerConfig: grpc_port 65535 passes (upper bound)", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.server.grpc_port = 65535;
    REQUIRE_NOTHROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// WorkerConfig::validate() - scheduler.address
// ---------------------------------------------------------------------------

TEST_CASE("WorkerConfig: empty scheduler.address throws", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.scheduler.address = "";
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// WorkerConfig::validate() - worker.max_tasks
// ---------------------------------------------------------------------------

TEST_CASE("WorkerConfig: max_tasks 0 throws", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.worker.max_tasks = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("WorkerConfig: max_tasks negative throws", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.worker.max_tasks = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("WorkerConfig: max_tasks 1 passes (lower bound)", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.worker.max_tasks = 1;
    REQUIRE_NOTHROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// WorkerConfig::validate() - task_log.retention_days
// ---------------------------------------------------------------------------

TEST_CASE("WorkerConfig: retention_days 0 throws", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.task_log.retention_days = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("WorkerConfig: retention_days negative throws", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.task_log.retention_days = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("WorkerConfig: retention_days 1 passes (lower bound)", "[config_worker]") {
    auto cfg = makeValidWorkerConfig();
    cfg.task_log.retention_days = 1;
    REQUIRE_NOTHROW(cfg.validate());
}

// ============================================================================
// Fix #263: load() 方法测试、connectionString() 测试、异常消息校验、边界值
// ============================================================================

// --- DatabaseConfig::connectionString() 测试 ---

TEST_CASE("DatabaseConfig: connectionString format", "[config_connection_string]") {
    // Fix #263: 验证 connectionString() 输出格式
    DatabaseConfig db;
    db.host = "db.example.com";
    db.port = 6543;
    db.name = "mydb";
    db.user = "myuser";
    db.password = "mypass";

    std::string conn = db.connectionString();
    REQUIRE(conn.find("host=db.example.com") != std::string::npos);
    REQUIRE(conn.find("port=6543") != std::string::npos);
    REQUIRE(conn.find("dbname=mydb") != std::string::npos);
    REQUIRE(conn.find("user=myuser") != std::string::npos);
    REQUIRE(conn.find("password=mypass") != std::string::npos);
}

TEST_CASE("DatabaseConfig: connectionString with empty password", "[config_connection_string]") {
    // Fix #263: 空 password 时 connectionString 仍应包含 password=（空值）
    DatabaseConfig db;
    db.host = "localhost";
    db.port = 5432;
    db.name = "testdb";
    db.user = "testuser";
    db.password = "";  // 空

    std::string conn = db.connectionString();
    REQUIRE(conn.find("password=") != std::string::npos);
    // password= 后应跟空格或为字符串末尾
    size_t pos = conn.find("password=");
    REQUIRE(pos != std::string::npos);
}

TEST_CASE("DatabaseConfig: connectionString default values", "[config_connection_string]") {
    // Fix #263: 默认值的 connectionString
    DatabaseConfig db;  // 使用默认值
    std::string conn = db.connectionString();
    REQUIRE(conn.find("host=localhost") != std::string::npos);
    REQUIRE(conn.find("port=5432") != std::string::npos);
    REQUIRE(conn.find("dbname=taskflow") != std::string::npos);
    REQUIRE(conn.find("user=taskflow") != std::string::npos);
}

// --- SchedulerConfig::load() 测试 ---

TEST_CASE("SchedulerConfig: load valid config file", "[config_load]") {
    // Fix #263: 验证 load() 正常加载合法 YAML 配置文件
    const std::string path = "/tmp/taskflow_test_scheduler_valid.yaml";
    {
        std::ofstream ofs(path);
        ofs << R"(
server:
  http_port: 9090
  grpc_port: 50052
  cors_origins: "https://example.com,https://api.example.com"
  tls:
    enabled: true
    cert_path: "/etc/ssl/cert.pem"
    key_path: "/etc/ssl/key.pem"
    ca_path: "/etc/ssl/ca.pem"
database:
  host: "db.host"
  port: 6543
  name: "testdb"
  user: "testuser"
  password: "testpass"
  min_connections: 3
  max_connections: 15
auth:
  jwt_secret: "this-is-a-very-secure-jwt-secret-32+"
  access_token_ttl: 7200
  refresh_token_ttl: 86400
encryption:
  aes_key: "0123456789abcdef0123456789abcdef"
log:
  level: "debug"
  file_path: "/var/log/taskflow.log"
schedule:
  dag_drive_interval: 3
  heartbeat_check_interval: 15
  heartbeat_timeout: 45
  timeout_check_interval: 20
  leader_lease_interval: 7
worker_client:
  tls:
    enabled: false
)";
    }

    auto cfg = SchedulerConfig::load(path);
    REQUIRE(cfg.server.http_port == 9090);
    REQUIRE(cfg.server.grpc_port == 50052);
    REQUIRE(cfg.server.cors_origins == "https://example.com,https://api.example.com");
    REQUIRE(cfg.server.tls.enabled == true);
    REQUIRE(cfg.server.tls.cert_path == "/etc/ssl/cert.pem");
    REQUIRE(cfg.server.tls.key_path == "/etc/ssl/key.pem");
    REQUIRE(cfg.server.tls.ca_path == "/etc/ssl/ca.pem");
    REQUIRE(cfg.database.host == "db.host");
    REQUIRE(cfg.database.port == 6543);
    REQUIRE(cfg.database.name == "testdb");
    REQUIRE(cfg.database.user == "testuser");
    REQUIRE(cfg.database.password == "testpass");
    REQUIRE(cfg.database.min_connections == 3);
    REQUIRE(cfg.database.max_connections == 15);
    REQUIRE(cfg.auth.jwt_secret == "this-is-a-very-secure-jwt-secret-32+");
    REQUIRE(cfg.auth.access_token_ttl == 7200);
    REQUIRE(cfg.auth.refresh_token_ttl == 86400);
    REQUIRE(cfg.encryption.aes_key == "0123456789abcdef0123456789abcdef");
    REQUIRE(cfg.log.level == "debug");
    REQUIRE(cfg.log.file_path == "/var/log/taskflow.log");
    REQUIRE(cfg.schedule.dag_drive_interval == 3);
    REQUIRE(cfg.schedule.heartbeat_check_interval == 15);
    REQUIRE(cfg.schedule.heartbeat_timeout == 45);
    REQUIRE(cfg.schedule.timeout_check_interval == 20);
    REQUIRE(cfg.schedule.leader_lease_interval == 7);
    REQUIRE(cfg.worker_client.tls.enabled == false);

    std::remove(path.c_str());
}

TEST_CASE("SchedulerConfig: load non-existent file throws", "[config_load]") {
    // Fix #263: 文件不存在时抛 std::runtime_error
    REQUIRE_THROWS_AS(SchedulerConfig::load("/tmp/taskflow_nonexistent_config.yaml"),
                      std::runtime_error);
    try {
        SchedulerConfig::load("/tmp/taskflow_nonexistent_config.yaml");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("无法读取") != std::string::npos);
    }
}

TEST_CASE("SchedulerConfig: load malformed YAML throws", "[config_load]") {
    // Fix #263: YAML 格式错误时抛 std::runtime_error
    const std::string path = "/tmp/taskflow_test_scheduler_malformed.yaml";
    {
        std::ofstream ofs(path);
        ofs << "server: [unclosed bracket\n  invalid: yaml: structure";
    }

    REQUIRE_THROWS_AS(SchedulerConfig::load(path), std::runtime_error);
    std::remove(path.c_str());
}

TEST_CASE("SchedulerConfig: load with missing sections uses defaults", "[config_load]") {
    // Fix #263: 部分配置缺失时使用默认值
    const std::string path = "/tmp/taskflow_test_scheduler_partial.yaml";
    {
        std::ofstream ofs(path);
        ofs << R"(
auth:
  jwt_secret: "this-is-a-very-secure-jwt-secret-32+"
encryption:
  aes_key: "0123456789abcdef0123456789abcdef"
)";
        // 缺少 server, database, log, schedule, worker_client
    }

    auto cfg = SchedulerConfig::load(path);
    // 默认值应被保留
    REQUIRE(cfg.server.http_port == 8080);
    REQUIRE(cfg.server.grpc_port == 50051);
    REQUIRE(cfg.database.host == "localhost");
    REQUIRE(cfg.database.port == 5432);
    REQUIRE(cfg.log.level == "info");
    REQUIRE(cfg.schedule.dag_drive_interval == 2);
    REQUIRE(cfg.server.tls.enabled == false);

    std::remove(path.c_str());
}

TEST_CASE("SchedulerConfig: load invalid config throws on validate", "[config_load]") {
    // Fix #263: load() 内部调用 validate()，无效配置应抛异常
    const std::string path = "/tmp/taskflow_test_scheduler_invalid.yaml";
    {
        std::ofstream ofs(path);
        ofs << R"(
auth:
  jwt_secret: "short"
encryption:
  aes_key: "0123456789abcdef0123456789abcdef"
)";
    }

    REQUIRE_THROWS_AS(SchedulerConfig::load(path), std::runtime_error);
    std::remove(path.c_str());
}

// --- WorkerConfig::load() 测试 ---

TEST_CASE("WorkerConfig: load valid config file", "[config_worker_load]") {
    // Fix #263: 验证 WorkerConfig::load() 正常加载
    const std::string path = "/tmp/taskflow_test_worker_valid.yaml";
    {
        std::ofstream ofs(path);
        ofs << R"(
server:
  grpc_port: 50053
  advertise_address: "10.0.0.1:50053"
  tls:
    enabled: true
    cert_path: "/etc/ssl/worker-cert.pem"
    key_path: "/etc/ssl/worker-key.pem"
scheduler:
  address: "scheduler.host:50051"
  tls:
    enabled: false
worker:
  name: "worker-1"
  max_tasks: 20
  resource_tags:
    - "gpu"
    - "highmem"
log:
  level: "debug"
  file_path: "/var/log/worker.log"
task_log:
  dir: "/var/log/tasks"
  retention_days: 60
  sink_type: "elasticsearch"
  es_url: "http://es:9200"
  es_index: "worker-logs"
)";
    }

    auto cfg = WorkerConfig::load(path);
    REQUIRE(cfg.server.grpc_port == 50053);
    REQUIRE(cfg.server.advertise_address == "10.0.0.1:50053");
    REQUIRE(cfg.server.tls.enabled == true);
    REQUIRE(cfg.server.tls.cert_path == "/etc/ssl/worker-cert.pem");
    REQUIRE(cfg.scheduler.address == "scheduler.host:50051");
    REQUIRE(cfg.scheduler.tls.enabled == false);
    REQUIRE(cfg.worker.name == "worker-1");
    REQUIRE(cfg.worker.max_tasks == 20);
    REQUIRE(cfg.worker.resource_tags.size() == 2);
    REQUIRE(cfg.worker.resource_tags[0] == "gpu");
    REQUIRE(cfg.worker.resource_tags[1] == "highmem");
    REQUIRE(cfg.log.level == "debug");
    REQUIRE(cfg.log.file_path == "/var/log/worker.log");
    REQUIRE(cfg.task_log.dir == "/var/log/tasks");
    REQUIRE(cfg.task_log.retention_days == 60);
    REQUIRE(cfg.task_log.sink_type == "elasticsearch");
    REQUIRE(cfg.task_log.es_url == "http://es:9200");
    REQUIRE(cfg.task_log.es_index == "worker-logs");

    std::remove(path.c_str());
}

TEST_CASE("WorkerConfig: load non-existent file throws", "[config_worker_load]") {
    // Fix #263: 文件不存在时抛 std::runtime_error
    REQUIRE_THROWS_AS(WorkerConfig::load("/tmp/taskflow_nonexistent_worker.yaml"),
                      std::runtime_error);
}

TEST_CASE("WorkerConfig: load with missing sections uses defaults", "[config_worker_load]") {
    // Fix #263: 部分配置缺失时使用默认值
    const std::string path = "/tmp/taskflow_test_worker_partial.yaml";
    {
        std::ofstream ofs(path);
        ofs << R"(
worker:
  max_tasks: 15
)";
    }

    auto cfg = WorkerConfig::load(path);
    REQUIRE(cfg.server.grpc_port == 50052);  // 默认值
    REQUIRE(cfg.scheduler.address == "localhost:50051");  // 默认值
    REQUIRE(cfg.worker.max_tasks == 15);  // 配置值
    REQUIRE(cfg.task_log.retention_days == 30);  // 默认值
    REQUIRE(cfg.task_log.sink_type == "file");  // 默认值

    std::remove(path.c_str());
}

// --- 异常消息内容校验 ---

TEST_CASE("SchedulerConfig: empty jwt_secret error message", "[config_error_msg]") {
    // Fix #263: 验证空 jwt_secret 的错误消息含 "jwt_secret" 和 "为空"
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.jwt_secret = "";
    try {
        cfg.validate();
        FAIL("Expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("jwt_secret") != std::string::npos);
        REQUIRE(std::string(e.what()).find("为空") != std::string::npos);
    }
}

TEST_CASE("SchedulerConfig: short jwt_secret error message", "[config_error_msg]") {
    // Fix #263: 验证短 jwt_secret 的错误消息含 "jwt_secret" 和 "32"
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.jwt_secret = "short";  // 5 字符
    try {
        cfg.validate();
        FAIL("Expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("jwt_secret") != std::string::npos);
        REQUIRE(std::string(e.what()).find("32") != std::string::npos);
    }
}

TEST_CASE("SchedulerConfig: empty aes_key error message", "[config_error_msg]") {
    // Fix #263: 验证空 aes_key 的错误消息含 "aes_key" 和 "为空"
    auto cfg = makeValidSchedulerConfig();
    cfg.encryption.aes_key = "";
    try {
        cfg.validate();
        FAIL("Expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("aes_key") != std::string::npos);
        REQUIRE(std::string(e.what()).find("为空") != std::string::npos);
    }
}

TEST_CASE("SchedulerConfig: wrong length aes_key error message", "[config_error_msg]") {
    // Fix #263: 验证错误长度 aes_key 的错误消息含 "aes_key" 和 "32"
    auto cfg = makeValidSchedulerConfig();
    cfg.encryption.aes_key = "0123456789abcdef";  // 16 字符
    try {
        cfg.validate();
        FAIL("Expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("aes_key") != std::string::npos);
        REQUIRE(std::string(e.what()).find("32") != std::string::npos);
    }
}

TEST_CASE("SchedulerConfig: invalid http_port error message", "[config_error_msg]") {
    // Fix #263: 验证无效 http_port 的错误消息含 "http_port"
    auto cfg = makeValidSchedulerConfig();
    cfg.server.http_port = 0;
    try {
        cfg.validate();
        FAIL("Expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("http_port") != std::string::npos);
    }
}

TEST_CASE("SchedulerConfig: invalid grpc_port error message", "[config_error_msg]") {
    // Fix #263: 验证无效 grpc_port 的错误消息含 "grpc_port"
    auto cfg = makeValidSchedulerConfig();
    cfg.server.grpc_port = 70000;
    try {
        cfg.validate();
        FAIL("Expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("grpc_port") != std::string::npos);
    }
}

TEST_CASE("SchedulerConfig: max_connections < min_connections error message", "[config_error_msg]") {
    // Fix #263: 验证 max < min 的错误消息
    auto cfg = makeValidSchedulerConfig();
    cfg.database.min_connections = 10;
    cfg.database.max_connections = 5;
    try {
        cfg.validate();
        FAIL("Expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("max_connections") != std::string::npos);
    }
}

// --- 边界值测试 ---

TEST_CASE("SchedulerConfig: jwt_secret exactly 32 chars passes", "[config_boundary]") {
    // Fix #263: jwt_secret 恰好 32 字符应通过（边界值）
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.jwt_secret = std::string(32, 'a');
    REQUIRE_NOTHROW(cfg.validate());
}

TEST_CASE("SchedulerConfig: jwt_secret 31 chars throws", "[config_boundary]") {
    // Fix #263: jwt_secret 31 字符（边界-1）应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.jwt_secret = std::string(31, 'a');
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: aes_key exactly 32 chars passes", "[config_boundary]") {
    // Fix #263: aes_key 恰好 32 字符应通过（边界值）
    auto cfg = makeValidSchedulerConfig();
    cfg.encryption.aes_key = std::string(32, 'b');
    REQUIRE_NOTHROW(cfg.validate());
}

TEST_CASE("SchedulerConfig: aes_key 31 chars throws", "[config_boundary]") {
    // Fix #263: aes_key 31 字符（边界-1）应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.encryption.aes_key = std::string(31, 'b');
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: grpc_port negative throws", "[config_boundary]") {
    // Fix #263: grpc_port 负值应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.server.grpc_port = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: grpc_port 65535 passes (upper bound)", "[config_boundary]") {
    // Fix #263: grpc_port 65535（上界）应通过
    auto cfg = makeValidSchedulerConfig();
    cfg.server.grpc_port = 65535;
    REQUIRE_NOTHROW(cfg.validate());
}

TEST_CASE("SchedulerConfig: heartbeat_check_interval negative throws", "[config_boundary]") {
    // Fix #263: heartbeat_check_interval 负值应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.heartbeat_check_interval = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: heartbeat_timeout negative throws", "[config_boundary]") {
    // Fix #263: heartbeat_timeout 负值应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.heartbeat_timeout = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: leader_lease_interval negative throws", "[config_boundary]") {
    // Fix #263: leader_lease_interval 负值应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.leader_lease_interval = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: max_connections equals min passes", "[config_boundary]") {
    // Fix #263: max_connections == min_connections 应通过
    auto cfg = makeValidSchedulerConfig();
    cfg.database.min_connections = 10;
    cfg.database.max_connections = 10;
    REQUIRE_NOTHROW(cfg.validate());
}

// ============================================================================
// Fix #280: SchedulerConfig 补充校验测试
// 原校验遗漏 timeout_check_interval / access_token_ttl / refresh_token_ttl /
// database.port。0 或负值会导致 tight-loop spinning、token 立即过期、连接失败。
// ============================================================================

TEST_CASE("SchedulerConfig: timeout_check_interval zero throws", "[config_boundary]") {
    // Fix #280: timeout_check_interval=0 会导致任务超时检查 tight-loop spinning
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.timeout_check_interval = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: timeout_check_interval negative throws", "[config_boundary]") {
    // Fix #280: timeout_check_interval 负值应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.schedule.timeout_check_interval = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: access_token_ttl zero throws", "[config_boundary]") {
    // Fix #280: access_token_ttl=0 会导致 access token 立即过期，无法登录
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.access_token_ttl = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: access_token_ttl negative throws", "[config_boundary]") {
    // Fix #280: access_token_ttl 负值应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.access_token_ttl = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: refresh_token_ttl zero throws", "[config_boundary]") {
    // Fix #280: refresh_token_ttl=0 会导致 refresh token 立即过期
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.refresh_token_ttl = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: refresh_token_ttl negative throws", "[config_boundary]") {
    // Fix #280: refresh_token_ttl 负值应失败
    auto cfg = makeValidSchedulerConfig();
    cfg.auth.refresh_token_ttl = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: database_port zero throws", "[config_boundary]") {
    // Fix #280: database.port=0 无效
    auto cfg = makeValidSchedulerConfig();
    cfg.database.port = 0;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: database_port negative throws", "[config_boundary]") {
    // Fix #280: database.port 负值无效
    auto cfg = makeValidSchedulerConfig();
    cfg.database.port = -1;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: database_port 65536 throws (upper bound)", "[config_boundary]") {
    // Fix #280: database.port 超过 65535 无效
    auto cfg = makeValidSchedulerConfig();
    cfg.database.port = 65536;
    REQUIRE_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("SchedulerConfig: database_port 65535 passes (upper bound)", "[config_boundary]") {
    // Fix #280: database.port=65535（上界）应通过
    auto cfg = makeValidSchedulerConfig();
    cfg.database.port = 65535;
    REQUIRE_NOTHROW(cfg.validate());
}

TEST_CASE("SchedulerConfig: database_port 1 passes (lower bound)", "[config_boundary]") {
    // Fix #280: database.port=1（下界）应通过
    auto cfg = makeValidSchedulerConfig();
    cfg.database.port = 1;
    REQUIRE_NOTHROW(cfg.validate());
}

// ============================================================================
// Fix #281: DatabaseConfig::connectionString() 转义测试
// 原实现直接拼接 password 等字段，含空格/单引号/反斜杠的值会破坏 libpq 连接串
// 语法或引发注入。这些测试验证转义逻辑。
// ============================================================================

TEST_CASE("DatabaseConfig: connectionString escapes password with space", "[config_db_connstr]") {
    // Fix #281: 含空格的 password 需用单引号包裹
    DatabaseConfig db;
    db.host = "localhost";
    db.port = 5432;
    db.name = "taskflow";
    db.user = "taskflow";
    db.password = "my password";
    std::string conn = db.connectionString();
    // 应包含 password='my password'
    REQUIRE(conn.find("password='my password'") != std::string::npos);
}

TEST_CASE("DatabaseConfig: connectionString escapes password with single quote", "[config_db_connstr]") {
    // Fix #281: 含单引号的 password 需转义并包裹
    DatabaseConfig db;
    db.host = "localhost";
    db.port = 5432;
    db.name = "taskflow";
    db.user = "taskflow";
    db.password = "pa'ss";
    std::string conn = db.connectionString();
    // 单引号应被反斜杠转义：password='pa\'ss'
    REQUIRE(conn.find("password='pa\\'ss'") != std::string::npos);
}

TEST_CASE("DatabaseConfig: connectionString escapes password with backslash", "[config_db_connstr]") {
    // Fix #281: 含反斜杠的 password 需转义并包裹
    DatabaseConfig db;
    db.host = "localhost";
    db.port = 5432;
    db.name = "taskflow";
    db.user = "taskflow";
    db.password = "pa\\ss";
    std::string conn = db.connectionString();
    // 反斜杠应被转义为双反斜杠：password='pa\\ss'
    REQUIRE(conn.find("password='pa\\\\ss'") != std::string::npos);
}

TEST_CASE("DatabaseConfig: connectionString no escape for simple password", "[config_db_connstr]") {
    // Fix #281: 不含特殊字符的 password 不应被包裹
    DatabaseConfig db;
    db.host = "localhost";
    db.port = 5432;
    db.name = "taskflow";
    db.user = "taskflow";
    db.password = "simplepass123";
    std::string conn = db.connectionString();
    REQUIRE(conn.find("password=simplepass123") != std::string::npos);
    // 不应被单引号包裹
    REQUIRE(conn.find("password='simplepass123'") == std::string::npos);
}

TEST_CASE("DatabaseConfig: connectionString escapes empty password", "[config_db_connstr]") {
    // Fix #281: 空 password 应输出 password=''
    DatabaseConfig db;
    db.host = "localhost";
    db.port = 5432;
    db.name = "taskflow";
    db.user = "taskflow";
    db.password = "";
    std::string conn = db.connectionString();
    REQUIRE(conn.find("password=''") != std::string::npos);
}

TEST_CASE("DatabaseConfig: connectionString escapes host with space", "[config_db_connstr]") {
    // Fix #281: host 含空格也需转义
    DatabaseConfig db;
    db.host = "my host";
    db.port = 5432;
    db.name = "taskflow";
    db.user = "taskflow";
    db.password = "pass";
    std::string conn = db.connectionString();
    REQUIRE(conn.find("host='my host'") != std::string::npos);
}
