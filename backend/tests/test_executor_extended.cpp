#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <thread>
#include <chrono>

#include "worker/executor/command_executor.h"
#include "worker/executor/script_executor.h"
#include "worker/executor/sql_executor.h"

using namespace taskflow::worker::executor;
namespace fs = std::filesystem;

static const std::string TEST_LOG_DIR = "/tmp/taskflow_test_logs_ext";

struct LogDirFixture2 {
    LogDirFixture2() {
        fs::create_directories(TEST_LOG_DIR);
    }
};

// Fix #243: Helper to read a task log file content for output verification.
static std::string readTaskLog(const std::string& task_instance_id) {
    std::ifstream log_file(TEST_LOG_DIR + "/" + task_instance_id + ".log");
    if (!log_file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(log_file)),
                       std::istreambuf_iterator<char>());
}

// ============================================================================
// CommandExecutor env_vars 测试
// 验收指标：
//   1. env_vars 中的环境变量在子进程中可用
//   2. 多个环境变量同时设置
//   3. 环境变量值包含特殊字符
//   4. 空 env_vars 不影响执行
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: env_vars single variable", "[command_env]") {
    // Fix #243: 原测试只检查 status==SUCCESS，不读取日志验证环境变量值。
    // 现在读取日志验证 $MY_TEST_VAR 确实被替换为 "hello_env"。
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo $MY_TEST_VAR";
    config["env_vars"] = {{"MY_TEST_VAR", "hello_env"}};

    auto result = executor.execute("env-test-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("env-test-1");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("hello_env") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: env_vars multiple variables", "[command_env]") {
    // Fix #243: 验证多个环境变量同时设置并在子进程中可用
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo $VAR_A $VAR_B $VAR_C";
    config["env_vars"] = {{"VAR_A", "alpha"}, {"VAR_B", "beta"}, {"VAR_C", "gamma"}};

    auto result = executor.execute("env-test-2", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("env-test-2");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("alpha") != std::string::npos);
    REQUIRE(log.find("beta") != std::string::npos);
    REQUIRE(log.find("gamma") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: env_vars with special characters", "[command_env]") {
    // Fix #243: 验证含特殊字符的环境变量值正确传递
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo $SPECIAL_VAR";
    config["env_vars"] = {{"SPECIAL_VAR", "hello=world&foo=bar"}};

    auto result = executor.execute("env-test-3", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("env-test-3");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("hello=world&foo=bar") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: empty env_vars does not affect execution", "[command_env]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo no_env";
    config["env_vars"] = nlohmann::json::object();

    auto result = executor.execute("env-test-4", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("env-test-4");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("no_env") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: no env_vars field does not affect execution", "[command_env]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo no_env_field";

    auto result = executor.execute("env-test-5", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("env-test-5");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("no_env_field") != std::string::npos);
}

// ============================================================================
// CommandExecutor working_dir 测试
// 验收指标：
//   1. working_dir 切换子进程工作目录
//   2. 无效 working_dir 返回 FAILED
//   3. 空 working_dir 不影响执行
//   4. 不设置 working_dir 不影响执行
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: working_dir /tmp", "[command_workdir]") {
    // Fix #243: 验证 pwd 输出确实是 /tmp
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "pwd";
    config["working_dir"] = "/tmp";

    auto result = executor.execute("workdir-test-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("workdir-test-1");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("/tmp") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: working_dir /var/log", "[command_workdir]") {
    // Fix #243: 验证 pwd 输出确实是 /var/log
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "pwd";
    config["working_dir"] = "/var/log";

    auto result = executor.execute("workdir-test-2", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("workdir-test-2");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("/var/log") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: invalid working_dir returns FAILED", "[command_workdir]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo test";
    config["working_dir"] = "/nonexistent_dir_xyz_12345";

    auto result = executor.execute("workdir-test-3", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: empty working_dir does not affect execution", "[command_workdir]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo empty_workdir";
    config["working_dir"] = "";

    auto result = executor.execute("workdir-test-4", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: no working_dir field does not affect execution", "[command_workdir]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo no_workdir";

    auto result = executor.execute("workdir-test-5", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
}

// ============================================================================
// CommandExecutor env_vars + working_dir 组合测试
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: env_vars and working_dir combined", "[command_combined]") {
    // Fix #243: 验证 env_vars 和 working_dir 同时生效
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo $COMBINED_VAR";
    config["working_dir"] = "/tmp";
    config["env_vars"] = {{"COMBINED_VAR", "combined_test"}};

    auto result = executor.execute("combined-test-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("combined-test-1");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("combined_test") != std::string::npos);
}

// ============================================================================
// ScriptExecutor env_vars + working_dir 测试
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: env_vars in script", "[script_env]") {
    // Fix #243: 验证脚本中环境变量可用
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\necho $SCRIPT_ENV_VAR";
    config["env_vars"] = {{"SCRIPT_ENV_VAR", "script_env_value"}};

    auto result = executor.execute("script-env-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("script-env-1");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("script_env_value") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: working_dir in script", "[script_workdir]") {
    // Fix #243: 验证脚本中 pwd 输出为 /tmp
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\npwd";
    config["working_dir"] = "/tmp";

    auto result = executor.execute("script-workdir-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("script-workdir-1");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("/tmp") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: env_vars and working_dir combined", "[script_combined]") {
    // Fix #243: 验证脚本中 env_vars 和 working_dir 同时生效
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\npwd\necho $COMBINED_SCRIPT_VAR";
    config["working_dir"] = "/tmp";
    config["env_vars"] = {{"COMBINED_SCRIPT_VAR", "combined_script_value"}};

    auto result = executor.execute("script-combined-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::string log = readTaskLog("script-combined-1");
    REQUIRE_FALSE(log.empty());
    REQUIRE(log.find("/tmp") != std::string::npos);
    REQUIRE(log.find("combined_script_value") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: invalid working_dir returns FAILED", "[script_workdir]") {
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\necho test";
    config["working_dir"] = "/nonexistent_dir_xyz_12345";

    auto result = executor.execute("script-workdir-invalid", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
}

// ============================================================================
// SqlExecutor 参数校验测试
// 验收指标：
//   1. 缺少必要字段返回 FAILED
//   2. db_port 整数类型支持
//   3. db_port 字符串类型支持
// ============================================================================

TEST_CASE("SqlExecutor: missing db_host returns FAILED", "[sql_executor]") {
    SqlExecutor executor;
    nlohmann::json config;
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-missing-host", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE("SqlExecutor: missing sql_statement returns FAILED", "[sql_executor]") {
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";

    auto result = executor.execute("sql-missing-stmt", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE("SqlExecutor: db_port as integer", "[sql_executor]") {
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;  // integer
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    // Should not fail due to port type - connection may fail but not validation
    auto result = executor.execute("sql-int-port", config, 10, TEST_LOG_DIR);
    // Either SUCCESS (if DB reachable) or FAILED (connection error), not a crash
    REQUIRE((result.status == "SUCCESS" || result.status == "FAILED"));
}

TEST_CASE("SqlExecutor: db_port as string", "[sql_executor]") {
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = "5432";  // string
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-str-port", config, 10, TEST_LOG_DIR);
    REQUIRE((result.status == "SUCCESS" || result.status == "FAILED"));
}

TEST_CASE("SqlExecutor: db_port as float returns FAILED", "[sql_executor]") {
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432.5;  // float - not valid
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-float-port", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
}

// ============================================================================
// Fix #254: SqlExecutor 各字段缺失和超时测试
// 验收指标：
//   1. 缺少 db_port 返回 FAILED
//   2. 缺少 db_name 返回 FAILED
//   3. 缺少 db_user 返回 FAILED
//   4. 缺少 db_password 返回 FAILED
//   5. 超时返回 TIMEOUT（连接不可达主机）
// ============================================================================

TEST_CASE("SqlExecutor: missing db_port returns FAILED", "[sql_executor_fields]") {
    // Fix #254: 验证缺少 db_port 字段返回 FAILED
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    // db_port 缺失
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-missing-port", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.error_message.find("db_port") != std::string::npos);
}

TEST_CASE("SqlExecutor: missing db_name returns FAILED", "[sql_executor_fields]") {
    // Fix #254: 验证缺少 db_name 字段返回 FAILED
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    // db_name 缺失
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-missing-name", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.error_message.find("db_name") != std::string::npos);
}

TEST_CASE("SqlExecutor: missing db_user returns FAILED", "[sql_executor_fields]") {
    // Fix #254: 验证缺少 db_user 字段返回 FAILED
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    // db_user 缺失
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-missing-user", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.error_message.find("db_user") != std::string::npos);
}

TEST_CASE("SqlExecutor: missing db_password returns FAILED", "[sql_executor_fields]") {
    // Fix #254: 验证缺少 db_password 字段返回 FAILED
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    // db_password 缺失
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-missing-password", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.error_message.find("db_password") != std::string::npos);
}

TEST_CASE("SqlExecutor: all fields missing returns FAILED on first check", "[sql_executor_fields]") {
    // Fix #254: 验证所有字段缺失时在第一个字段（db_host）检查就返回 FAILED
    SqlExecutor executor;
    nlohmann::json config;
    // 所有字段缺失
    config["db_type"] = "postgresql";

    auto result = executor.execute("sql-missing-all", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.error_message.find("db_host") != std::string::npos);
}

TEST_CASE("SqlExecutor: timeout on unreachable host returns TIMEOUT", "[sql_executor_timeout]") {
    // Fix #254: 验证连接不可达主机时超时返回 TIMEOUT
    // 使用 TEST-NET-1 (192.0.2.0/24) 保留地址，保证不可达
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "192.0.2.1";  // RFC 5737 TEST-NET-1, 不可路由
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    // 超时 3 秒，子进程连接不可达主机会卡住，父进程超时后 SIGKILL
    auto start = std::chrono::steady_clock::now();
    auto result = executor.execute("sql-timeout", config, 3, TEST_LOG_DIR);
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();

    REQUIRE(result.status == "TIMEOUT");
    REQUIRE(result.exit_code == -1);
    REQUIRE(result.error_message.find("timed out") != std::string::npos);
    // 验证确实在超时时间附近返回（不超过 10 秒）
    REQUIRE(elapsed < 10);
}
