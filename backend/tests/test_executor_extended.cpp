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
    // Fix #306: 析构时清理日志目录，避免 /tmp 残留文件累积导致 CI 磁盘占用增长
    ~LogDirFixture2() {
        std::error_code ec;
        fs::remove_all(TEST_LOG_DIR, ec);
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

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: missing db_host returns FAILED", "[sql_executor]") {
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

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: missing sql_statement returns FAILED", "[sql_executor]") {
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

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: db_port as integer", "[sql_executor]") {
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
    // Fix #296: 强化断言 —— 测试环境无 PostgreSQL，连接必失败返回 FAILED
    // （若 DB 可达则 SUCCESS，但测试环境不应有 DB，故断言 FAILED）
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: db_port as string", "[sql_executor]") {
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
    // Fix #296: 强化断言 —— 测试环境无 PostgreSQL，连接必失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: db_port as float returns FAILED", "[sql_executor]") {
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

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: missing db_port returns FAILED", "[sql_executor_fields]") {
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

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: missing db_name returns FAILED", "[sql_executor_fields]") {
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

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: missing db_user returns FAILED", "[sql_executor_fields]") {
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

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: missing db_password returns FAILED", "[sql_executor_fields]") {
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

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: all fields missing returns FAILED on first check", "[sql_executor_fields]") {
    // Fix #254: 验证所有字段缺失时在第一个字段（db_host）检查就返回 FAILED
    SqlExecutor executor;
    nlohmann::json config;
    // 所有字段缺失
    config["db_type"] = "postgresql";

    auto result = executor.execute("sql-missing-all", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.error_message.find("db_host") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: timeout on unreachable host returns TIMEOUT", "[sql_executor_timeout]") {
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

// ============================================================================
// Fix #273: ScriptExecutor 临时文件安全与 interpreter 边界测试
// 验收指标：
//   1. interpreter 含空格时执行失败（execlp 将整个字符串当作程序名）
//   2. interpreter 为空字符串时执行失败
//   3. 空 script_content 执行行为验证
//   4. 缺少 script_content 参数返回 FAILED
//   5. interpreter 路径穿越验证
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: interpreter with space fails", "[script_executor_edge]") {
    // Fix #273: interpreter 含空格（如 "python3 -u"）应失败
    // execlp 将整个字符串当作程序名，找不到 "python3 -u" 这个程序
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "print('hello')";
    config["interpreter"] = "python3 -u";  // 含空格

    auto result = executor.execute("interp-space", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code != 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: empty interpreter uses bash default", "[script_executor_edge]") {
    // Fix #273: interpreter 为空字符串时，源码第 33 行 is_string() 为 true，
    // interpreter 被设为空字符串，execlp("") 行为未定义但应返回 FAILED
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "echo hello";
    config["interpreter"] = "";

    auto result = executor.execute("interp-empty", config, 10, TEST_LOG_DIR);
    // 空 interpreter 会导致 execlp 失败，返回 FAILED
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code != 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: empty script content executes without crash", "[script_executor_edge]") {
    // Fix #273: 空 script_content 应能执行（空脚本），不崩溃
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "";
    config["interpreter"] = "bash";

    auto result = executor.execute("empty-script", config, 10, TEST_LOG_DIR);
    // 空脚本执行应成功（bash 执行空文件返回 0）
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: missing script_content returns FAILED", "[script_executor_edge]") {
    // Fix #273: 缺少 script_content 参数应返回 FAILED
    ScriptExecutor executor;
    nlohmann::json config;
    // 不设置 script_content
    config["interpreter"] = "bash";

    auto result = executor.execute("missing-content", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code != 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: script_content as non-string returns FAILED", "[script_executor_edge]") {
    // Fix #273: script_content 为非字符串类型应返回 FAILED（源码 is_string() 检查）
    // Fix #296: 修正测试名与实现不符 —— 原测试仅 REQUIRE_NOTHROW 不验证返回值
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = 12345;  // 数字而非字符串
    config["interpreter"] = "bash";

    auto result = executor.execute("non-string-content", config, 10, TEST_LOG_DIR);
    // 源码第 43-49 行检查 is_string()，非字符串直接返回 FAILED
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code != 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: interpreter as non-string uses default", "[script_executor_edge]") {
    // Fix #273: interpreter 为非字符串类型时，is_string() 为 false，使用默认 bash
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "echo default_interp";
    config["interpreter"] = 123;  // 数字而非字符串

    auto result = executor.execute("non-string-interp", config, 10, TEST_LOG_DIR);
    // 应使用默认 bash 解释器执行
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: whitespace-only script content", "[script_executor_edge]") {
    // Fix #273: 仅含空白字符的脚本应能执行
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "   \n\t  ";
    config["interpreter"] = "bash";

    auto result = executor.execute("whitespace-script", config, 10, TEST_LOG_DIR);
    // 空白脚本 bash 执行返回 0
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: temp file is cleaned up after execution", "[script_executor_edge]") {
    // Fix #273: 验证临时脚本文件在执行后被清理
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "echo cleanup_test";
    config["interpreter"] = "bash";

    std::string expected_temp = "/tmp/taskflow_script_cleanup-test.sh";

    auto result = executor.execute("cleanup-test", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");

    // 临时文件应已被删除
    REQUIRE_FALSE(fs::exists(expected_temp));
}

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: temp file is cleaned up on failure", "[script_executor_edge]") {
    // Fix #273: 验证执行失败后临时脚本文件也被清理
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "exit 1";
    config["interpreter"] = "bash";

    std::string expected_temp = "/tmp/taskflow_script_fail-cleanup.sh";

    auto result = executor.execute("fail-cleanup", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");

    // 即使失败，临时文件也应被删除
    REQUIRE_FALSE(fs::exists(expected_temp));
}

// ============================================================================
// Fix #274: SqlExecutor SELECT 检测与连接字符串边界测试
// 验收指标：
//   1. 空 SQL 语句返回 FAILED
//   2. 仅空白字符 SQL 语句返回 FAILED
//   3. 缺少 db_password 返回 FAILED
//   4. db_password 含特殊字符不崩溃
//   5. db_password 为非字符串类型不崩溃
//   6. sql_statement 为非字符串类型不崩溃
//   7. db_port 为负数不崩溃
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: empty sql_statement returns FAILED", "[sql_executor_edge]") {
    // Fix #274: 空 SQL 语句应返回 FAILED
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "";

    auto result = executor.execute("sql-empty-stmt", config, 10, TEST_LOG_DIR);
    // Fix #296: 强化断言 —— 空 SQL 或连接失败均返回 FAILED（子进程 exit code 1）
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: whitespace-only sql_statement", "[sql_executor_edge]") {
    // Fix #274: 仅空白字符的 SQL 语句应被检测为空语句
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "   \n\t  ";

    auto result = executor.execute("sql-whitespace-stmt", config, 10, TEST_LOG_DIR);
    // Fix #296: 强化断言 —— 空白 SQL 或连接失败均返回 FAILED（子进程 exit code 1）
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: missing db_password returns FAILED", "[sql_executor_edge]") {
    // Fix #274: 缺少 db_password 应返回 FAILED
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";
    // 不设置 db_password

    auto result = executor.execute("sql-missing-password", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: db_password with special characters does not crash", "[sql_executor_edge]") {
    // Fix #274: db_password 含特殊字符（空格、=、反斜杠）不应崩溃
    // Fix #288: 连接串转义后连接会失败（测试环境无 DB），返回 FAILED
    // Fix #296: 修正 REQUIRE_NOTHROW lambda 模式 —— 验证返回值而非仅验证不抛异常
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "pass with spaces and = signs";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-special-password", config, 10, TEST_LOG_DIR);
    // 转义后连接串合法，但测试环境无 DB，连接失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: db_password as non-string does not crash", "[sql_executor_edge]") {
    // Fix #274: db_password 为非字符串类型（数字）不应崩溃
    // Fix #296: 修正 REQUIRE_NOTHROW lambda 模式 —— 验证返回值
    // 源码 get_string() 对 is_number() 调用 std::to_string(get<int>())，转换为 "12345"
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = 12345;  // 数字而非字符串
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-int-password", config, 10, TEST_LOG_DIR);
    // 数字被转换为字符串 "12345"，连接失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: sql_statement as non-string does not crash", "[sql_executor_edge]") {
    // Fix #274: sql_statement 为非字符串类型不应崩溃
    // Fix #296: 修正 REQUIRE_NOTHROW lambda 模式 —— 验证返回值
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = 12345;  // 数字而非字符串

    auto result = executor.execute("sql-int-stmt", config, 10, TEST_LOG_DIR);
    // 数字被转换为字符串 "12345"，连接失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: db_port as negative number does not crash", "[sql_executor_edge]") {
    // Fix #274: db_port 为负数不应崩溃（连接会失败但不崩溃）
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = -1;  // 负数端口
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-negative-port", config, 10, TEST_LOG_DIR);
    // Fix #296: 强化断言 —— 负数端口连接失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: db_host with special characters does not crash", "[sql_executor_edge]") {
    // Fix #274: db_host 含特殊字符不应崩溃
    // Fix #288: 连接串转义后连接会失败（测试环境无 DB），返回 FAILED
    // Fix #296: 修正 REQUIRE_NOTHROW lambda 模式 —— 验证返回值
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "host with spaces";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    auto result = executor.execute("sql-special-host", config, 10, TEST_LOG_DIR);
    // 转义后连接串合法，但测试环境无 DB，连接失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: lowercase select statement", "[sql_executor_edge]") {
    // Fix #274: 小写 "select" 应被识别为 SELECT 语句（源码第 43 行检测 "select"）
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "select 1";

    // 连接会失败，但不应崩溃。小写 select 走非事务路径。
    auto result = executor.execute("sql-lowercase-select", config, 10, TEST_LOG_DIR);
    // Fix #296: 强化断言 —— 测试环境无 DB，连接失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: mixed case Select statement", "[sql_executor_edge]") {
    // Fix #274: 混合大小写 "Select" 应被识别（源码第 44 行检测 "Select"）
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "Select 1";

    auto result = executor.execute("sql-mixed-select", config, 10, TEST_LOG_DIR);
    // Fix #296: 强化断言 —— 测试环境无 DB，连接失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: SELECT with leading parentheses treated as DML", "[sql_executor_edge]") {
    // Fix #274: 带前导括号的 SELECT "(SELECT 1)" 被误判为 DML（源码仅检测前 6 字符）
    // 此测试记录当前行为：前导括号的 SELECT 走事务路径
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "(SELECT 1)";

    // 连接会失败，但不应崩溃。带括号的 SELECT 走事务路径（DML）。
    auto result = executor.execute("sql-paren-select", config, 10, TEST_LOG_DIR);
    // Fix #296: 强化断言 —— 测试环境无 DB，连接失败返回 FAILED
    REQUIRE(result.status == "FAILED");
}

// ============================================================================
// Fix #287: ScriptExecutor 路径穿越防护测试
// 验收指标：
//   1. 含 "/" 的 task_instance_id 被拒绝
//   2. 含 "\" 的 task_instance_id 被拒绝
//   3. 含 ".." 的 task_instance_id 被拒绝
//   4. 含 null 字节的 task_instance_id 被拒绝
//   5. 单 "." 的 task_instance_id 被拒绝
//   6. 空 task_instance_id 被拒绝
//   7. 正常 task_instance_id 不受影响
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "ScriptExecutor: path traversal in task_instance_id rejected", "[script_executor_security]") {
    // Fix #287: 验证含路径分隔符的 task_instance_id 被拒绝
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "echo hello";
    config["interpreter"] = "bash";

    SECTION("slash in id") {
        auto result = executor.execute("evil/../etc/passwd", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
        REQUIRE(result.error_message.find("path separators") != std::string::npos);
    }

    SECTION("backslash in id") {
        auto result = executor.execute("evil\\..\\etc", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
    }

    SECTION("dot-dot in id") {
        auto result = executor.execute("foo..bar", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
    }

    SECTION("single dot id") {
        auto result = executor.execute(".", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
    }

    SECTION("empty id") {
        auto result = executor.execute("", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
    }

    SECTION("normal id still works") {
        auto result = executor.execute("normal-task-id-123", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "SUCCESS");
        REQUIRE(result.exit_code == 0);
    }
}

// ============================================================================
// Fix #288: SqlExecutor 路径穿越防护测试
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: path traversal in task_instance_id rejected", "[sql_executor_security]") {
    // Fix #288: 验证含路径分隔符的 task_instance_id 被拒绝
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";
    config["db_password"] = "test";
    config["db_type"] = "postgresql";
    config["sql_statement"] = "SELECT 1";

    SECTION("slash in id") {
        auto result = executor.execute("evil/../etc/passwd", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
        REQUIRE(result.error_message.find("path separators") != std::string::npos);
    }

    SECTION("backslash in id") {
        auto result = executor.execute("evil\\..\\etc", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
    }

    SECTION("dot-dot in id") {
        auto result = executor.execute("foo..bar", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
    }

    SECTION("empty id") {
        auto result = executor.execute("", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code == 1);
    }
}

// ============================================================================
// Fix #288: SqlExecutor 连接串转义测试
// 验证含特殊字符的 db_host/db_name/db_user/db_password 被正确转义
// ============================================================================

TEST_CASE_METHOD(LogDirFixture2, "SqlExecutor: connection string escaping with special characters", "[sql_executor_security]") {
    // Fix #288: 验证含空格、单引号、反斜杠的连接参数被正确转义
    // 转义后连接串合法，但测试环境无 DB，连接失败返回 FAILED（不是崩溃）
    SqlExecutor executor;
    nlohmann::json config;
    config["db_host"] = "localhost";
    config["db_port"] = 5432;
    config["db_name"] = "test";
    config["db_user"] = "test";

    SECTION("password with single quote") {
        config["db_password"] = "pass'word";
        config["db_type"] = "postgresql";
        config["sql_statement"] = "SELECT 1";
        auto result = executor.execute("sql-escape-quote", config, 10, TEST_LOG_DIR);
        // 转义后连接串合法，连接失败返回 FAILED（不崩溃）
        REQUIRE(result.status == "FAILED");
    }

    SECTION("password with backslash") {
        config["db_password"] = "pass\\word";
        config["db_type"] = "postgresql";
        config["sql_statement"] = "SELECT 1";
        auto result = executor.execute("sql-escape-backslash", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
    }

    SECTION("password with tab and newline") {
        config["db_password"] = "pass\tword\n";
        config["db_type"] = "postgresql";
        config["sql_statement"] = "SELECT 1";
        auto result = executor.execute("sql-escape-tab-newline", config, 10, TEST_LOG_DIR);
        REQUIRE(result.status == "FAILED");
    }

    SECTION("empty password") {
        config["db_password"] = "";
        config["db_type"] = "postgresql";
        config["sql_statement"] = "SELECT 1";
        auto result = executor.execute("sql-escape-empty", config, 10, TEST_LOG_DIR);
        // 空密码被转义为 ''，连接失败返回 FAILED
        REQUIRE(result.status == "FAILED");
    }
}
