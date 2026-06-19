#include <catch2/catch_test_macros.hpp>
#include <string>
#include <stdexcept>

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
