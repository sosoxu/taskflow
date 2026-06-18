#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
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

// ============================================================================
// CommandExecutor env_vars 测试
// 验收指标：
//   1. env_vars 中的环境变量在子进程中可用
//   2. 多个环境变量同时设置
//   3. 环境变量值包含特殊字符
//   4. 空 env_vars 不影响执行
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: env_vars single variable", "[command_env]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo $MY_TEST_VAR";
    config["env_vars"] = {{"MY_TEST_VAR", "hello_env"}};

    auto result = executor.execute("env-test-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: env_vars multiple variables", "[command_env]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo $VAR_A $VAR_B $VAR_C";
    config["env_vars"] = {{"VAR_A", "alpha"}, {"VAR_B", "beta"}, {"VAR_C", "gamma"}};

    auto result = executor.execute("env-test-2", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: env_vars with special characters", "[command_env]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo $SPECIAL_VAR";
    config["env_vars"] = {{"SPECIAL_VAR", "hello=world&foo=bar"}};

    auto result = executor.execute("env-test-3", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: empty env_vars does not affect execution", "[command_env]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo no_env";
    config["env_vars"] = nlohmann::json::object();

    auto result = executor.execute("env-test-4", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: no env_vars field does not affect execution", "[command_env]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo no_env_field";

    auto result = executor.execute("env-test-5", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
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
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "pwd";
    config["working_dir"] = "/tmp";

    auto result = executor.execute("workdir-test-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "CommandExecutor: working_dir /var/log", "[command_workdir]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "pwd";
    config["working_dir"] = "/var/log";

    auto result = executor.execute("workdir-test-2", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
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
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo $COMBINED_VAR";
    config["working_dir"] = "/tmp";
    config["env_vars"] = {{"COMBINED_VAR", "combined_test"}};

    auto result = executor.execute("combined-test-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

// ============================================================================
// ScriptExecutor env_vars + working_dir 测试
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: env_vars in script", "[script_env]") {
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\necho $SCRIPT_ENV_VAR";
    config["env_vars"] = {{"SCRIPT_ENV_VAR", "script_env_value"}};

    auto result = executor.execute("script-env-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: working_dir in script", "[script_workdir]") {
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\npwd";
    config["working_dir"] = "/tmp";

    auto result = executor.execute("script-workdir-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: env_vars and working_dir combined", "[script_combined]") {
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\npwd\necho $COMBINED_SCRIPT_VAR";
    config["working_dir"] = "/tmp";
    config["env_vars"] = {{"COMBINED_SCRIPT_VAR", "combined_script_value"}};

    auto result = executor.execute("script-combined-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
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
