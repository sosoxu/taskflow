#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <thread>
#include <chrono>
#include <sys/stat.h>

#include "worker/executor/task_executor.h"
#include "worker/executor/command_executor.h"
#include "worker/executor/script_executor.h"
#include "worker/executor/log_sink.h"

using namespace taskflow::worker::executor;
namespace fs = std::filesystem;

// ============================================================================
// §6.2 Command 执行器
// 验收指标：
//   1. 执行简单命令返回 exit_code=0, status=SUCCESS
//   2. 执行失败命令返回 exit_code!=0, status=FAILED
//   3. 缺少 command 参数返回 FAILED
//   4. 超时命令返回 status=TIMEOUT
//   5. 使用 /bin/sh -c 执行（支持 shell 操作符 &&, ||, |, ; 等）
// ============================================================================

// Helper to create log directory before tests
static const std::string TEST_LOG_DIR = "/tmp/taskflow_test_logs";

struct LogDirFixture {
    LogDirFixture() {
        fs::create_directories(TEST_LOG_DIR);
    }
};

TEST_CASE_METHOD(LogDirFixture, "CommandExecutor: execute echo command", "[command_executor]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "/bin/echo hello";

    auto result = executor.execute("test-instance-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture, "CommandExecutor: execute failing command", "[command_executor]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "/bin/ls /nonexistent_dir_xyz";

    auto result = executor.execute("test-instance-2", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code != 0);
}

TEST_CASE_METHOD(LogDirFixture, "CommandExecutor: missing command parameter", "[command_executor]") {
    CommandExecutor executor;
    nlohmann::json config;

    auto result = executor.execute("test-instance-3", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code == 1);
}

TEST_CASE_METHOD(LogDirFixture, "CommandExecutor: empty command", "[command_executor]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "";

    auto result = executor.execute("test-instance-4", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture, "CommandExecutor: timeout", "[command_executor]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "/bin/sleep 60";

    auto result = executor.execute("test-instance-5", config, 2, TEST_LOG_DIR);
    REQUIRE(result.status == "TIMEOUT");
}

// §6.2 验收指标 5: 使用 /bin/sh -c 执行（支持 shell 操作符）
// Fix #237: 实现使用 /bin/sh -c，会解释 shell 元字符 (&&, ||, |, ; 等)。
// 原测试名称 "execvp does not interpret shell metacharacters" 与实现矛盾，
// 且断言接受 SUCCESS 或 FAILED 两种结果，无法验证任何行为。
TEST_CASE_METHOD(LogDirFixture, "CommandExecutor: /bin/sh -c interprets shell metacharacters", "[command_executor]") {
    CommandExecutor executor;
    nlohmann::json config;
    // "&&" 是 shell 操作符：echo hello 成功后才会执行 echo world
    config["command"] = "/bin/echo hello && /bin/echo world";

    auto result = executor.execute("test-instance-6", config, 5, TEST_LOG_DIR);
    // shell 解释了 &&，整条命令成功执行
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    // 读取日志验证两个 echo 都执行了（shell 解释 && 的证据）
    std::ifstream log_file(TEST_LOG_DIR + "/test-instance-6.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("hello") != std::string::npos);
    REQUIRE(content.find("world") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture, "CommandExecutor: shell short-circuit operator stops on failure", "[command_executor]") {
    // Fix #237: 进一步验证 shell 元字符被解释 —— "||" 短路逻辑
    CommandExecutor executor;
    nlohmann::json config;
    // false 失败后，由于 ||，会执行 /bin/echo fallback
    config["command"] = "/bin/false || /bin/echo fallback_ran";

    auto result = executor.execute("test-instance-7", config, 5, TEST_LOG_DIR);
    // 整条命令因 echo 成功而返回 0
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::ifstream log_file(TEST_LOG_DIR + "/test-instance-7.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("fallback_ran") != std::string::npos);
}

// ============================================================================
// §6.3 Script 执行器
// 验收指标：
//   1. 执行简单脚本返回 SUCCESS
//   2. 执行失败脚本返回 FAILED
//   3. 缺少 script_content 参数返回 FAILED
//   4. 超时脚本返回 TIMEOUT
// ============================================================================

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: execute simple script", "[script_executor]") {
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\necho 'hello from script'\nexit 0";

    auto result = executor.execute("test-script-1", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: execute failing script", "[script_executor]") {
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\nexit 42";

    auto result = executor.execute("test-script-2", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code == 42);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: missing script_content", "[script_executor]") {
    ScriptExecutor executor;
    nlohmann::json config;

    auto result = executor.execute("test-script-3", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code == 1);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: timeout", "[script_executor]") {
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\nsleep 60";

    auto result = executor.execute("test-script-4", config, 2, TEST_LOG_DIR);
    REQUIRE(result.status == "TIMEOUT");
}

// ============================================================================
// §10.2 任务类型插件机制
// 验收指标：
//   1. 内置类型 command/script/sql 可创建执行器
//   2. registerExecutor 注册自定义类型后可创建执行器
//   3. 未知类型返回 nullptr
// ============================================================================

class MockExecutor : public TaskExecutorBase {
public:
    TaskResult execute(const std::string& /*task_instance_id*/,
                       const nlohmann::json& /*config*/,
                       int /*timeout*/,
                       const std::string& /*log_dir*/,
                       std::function<void(pid_t)> /*pid_callback*/ = nullptr,
                       LogSink* /*log_sink*/ = nullptr) override {
        TaskResult result;
        result.status = "SUCCESS";
        result.exit_code = 0;
        result.error_message = "mock_executor";
        return result;
    }
};

TEST_CASE("TaskExecutor: built-in command type", "[task_executor]") {
    TaskExecutor executor(10);
    // submit command type task
    nlohmann::json config;
    config["command"] = "/bin/echo test";

    std::atomic<bool> callback_called{false};
    TaskResult callback_result;

    auto submit_result = executor.submit("test-1", "command", config, 10,
        TEST_LOG_DIR, "",
        [&](const TaskResult& r) {
            callback_result = r;
            callback_called = true;
        });

    REQUIRE(submit_result.ok());

    // Wait for callback
    for (int i = 0; i < 50 && !callback_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(callback_called);
    REQUIRE(callback_result.status == "SUCCESS");
}

TEST_CASE("TaskExecutor: register custom executor", "[task_executor]") {
    TaskExecutor executor(10);

    executor.registerExecutor("custom_type", []() -> std::unique_ptr<TaskExecutorBase> {
        return std::make_unique<MockExecutor>();
    });

    nlohmann::json config;
    std::atomic<bool> callback_called{false};
    TaskResult callback_result;

    auto submit_result = executor.submit("test-2", "custom_type", config, 10,
TEST_LOG_DIR, "",
        [&](const TaskResult& r) {
            callback_result = r;
            callback_called = true;
        });

    REQUIRE(submit_result.ok());

    for (int i = 0; i < 50 && !callback_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(callback_called);
    REQUIRE(callback_result.status == "SUCCESS");
    REQUIRE(callback_result.error_message == "mock_executor");
}

TEST_CASE("TaskExecutor: unknown task type returns error", "[task_executor]") {
    TaskExecutor executor(10);
    nlohmann::json config;

    auto result = executor.submit("test-3", "unknown_type", config, 10,
TEST_LOG_DIR, "", nullptr);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("TaskExecutor: max tasks limit", "[task_executor]") {
    TaskExecutor executor(1);  // max 1 concurrent task
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\nsleep 5";

    std::atomic<int> callback_count{0};
    auto callback = [&](const TaskResult&) { callback_count++; };

    // Submit first task (should succeed)
    auto r1 = executor.submit("max-1", "script", config, 10,
TEST_LOG_DIR, "", callback);
    REQUIRE(r1.ok());

    // Submit second task (should fail due to limit)
    auto r2 = executor.submit("max-2", "script", config, 10,
TEST_LOG_DIR, "", callback);
    REQUIRE_FALSE(r2.ok());

    // Wait for first task to complete
    for (int i = 0; i < 100 && callback_count < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ============================================================================
// §8.5 日志存储后端可切换
// 验收指标：
//   1. FileLogSink 可写入和读取日志
//   2. ElasticLogSink 回退到 FileLogSink
//   3. createLogSink 工厂函数根据类型创建
// ============================================================================

TEST_CASE("FileLogSink: write and read", "[log_sink]") {
    const std::string base_dir = "/tmp/taskflow_test_logs_sink";
    fs::create_directories(base_dir);

    FileLogSink sink(base_dir);

    std::string data = "Hello log line 1\nHello log line 2\n";
    bool write_ok = sink.write("wf-1", "task-1", data);
    REQUIRE(write_ok);

    REQUIRE(sink.exists("wf-1", "task-1"));

    std::string read_data = sink.read("wf-1", "task-1");
    REQUIRE(read_data == data);

    // Cleanup
    fs::remove_all(base_dir);
}

TEST_CASE("FileLogSink: read non-existent log returns empty", "[log_sink]") {
    const std::string base_dir = "/tmp/taskflow_test_logs_sink_ne";
    fs::create_directories(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE_FALSE(sink.exists("wf-nonexistent", "task-nonexistent"));
    REQUIRE(sink.read("wf-nonexistent", "task-nonexistent") == "");

    fs::remove_all(base_dir);
}

TEST_CASE("FileLogSink: getLogPath returns correct path", "[log_sink]") {
    const std::string base_dir = "/tmp/taskflow_test_logs_sink_path";
    FileLogSink sink(base_dir);

    std::string path = sink.getLogPath("wf-1", "task-1");
    REQUIRE(path.find(base_dir) != std::string::npos);
    REQUIRE(path.find("wf-1") != std::string::npos);
    REQUIRE(path.find("task-1") != std::string::npos);
}

TEST_CASE("ElasticLogSink: falls back to file storage", "[log_sink]") {
    const std::string base_dir = "/tmp/taskflow_test_logs_sink_es";
    fs::create_directories(base_dir);

    ElasticLogSink sink(base_dir);

    std::string data = "Elastic log test\n";
    bool write_ok = sink.write("wf-es-1", "task-es-1", data);
    REQUIRE(write_ok);

    std::string read_data = sink.read("wf-es-1", "task-es-1");
    REQUIRE(read_data == data);

    fs::remove_all(base_dir);
}

TEST_CASE("createLogSink: creates FileLogSink for file type", "[log_sink]") {
    auto sink = createLogSink("file", "/tmp/taskflow_test_logs_factory");
    REQUIRE(sink != nullptr);
}

TEST_CASE("createLogSink: creates ElasticLogSink for elasticsearch type", "[log_sink]") {
    auto sink = createLogSink("elasticsearch", "/tmp/taskflow_test_logs_factory", "http://localhost:9200", "taskflow-logs");
    REQUIRE(sink != nullptr);
}
