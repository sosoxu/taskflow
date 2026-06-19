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

// ============================================================================
// Fix #252: LogSink cleanup/append/多任务隔离测试
// 验收指标：
//   1. 同一 task 多次 write 是追加（append）而非覆盖
//   2. 不同 workflow_instance 的日志目录隔离
//   3. FileLogSink::cleanup(retention_days) 清理过期日志目录
//   4. ElasticLogSink 的 exists/cleanup/getLogPath 委托 FileLogSink
//   5. createLogSink 未知类型和空类型回退到 FileLogSink
// ============================================================================

TEST_CASE("FileLogSink: multiple writes append to same log", "[log_sink_append]") {
    // Fix #252: 验证 write 使用追加模式，多次写入不会覆盖之前的内容
    const std::string base_dir = "/tmp/taskflow_test_logs_append";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE(sink.write("wf-append", "task-append", "line1\n"));
    REQUIRE(sink.write("wf-append", "task-append", "line2\n"));
    REQUIRE(sink.write("wf-append", "task-append", "line3\n"));

    std::string content = sink.read("wf-append", "task-append");
    REQUIRE(content == "line1\nline2\nline3\n");

    fs::remove_all(base_dir);
}

TEST_CASE("FileLogSink: different workflow instances are isolated", "[log_sink_isolation]") {
    // Fix #252: 验证不同 workflow_instance_id 的日志写入不同目录，互不干扰
    const std::string base_dir = "/tmp/taskflow_test_logs_isolation";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    FileLogSink sink(base_dir);

    // 同一 task_instance_id 但不同 workflow_instance_id，应写入不同文件
    REQUIRE(sink.write("wf-A", "task-shared", "data-from-wf-A\n"));
    REQUIRE(sink.write("wf-B", "task-shared", "data-from-wf-B\n"));

    REQUIRE(sink.exists("wf-A", "task-shared"));
    REQUIRE(sink.exists("wf-B", "task-shared"));

    std::string content_a = sink.read("wf-A", "task-shared");
    std::string content_b = sink.read("wf-B", "task-shared");
    REQUIRE(content_a == "data-from-wf-A\n");
    REQUIRE(content_b == "data-from-wf-B\n");

    // 验证目录结构隔离
    std::string path_a = sink.getLogPath("wf-A", "task-shared");
    std::string path_b = sink.getLogPath("wf-B", "task-shared");
    REQUIRE(path_a != path_b);
    REQUIRE(fs::path(path_a).parent_path() != fs::path(path_b).parent_path());

    fs::remove_all(base_dir);
}

TEST_CASE("FileLogSink: cleanup removes expired log directories", "[log_sink_cleanup]") {
    // Fix #252: 验证 cleanup(retention_days) 删除修改时间超过保留期的目录
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    FileLogSink sink(base_dir);

    // 写入两个 workflow 的日志
    REQUIRE(sink.write("wf-old", "task-1", "old data\n"));
    REQUIRE(sink.write("wf-new", "task-1", "new data\n"));

    // 将 wf-old 目录的修改时间设为 30 天前
    std::string old_dir = base_dir + "/wf-old";
    auto old_time = std::filesystem::file_time_type::clock::now()
                    - std::chrono::hours(24 * 30);
    std::filesystem::last_write_time(old_dir, old_time);

    // cleanup 保留 7 天
    sink.cleanup(7);

    // wf-old 应被清理，wf-new 应保留
    REQUIRE_FALSE(fs::exists(old_dir));
    REQUIRE(fs::exists(base_dir + "/wf-new"));

    fs::remove_all(base_dir);
}

TEST_CASE("FileLogSink: cleanup keeps logs within retention period", "[log_sink_cleanup]") {
    // Fix #252: 验证 cleanup 不删除保留期内的目录
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup_keep";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE(sink.write("wf-recent", "task-1", "recent data\n"));

    // 将目录修改时间设为 3 天前（在 7 天保留期内）
    std::string dir = base_dir + "/wf-recent";
    auto recent_time = std::filesystem::file_time_type::clock::now()
                       - std::chrono::hours(24 * 3);
    std::filesystem::last_write_time(dir, recent_time);

    sink.cleanup(7);

    REQUIRE(fs::exists(dir));
    REQUIRE(sink.exists("wf-recent", "task-1"));

    fs::remove_all(base_dir);
}

TEST_CASE("FileLogSink: cleanup on non-existent base_dir does not crash", "[log_sink_cleanup]") {
    // Fix #252: 验证 cleanup 对不存在的 base_dir 安全（不崩溃）
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup_nonexist";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);
    // 不应抛异常
    REQUIRE_NOTHROW(sink.cleanup(7));
}

TEST_CASE("ElasticLogSink: delegates exists/getLogPath to file sink", "[log_sink_es_delegation]") {
    // Fix #252: 验证 ElasticLogSink 的 exists/getLogPath 委托给 FileLogSink
    const std::string base_dir = "/tmp/taskflow_test_logs_es_deleg";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    ElasticLogSink sink(base_dir, "http://localhost:9200", "test-index");

    REQUIRE(sink.write("wf-es", "task-es", "elastic data\n"));

    // exists 委托
    REQUIRE(sink.exists("wf-es", "task-es"));
    REQUIRE_FALSE(sink.exists("wf-es", "task-nonexistent"));
    REQUIRE_FALSE(sink.exists("wf-nonexistent", "task-es"));

    // getLogPath 委托 —— 与 FileLogSink 路径一致
    FileLogSink file_sink(base_dir);
    REQUIRE(sink.getLogPath("wf-es", "task-es") == file_sink.getLogPath("wf-es", "task-es"));

    // read 委托
    REQUIRE(sink.read("wf-es", "task-es") == "elastic data\n");

    fs::remove_all(base_dir);
}

TEST_CASE("ElasticLogSink: cleanup delegates to file sink", "[log_sink_es_delegation]") {
    // Fix #252: 验证 ElasticLogSink::cleanup 委托给 FileLogSink
    const std::string base_dir = "/tmp/taskflow_test_logs_es_cleanup";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    ElasticLogSink sink(base_dir, "http://localhost:9200", "test-index");

    REQUIRE(sink.write("wf-old-es", "task-1", "old es data\n"));

    // 设为 30 天前
    std::string old_dir = base_dir + "/wf-old-es";
    auto old_time = std::filesystem::file_time_type::clock::now()
                    - std::chrono::hours(24 * 30);
    std::filesystem::last_write_time(old_dir, old_time);

    REQUIRE_NOTHROW(sink.cleanup(7));
    REQUIRE_FALSE(fs::exists(old_dir));

    fs::remove_all(base_dir);
}

TEST_CASE("createLogSink: unknown type falls back to FileLogSink", "[log_sink_factory_fallback]") {
    // Fix #252: 验证未知 sink_type 回退到 FileLogSink
    const std::string base_dir = "/tmp/taskflow_test_logs_unknown";
    fs::remove_all(base_dir);

    auto sink = createLogSink("unknown_type_xyz", base_dir);
    REQUIRE(sink != nullptr);

    // 应表现为 FileLogSink：可写入和读取
    REQUIRE(sink->write("wf-fallback", "task-1", "fallback data\n"));
    REQUIRE(sink->exists("wf-fallback", "task-1"));
    REQUIRE(sink->read("wf-fallback", "task-1") == "fallback data\n");

    fs::remove_all(base_dir);
}

TEST_CASE("createLogSink: empty type falls back to FileLogSink", "[log_sink_factory_fallback]") {
    // Fix #252: 验证空 sink_type 回退到 FileLogSink
    const std::string base_dir = "/tmp/taskflow_test_logs_empty_type";
    fs::remove_all(base_dir);

    auto sink = createLogSink("", base_dir);
    REQUIRE(sink != nullptr);

    REQUIRE(sink->write("wf-empty", "task-1", "empty type data\n"));
    REQUIRE(sink->exists("wf-empty", "task-1"));
    REQUIRE(sink->read("wf-empty", "task-1") == "empty type data\n");

    fs::remove_all(base_dir);
}

// ============================================================================
// Fix #253: TaskExecutor shutdown/registerExecutor/setLogSink 测试
// 验收指标：
//   1. shutdown() 后 submit 被拒绝
//   2. shutdown() 等待运行中的任务完成
//   3. shutdown(timeout) 超时后取消未完成的任务
//   4. registerExecutor 覆盖同类型的内置执行器
//   5. setLogSink 设置后不影响任务正常执行
// ============================================================================

TEST_CASE("TaskExecutor: shutdown rejects new submissions", "[task_executor_shutdown]") {
    // Fix #253: 验证 shutdown() 后 submit 返回 failure
    TaskExecutor executor(10);

    executor.shutdown(5);

    nlohmann::json config;
    config["command"] = "/bin/echo test";
    auto result = executor.submit("shutdown-test-1", "command", config, 10,
                                  TEST_LOG_DIR, "", nullptr);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("shutting down") != std::string::npos);
}

TEST_CASE("TaskExecutor: shutdown waits for running tasks to complete", "[task_executor_shutdown]") {
    // Fix #253: 验证 shutdown() 等待运行中的任务自然完成
    TaskExecutor executor(10);

    nlohmann::json config;
    config["command"] = "/bin/echo finishing_ok";

    std::atomic<bool> callback_called{false};
    TaskResult callback_result;

    auto submit_result = executor.submit("shutdown-wait-1", "command", config, 10,
        TEST_LOG_DIR, "",
        [&](const TaskResult& r) {
            callback_result = r;
            callback_called = true;
        });
    REQUIRE(submit_result.ok());

    // shutdown 应等待任务完成
    executor.shutdown(10);

    REQUIRE(callback_called);
    REQUIRE(callback_result.status == "SUCCESS");
}

TEST_CASE("TaskExecutor: shutdown with short timeout cancels long task", "[task_executor_shutdown]") {
    // Fix #253: 验证 shutdown(timeout) 超时后取消未完成的任务
    TaskExecutor executor(10);

    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\nsleep 60";

    std::atomic<bool> callback_called{false};
    TaskResult callback_result;

    auto submit_result = executor.submit("shutdown-cancel-1", "script", config, 60,
        TEST_LOG_DIR, "",
        [&](const TaskResult& r) {
            callback_result = r;
            callback_called = true;
        });
    REQUIRE(submit_result.ok());

    // shutdown 超时 2 秒，应取消 sleep 60 的任务
    auto start = std::chrono::steady_clock::now();
    executor.shutdown(2);
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();

    // shutdown 应在合理时间内返回（不超过 10 秒，因为超时 2 秒后取消）
    REQUIRE(elapsed < 15);

    // 任务应被取消（回调被调用，状态非 SUCCESS）
    REQUIRE(callback_called);
    REQUIRE(callback_result.status != "SUCCESS");
}

TEST_CASE("TaskExecutor: registerExecutor overrides built-in type", "[task_executor_register]") {
    // Fix #253: 验证 registerExecutor 覆盖同类型的内置执行器
    TaskExecutor executor(10);

    // 注册 "command" 类型的自定义执行器，覆盖内置的 CommandExecutor
    executor.registerExecutor("command", []() -> std::unique_ptr<TaskExecutorBase> {
        return std::make_unique<MockExecutor>();
    });

    nlohmann::json config;
    config["command"] = "/bin/echo should_not_run";

    std::atomic<bool> callback_called{false};
    TaskResult callback_result;

    auto submit_result = executor.submit("override-1", "command", config, 10,
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
    // 自定义执行器返回 mock_executor，证明内置 command 被覆盖
    REQUIRE(callback_result.status == "SUCCESS");
    REQUIRE(callback_result.error_message == "mock_executor");

    executor.shutdown(5);
}

TEST_CASE("TaskExecutor: setLogSink does not break task execution", "[task_executor_logsink]") {
    // Fix #253: 验证 setLogSink 设置日志 sink 后任务仍正常执行
    TaskExecutor executor(10);

    const std::string sink_dir = "/tmp/taskflow_test_logs_sink_exec";
    fs::remove_all(sink_dir);
    fs::create_directories(sink_dir);

    auto sink = std::make_shared<FileLogSink>(sink_dir);
    executor.setLogSink(sink);

    nlohmann::json config;
    config["command"] = "/bin/echo sink_test_ok";

    std::atomic<bool> callback_called{false};
    TaskResult callback_result;

    auto submit_result = executor.submit("sink-test-1", "command", config, 10,
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

    executor.shutdown(5);
    fs::remove_all(sink_dir);
}

TEST_CASE("TaskExecutor: runningCount reflects active tasks", "[task_executor_runningcount]") {
    // Fix #253: 验证 runningCount() 在任务运行时 >0，完成后归 0
    TaskExecutor executor(10);

    REQUIRE(executor.runningCount() == 0);

    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\nsleep 2";

    std::atomic<bool> callback_called{false};
    auto submit_result = executor.submit("running-count-1", "script", config, 10,
        TEST_LOG_DIR, "",
        [&](const TaskResult&) {
            callback_called = true;
        });
    REQUIRE(submit_result.ok());

    // 等待任务开始（runningCount 应 >0）
    bool saw_running = false;
    for (int i = 0; i < 30 && !callback_called; ++i) {
        if (executor.runningCount() > 0) {
            saw_running = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(saw_running);

    // 等待任务完成
    for (int i = 0; i < 50 && !callback_called; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(callback_called);

    // 完成后 runningCount 应归 0（给一点时间让计数器递减）
    for (int i = 0; i < 20 && executor.runningCount() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(executor.runningCount() == 0);

    executor.shutdown(5);
}

// ============================================================================
// Fix #255: ScriptExecutor interpreter 参数测试
// 验收指标：
//   1. 默认 interpreter (bash) 执行 bash 脚本
//   2. interpreter="bash" 显式指定
//   3. interpreter="python3" 执行 Python 脚本
//   4. interpreter="node" 执行 JavaScript 脚本
//   5. interpreter="perl" 执行 Perl 脚本
//   6. interpreter="ruby" 执行 Ruby 脚本
//   7. 不存在的 interpreter 返回 FAILED
// ============================================================================

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: default interpreter is bash", "[script_interpreter]") {
    // Fix #255: 验证不指定 interpreter 时默认使用 bash
    ScriptExecutor executor;
    nlohmann::json config;
    // bash 语法：$(...) 命令替换
    config["script_content"] = "echo 'bash_default_ok'";

    auto result = executor.execute("interp-default", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::ifstream log_file(TEST_LOG_DIR + "/interp-default.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("bash_default_ok") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: explicit bash interpreter", "[script_interpreter]") {
    // Fix #255: 验证显式指定 interpreter="bash"
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "echo 'explicit_bash_ok'";
    config["interpreter"] = "bash";

    auto result = executor.execute("interp-bash", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::ifstream log_file(TEST_LOG_DIR + "/interp-bash.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("explicit_bash_ok") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: python3 interpreter", "[script_interpreter]") {
    // Fix #255: 验证 interpreter="python3" 执行 Python 脚本
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "print('python3_ok')";
    config["interpreter"] = "python3";

    auto result = executor.execute("interp-python3", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::ifstream log_file(TEST_LOG_DIR + "/interp-python3.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("python3_ok") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: python interpreter (substring match)", "[script_interpreter]") {
    // Fix #255: 验证 interpreter="python" 也能匹配（实现用 find("python") != npos）
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "print('python_ok')";
    config["interpreter"] = "python";

    auto result = executor.execute("interp-python", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::ifstream log_file(TEST_LOG_DIR + "/interp-python.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("python_ok") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: node interpreter", "[script_interpreter]") {
    // Fix #255: 验证 interpreter="node" 执行 JavaScript 脚本
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "console.log('node_ok');";
    config["interpreter"] = "node";

    auto result = executor.execute("interp-node", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::ifstream log_file(TEST_LOG_DIR + "/interp-node.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("node_ok") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: perl interpreter", "[script_interpreter]") {
    // Fix #255: 验证 interpreter="perl" 执行 Perl 脚本
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "print 'perl_ok\n';";
    config["interpreter"] = "perl";

    auto result = executor.execute("interp-perl", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::ifstream log_file(TEST_LOG_DIR + "/interp-perl.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("perl_ok") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: ruby interpreter", "[script_interpreter]") {
    // Fix #255: 验证 interpreter="ruby" 执行 Ruby 脚本
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "puts 'ruby_ok'";
    config["interpreter"] = "ruby";

    auto result = executor.execute("interp-ruby", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    std::ifstream log_file(TEST_LOG_DIR + "/interp-ruby.log");
    REQUIRE(log_file.is_open());
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("ruby_ok") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: non-existent interpreter returns FAILED", "[script_interpreter]") {
    // Fix #255: 验证不存在的 interpreter 返回 FAILED（execlp 失败，子进程 _exit(127)）
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "echo 'should_not_run'";
    config["interpreter"] = "nonexistent_interpreter_xyz_123";

    auto result = executor.execute("interp-nonexist", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    // execlp 失败时子进程 _exit(127)
    REQUIRE(result.exit_code == 127);
}

TEST_CASE_METHOD(LogDirFixture, "ScriptExecutor: python3 script with syntax error returns FAILED", "[script_interpreter]") {
    // Fix #255: 验证 Python 脚本语法错误时返回 FAILED 且 exit_code != 0
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "print('missing_close_paren'";
    config["interpreter"] = "python3";

    auto result = executor.execute("interp-python-err", config, 10, TEST_LOG_DIR);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code != 0);
}
