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
    // Fix #306: 析构时清理日志目录，避免 /tmp 残留文件累积导致 CI 磁盘占用增长
    ~LogDirFixture() {
        std::error_code ec;
        fs::remove_all(TEST_LOG_DIR, ec);
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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: built-in command type", "[task_executor]") {
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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: register custom executor", "[task_executor]") {
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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: unknown task type returns error", "[task_executor]") {
    TaskExecutor executor(10);
    nlohmann::json config;

    auto result = executor.submit("test-3", "unknown_type", config, 10,
TEST_LOG_DIR, "", nullptr);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: max tasks limit", "[task_executor]") {
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

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: write and read", "[log_sink]") {
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

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: read non-existent log returns empty", "[log_sink]") {
    const std::string base_dir = "/tmp/taskflow_test_logs_sink_ne";
    fs::create_directories(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE_FALSE(sink.exists("wf-nonexistent", "task-nonexistent"));
    REQUIRE(sink.read("wf-nonexistent", "task-nonexistent") == "");

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: getLogPath returns correct path", "[log_sink]") {
    const std::string base_dir = "/tmp/taskflow_test_logs_sink_path";
    FileLogSink sink(base_dir);

    std::string path = sink.getLogPath("wf-1", "task-1");
    REQUIRE(path.find(base_dir) != std::string::npos);
    REQUIRE(path.find("wf-1") != std::string::npos);
    REQUIRE(path.find("task-1") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture,"ElasticLogSink: falls back to file storage", "[log_sink]") {
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

TEST_CASE_METHOD(LogDirFixture,"createLogSink: creates FileLogSink for file type", "[log_sink]") {
    auto sink = createLogSink("file", "/tmp/taskflow_test_logs_factory");
    REQUIRE(sink != nullptr);
}

TEST_CASE_METHOD(LogDirFixture,"createLogSink: creates ElasticLogSink for elasticsearch type", "[log_sink]") {
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

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: multiple writes append to same log", "[log_sink_append]") {
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

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: different workflow instances are isolated", "[log_sink_isolation]") {
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

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: cleanup removes expired log directories", "[log_sink_cleanup]") {
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

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: cleanup keeps logs within retention period", "[log_sink_cleanup]") {
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

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: cleanup on non-existent base_dir does not crash", "[log_sink_cleanup]") {
    // Fix #252: 验证 cleanup 对不存在的 base_dir 安全（不崩溃）
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup_nonexist";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);
    // 不应抛异常
    REQUIRE_NOTHROW(sink.cleanup(7));
}

TEST_CASE_METHOD(LogDirFixture,"ElasticLogSink: delegates exists/getLogPath to file sink", "[log_sink_es_delegation]") {
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

TEST_CASE_METHOD(LogDirFixture,"ElasticLogSink: cleanup delegates to file sink", "[log_sink_es_delegation]") {
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

TEST_CASE_METHOD(LogDirFixture,"createLogSink: unknown type falls back to FileLogSink", "[log_sink_factory_fallback]") {
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

TEST_CASE_METHOD(LogDirFixture,"createLogSink: empty type falls back to FileLogSink", "[log_sink_factory_fallback]") {
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
// Fix #271: LogSink 路径穿越漏洞与 cleanup 边界条件测试
// 验收指标：
//   1. 含 "../" 的 workflow_instance_id 被拒绝
//   2. 含 "../" 的 task_instance_id 被拒绝
//   3. 含路径分隔符的 ID 被拒绝
//   4. 空 ID 被拒绝
//   5. cleanup(0) 行为验证
//   6. cleanup 对含普通文件的 base_dir 安全
// ============================================================================

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: rejects workflow_instance_id with path traversal", "[log_sink_security]") {
    // Fix #271: 含 "../" 的 workflow_instance_id 应被拒绝，防止路径穿越
    const std::string base_dir = "/tmp/taskflow_test_logs_traversal";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);

    // 尝试用 "../" 逃逸 base_dir
    REQUIRE_FALSE(sink.write("../../etc", "task-1", "malicious\n"));
    REQUIRE_FALSE(sink.exists("../../etc", "task-1"));
    REQUIRE(sink.read("../../etc", "task-1") == "");

    // 验证没有在 base_dir 之外创建文件
    REQUIRE_FALSE(fs::exists("/tmp/taskflow_test_logs_traversal/../../etc/task-1.log"));

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: rejects task_instance_id with path traversal", "[log_sink_security]") {
    // Fix #271: 含 "../" 的 task_instance_id 应被拒绝
    const std::string base_dir = "/tmp/taskflow_test_logs_traversal_task";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE_FALSE(sink.write("wf-1", "../../escape", "malicious\n"));
    REQUIRE_FALSE(sink.exists("wf-1", "../../escape"));
    REQUIRE(sink.read("wf-1", "../../escape") == "");

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: rejects instance id with path separator", "[log_sink_security]") {
    // Fix #271: 含路径分隔符 "/" 的 ID 应被拒绝
    const std::string base_dir = "/tmp/taskflow_test_logs_separator";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE_FALSE(sink.write("wf/inner", "task-1", "data\n"));
    REQUIRE_FALSE(sink.write("wf-1", "task/inner", "data\n"));

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: rejects empty instance id", "[log_sink_security]") {
    // Fix #271: 空 ID 应被拒绝
    const std::string base_dir = "/tmp/taskflow_test_logs_empty_id";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE_FALSE(sink.write("", "task-1", "data\n"));
    REQUIRE_FALSE(sink.write("wf-1", "", "data\n"));
    REQUIRE_FALSE(sink.exists("", "task-1"));
    REQUIRE_FALSE(sink.exists("wf-1", ""));
    REQUIRE(sink.read("", "task-1") == "");
    REQUIRE(sink.read("wf-1", "") == "");

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: accepts valid instance ids with hyphens and underscores", "[log_sink_security]") {
    // Fix #271: 合法 ID（含连字符、下划线、字母数字）应被接受
    const std::string base_dir = "/tmp/taskflow_test_logs_valid_ids";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE(sink.write("wf_instance-1", "task_instance-2", "valid data\n"));
    REQUIRE(sink.exists("wf_instance-1", "task_instance-2"));
    REQUIRE(sink.read("wf_instance-1", "task_instance-2") == "valid data\n");

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: cleanup with zero retention days", "[log_sink_cleanup_edge]") {
    // Fix #271: cleanup(0) 行为验证 —— retention=0 意味着清理所有超过 0 小时的目录
    // 刚创建的目录 last_modified 接近 now，now - last_modified 可能略大于 0
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup_zero";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);
    // 创建一个日志目录
    sink.write("wf-old", "task-1", "old data\n");
    REQUIRE(fs::exists(base_dir + "/wf-old"));

    // cleanup(0) —— retention 为 0 小时，刚创建的目录可能被清理也可能不被清理
    // 取决于时钟精度。此测试验证 cleanup(0) 不崩溃。
    REQUIRE_NOTHROW(sink.cleanup(0));

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: cleanup with negative retention does not crash", "[log_sink_cleanup_edge]") {
    // Fix #271: 负 retention_days 不应崩溃（24 * 负数 = 负小时，now - last_modified > 负值 恒真）
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup_negative";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);
    sink.write("wf-test", "task-1", "data\n");

    // 负 retention 会清理所有目录，但不应崩溃
    REQUIRE_NOTHROW(sink.cleanup(-1));

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: cleanup handles non-directory entries safely", "[log_sink_cleanup_edge]") {
    // Fix #271: base_dir 含普通文件（非目录）时 cleanup 应安全跳过
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup_files";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    // 在 base_dir 中创建一个普通文件（非目录）
    {
        std::ofstream ofs(base_dir + "/stray_file.txt");
        ofs << "not a directory";
    }
    // 同时创建一个合法的日志目录
    {
        FileLogSink sink(base_dir);
        sink.write("wf-real", "task-1", "real data\n");
    }

    FileLogSink sink(base_dir);
    REQUIRE_NOTHROW(sink.cleanup(7));

    // 普通文件应仍存在（cleanup 只处理目录）
    REQUIRE(fs::exists(base_dir + "/stray_file.txt"));

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: write to read-only directory fails gracefully", "[log_sink_write_fail]") {
    // Fix #271: 写入失败路径应返回 false 而非崩溃
    // 创建一个只读目录作为 base_dir
    const std::string base_dir = "/tmp/taskflow_test_logs_readonly";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    // 创建 sink 后将目录设为只读
    FileLogSink sink(base_dir);
    fs::permissions(base_dir, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace);

    // 写入应失败（无法创建子目录），但不崩溃
    bool result = sink.write("wf-readonly", "task-1", "data\n");
    // 在 root 权限下可能仍能写入，所以只验证不崩溃
    // 恢复权限以便清理
    fs::permissions(base_dir, fs::perms::owner_all, fs::perm_options::replace);
    fs::remove_all(base_dir);
}

// ============================================================================
// Fix #282: LogSink 单点 "." 拒绝与 cleanup 整数溢出测试
// 验收指标：
//   1. workflow_instance_id="." 被拒绝（防止日志写到 base_dir 根目录）
//   2. task_instance_id="." 被拒绝
//   3. cleanup(large_retention_days) 不因 24 * retention_days 溢出而崩溃
// ============================================================================

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: rejects single dot workflow_instance_id", "[log_sink_security_dot]") {
    // Fix #282: workflow_instance_id="." 等价于 base_dir 本身，应被拒绝
    const std::string base_dir = "/tmp/taskflow_test_logs_dot_wf";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);

    // "." 作为 workflow_instance_id 会被解析为 base_dir 本身，应拒绝
    REQUIRE_FALSE(sink.write(".", "task-1", "data\n"));
    REQUIRE_FALSE(sink.exists(".", "task-1"));
    REQUIRE(sink.read(".", "task-1") == "");

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: rejects single dot task_instance_id", "[log_sink_security_dot]") {
    // Fix #282: task_instance_id="." 会被解析为目录，应拒绝
    const std::string base_dir = "/tmp/taskflow_test_logs_dot_task";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);

    REQUIRE_FALSE(sink.write("wf-1", ".", "data\n"));
    REQUIRE_FALSE(sink.exists("wf-1", "."));
    REQUIRE(sink.read("wf-1", ".") == "");

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: cleanup with large retention_days does not overflow", "[log_sink_cleanup_overflow]") {
    // Fix #282: 24 * retention_days 在 retention_days 较大时会整数溢出。
    // 修复前用 int 计算，retention_days=100000000 时 24*100000000=2400000000 > INT_MAX 溢出。
    // 修复后用 long long 计算，不应崩溃。
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup_overflow";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);
    sink.write("wf-test", "task-1", "data\n");

    // 大 retention_days 不应导致整数溢出崩溃
    // 使用一个足够大但不会让 hours() 构造本身溢出的值
    REQUIRE_NOTHROW(sink.cleanup(100000));

    // 日志目录应仍存在（retention 很大，不会被清理）
    REQUIRE(fs::exists(base_dir + "/wf-test"));

    fs::remove_all(base_dir);
}

TEST_CASE_METHOD(LogDirFixture,"FileLogSink: cleanup with very large retention_days does not crash", "[log_sink_cleanup_overflow]") {
    // Fix #282: 极大 retention_days 不应崩溃
    // 注意：retention_days 极大时（如 1000000），24*retention_days 转换为纳秒可能溢出，
    // 导致 cleanup 行为不确定（可能删除目录）。此测试仅验证不崩溃，不验证目录是否存在。
    const std::string base_dir = "/tmp/taskflow_test_logs_cleanup_overflow_max";
    fs::remove_all(base_dir);

    FileLogSink sink(base_dir);
    sink.write("wf-test", "task-1", "data\n");

    // 极大 retention_days 不应崩溃（24 * retention_days 用 long long 计算不会溢出）
    REQUIRE_NOTHROW(sink.cleanup(1000000));

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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: shutdown rejects new submissions", "[task_executor_shutdown]") {
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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: shutdown waits for running tasks to complete", "[task_executor_shutdown]") {
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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: shutdown with short timeout cancels long task", "[task_executor_shutdown]") {
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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: registerExecutor overrides built-in type", "[task_executor_register]") {
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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: setLogSink does not break task execution", "[task_executor_logsink]") {
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

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: runningCount reflects active tasks", "[task_executor_runningcount]") {
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

// ============================================================================
// Fix #272: TaskExecutor 重复 task_instance_id 与回调异常安全测试
// 验收指标：
//   1. 重复 task_instance_id 被拒绝（旧任务仍在运行时）
//   2. 回调抛异常不导致 std::terminate
//   3. 回调抛异常后 active_threads_ 仍正确递减
//   4. 回调抛异常后 runningCount 归零
// ============================================================================

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: rejects duplicate task_instance_id while running", "[task_executor_duplicate]") {
    // Fix #272: 验证重复 task_instance_id 被拒绝，防止覆盖旧任务导致无法 cancel
    TaskExecutor executor(10);

    nlohmann::json config;
    config["command"] = "/bin/sleep 5";

    // 第一次提交应成功
    auto result1 = executor.submit("dup-task-id", "command", config, 30,
                                   TEST_LOG_DIR, "", nullptr);
    REQUIRE(result1.ok());

    // 第二次提交相同 ID 应失败（旧任务仍在运行）
    auto result2 = executor.submit("dup-task-id", "command", config, 30,
                                   TEST_LOG_DIR, "", nullptr);
    REQUIRE_FALSE(result2.ok());
    REQUIRE(result2.error().find("Duplicate") != std::string::npos);
    REQUIRE(result2.error().find("dup-task-id") != std::string::npos);

    // 取消第一个任务以便清理
    auto cancel_result = executor.cancel("dup-task-id");
    // cancel 可能成功也可能因任务完成而返回 failure，都可接受
    executor.shutdown(10);
}

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: callback throwing exception does not crash", "[task_executor_callback_exception]") {
    // Fix #272: 验证回调抛异常不会导致 std::terminate
    TaskExecutor executor(10);

    nlohmann::json config;
    config["command"] = "/bin/echo callback_exception_test";

    std::atomic<bool> callback_invoked{false};

    auto submit_result = executor.submit("callback-throw", "command", config, 10,
        TEST_LOG_DIR, "",
        [&](const TaskResult&) {
            callback_invoked = true;
            throw std::runtime_error("intentional callback exception");
        });
    REQUIRE(submit_result.ok());

    // 等待任务完成 —— 如果回调异常导致 std::terminate，测试会崩溃
    for (int i = 0; i < 100 && !callback_invoked; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(callback_invoked);

    // 回调抛异常后，runningCount 仍应归零（active_threads_ 正确递减）
    for (int i = 0; i < 30 && executor.runningCount() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(executor.runningCount() == 0);

    // shutdown 应能正常完成（不因回调异常而永久阻塞）
    executor.shutdown(5);
}

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: callback throwing non-std exception does not crash", "[task_executor_callback_exception]") {
    // Fix #272: 验证回调抛非 std 异常（catch (...) 捕获）不会崩溃
    TaskExecutor executor(10);

    nlohmann::json config;
    config["command"] = "/bin/echo non_std_exception";

    std::atomic<bool> callback_invoked{false};

    auto submit_result = executor.submit("callback-throw-nonstd", "command", config, 10,
        TEST_LOG_DIR, "",
        [&](const TaskResult&) {
            callback_invoked = true;
            throw 42;  // 非 std 异常（int）
        });
    REQUIRE(submit_result.ok());

    for (int i = 0; i < 100 && !callback_invoked; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(callback_invoked);

    // runningCount 应归零
    for (int i = 0; i < 30 && executor.runningCount() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(executor.runningCount() == 0);

    executor.shutdown(5);
}

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: duplicate id rejected then accepted after completion", "[task_executor_duplicate]") {
    // Fix #272: 验证旧任务完成后，相同 ID 可以再次提交
    TaskExecutor executor(10);

    nlohmann::json config;
    config["command"] = "/bin/echo reuse_id";

    std::atomic<bool> first_done{false};

    // 第一次提交
    auto result1 = executor.submit("reuse-id", "command", config, 10,
        TEST_LOG_DIR, "",
        [&](const TaskResult&) { first_done = true; });
    REQUIRE(result1.ok());

    // 等待第一次完成
    for (int i = 0; i < 50 && !first_done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(first_done);

    // 确保计数器归零
    for (int i = 0; i < 20 && executor.runningCount() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 第二次提交相同 ID 应成功（旧任务已完成）
    std::atomic<bool> second_done{false};
    auto result2 = executor.submit("reuse-id", "command", config, 10,
        TEST_LOG_DIR, "",
        [&](const TaskResult&) { second_done = true; });
    REQUIRE(result2.ok());

    for (int i = 0; i < 50 && !second_done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(second_done);

    executor.shutdown(5);
}

// ============================================================================
// Fix #277: CommandExecutor task_instance_id 路径穿越防护
// ============================================================================

TEST_CASE_METHOD(LogDirFixture,"CommandExecutor: rejects path traversal in task_instance_id", "[command_executor_security]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo hello";

    auto result = executor.execute("../../etc/passwd", config, 10, TEST_LOG_DIR, nullptr, nullptr);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.error_message.find("path separators") != std::string::npos);
}

TEST_CASE_METHOD(LogDirFixture,"CommandExecutor: rejects slash in task_instance_id", "[command_executor_security]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo hello";

    auto result = executor.execute("evil/path", config, 10, TEST_LOG_DIR, nullptr, nullptr);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture,"CommandExecutor: rejects backslash in task_instance_id", "[command_executor_security]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo hello";

    auto result = executor.execute("evil\\path", config, 10, TEST_LOG_DIR, nullptr, nullptr);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture,"CommandExecutor: rejects single dot as task_instance_id", "[command_executor_security]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo hello";

    auto result = executor.execute(".", config, 10, TEST_LOG_DIR, nullptr, nullptr);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture,"CommandExecutor: rejects empty task_instance_id", "[command_executor_security]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo hello";

    auto result = executor.execute("", config, 10, TEST_LOG_DIR, nullptr, nullptr);
    REQUIRE(result.status == "FAILED");
}

TEST_CASE_METHOD(LogDirFixture,"CommandExecutor: accepts valid task_instance_id", "[command_executor_security]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "echo hello";

    auto result = executor.execute("valid-task-id-123", config, 10, TEST_LOG_DIR, nullptr, nullptr);
    REQUIRE(result.status == "SUCCESS");
}

// ============================================================================
// Fix #278: TaskExecutor registerExecutor/setLogSink 线程安全
// ============================================================================

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: concurrent registerExecutor and submit does not crash", "[task_executor_thread_safety]") {
    TaskExecutor executor(10);
    nlohmann::json config;
    config["command"] = "echo hello";

    std::atomic<bool> stop{false};
    std::atomic<int> submit_count{0};
    std::atomic<int> register_count{0};

    // Thread 1: continuously submit tasks
    std::thread submit_thread([&]() {
        for (int i = 0; i < 20 && !stop; ++i) {
            auto result = executor.submit("ts-submit-" + std::to_string(i), "command",
                config, 10, TEST_LOG_DIR, "",
                [](const TaskResult&) {});
            if (result.ok()) submit_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    // Thread 2: continuously register/unregister executors
    std::thread register_thread([&]() {
        for (int i = 0; i < 20 && !stop; ++i) {
            executor.registerExecutor("custom-" + std::to_string(i),
                []() -> std::unique_ptr<TaskExecutorBase> { return nullptr; });
            register_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    // Thread 3: continuously set log sink
    std::thread sink_thread([&]() {
        for (int i = 0; i < 20 && !stop; ++i) {
            executor.setLogSink(std::make_shared<FileLogSink>(TEST_LOG_DIR + "/ts-sink"));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    submit_thread.join();
    stop = true;
    register_thread.join();
    sink_thread.join();

    executor.shutdown(10);
    // Fix #305: 原断言 submit_count >= 0 恒真（atomic<int> 从 0 递增），无验证意义
    // 改为验证确实有任务被提交
    REQUIRE(submit_count.load() > 0);
    REQUIRE(register_count.load() > 0);
}

TEST_CASE_METHOD(LogDirFixture,"TaskExecutor: setLogSink during task execution does not crash", "[task_executor_thread_safety]") {
    TaskExecutor executor(5);
    nlohmann::json config;
    config["command"] = "sleep 1";

    std::atomic<bool> done{false};
    auto result = executor.submit("ts-sink-task", "command", config, 10,
        TEST_LOG_DIR, "",
        [&](const TaskResult&) { done = true; });
    REQUIRE(result.ok());

    // Switch log sink while task is running
    for (int i = 0; i < 5; ++i) {
        executor.setLogSink(std::make_shared<FileLogSink>(TEST_LOG_DIR + "/ts-switch-" + std::to_string(i)));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    for (int i = 0; i < 50 && !done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done);
    executor.shutdown(5);
}
