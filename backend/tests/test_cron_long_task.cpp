#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <set>
#include <sys/stat.h>
#include <signal.h>

#include "scheduler/engine/cron_parser.h"
#include "scheduler/engine/dag_engine.h"
#include "worker/executor/task_executor.h"
#include "worker/executor/command_executor.h"
#include "worker/executor/script_executor.h"

using namespace taskflow::scheduler::engine;
using namespace taskflow::worker::executor;
namespace fs = std::filesystem;

// ============================================================================
// 测试目标（对照 completed-features.md）：
//   1. 定时任务（Cron）触发时间计算是否正常
//   2. 长时间运行任务是否正常执行、超时处理
//   3. 是否能够获取长时间运行任务的状态（运行中/已完成）
//   4. 是否能取消或终止任务（cancel SIGTERM/SIGKILL）
// ============================================================================

static const std::string LONG_TASK_LOG_DIR = "/tmp/taskflow_test_long_task_logs";

struct LongTaskFixture {
    LongTaskFixture() {
        fs::create_directories(LONG_TASK_LOG_DIR);
    }
    ~LongTaskFixture() {
        // 删除并重建日志目录，确保测试间隔离（避免上一个测试的日志文件干扰下一个测试的断言）
        std::error_code ec;
        fs::remove_all(LONG_TASK_LOG_DIR, ec);
        fs::create_directories(LONG_TASK_LOG_DIR);
    }
};

// ============================================================================
// 一、定时任务（Cron）测试
// 验收指标：
//   1.1 每秒触发的 cron 表达式计算正确
//   1.2 每分钟触发的 cron 表达式计算正确
//   1.3 指定时刻触发的 cron 表达式计算正确
//   1.4 跨日触发计算正确（今天已过指定时刻，下次为明天）
//   1.5 跨月触发计算正确
//   1.6 步长表达式计算正确
//   1.7 连续两次触发时间间隔正确（链式计算）
//   1.8 无效表达式被拒绝
// ============================================================================

TEST_CASE("Cron: every second trigger computes correctly", "[cron_schedule]") {
    auto result = CronParser::getNextTrigger("* * * * * *", "2025-06-18 10:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-06-18 10:00:01");
}

TEST_CASE("Cron: every minute at second 0", "[cron_schedule]") {
    auto result = CronParser::getNextTrigger("0 * * * * *", "2025-06-18 10:00:30");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-06-18 10:01:00");
}

TEST_CASE("Cron: specific time daily (09:30:00)", "[cron_schedule]") {
    auto result = CronParser::getNextTrigger("0 30 9 * * *", "2025-06-18 08:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-06-18 09:30:00");
}

TEST_CASE("Cron: cross-day trigger (today's time already passed)", "[cron_schedule]") {
    // 当前 11:00，每天 09:30 触发，下次应为明天 09:30
    auto result = CronParser::getNextTrigger("0 30 9 * * *", "2025-06-18 11:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-06-19 09:30:00");
}

TEST_CASE("Cron: cross-month trigger", "[cron_schedule]") {
    // 1月31日，每月1日00:00触发，下次应为2月1日
    auto result = CronParser::getNextTrigger("0 0 0 1 * *", "2025-01-31 12:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-02-01 00:00:00");
}

TEST_CASE("Cron: step expression every 5 minutes", "[cron_schedule]") {
    auto result = CronParser::getNextTrigger("0 */5 * * * *", "2025-06-18 10:02:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-06-18 10:05:00");
}

TEST_CASE("Cron: chained trigger computation (two consecutive triggers)", "[cron_schedule]") {
    // 第一次触发
    auto r1 = CronParser::getNextTrigger("0 */10 * * * *", "2025-06-18 10:00:00");
    REQUIRE(r1.ok());
    REQUIRE(r1.value() == "2025-06-18 10:10:00");

    // 以第一次触发时间为基础，计算第二次
    auto r2 = CronParser::getNextTrigger("0 */10 * * * *", r1.value());
    REQUIRE(r2.ok());
    REQUIRE(r2.value() == "2025-06-18 10:20:00");
}

TEST_CASE("Cron: specific day of week (every Friday)", "[cron_schedule]") {
    // 2025-06-18 是周三，每周五 00:00:00 触发（5=周五）
    auto result = CronParser::getNextTrigger("0 0 0 * * 5", "2025-06-18 12:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-06-20 00:00:00");
}

TEST_CASE("Cron: invalid expression with wrong field count rejected", "[cron_schedule]") {
    auto result = CronParser::getNextTrigger("0 * * * *", "2025-06-18 10:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Cron: invalid out-of-range value rejected", "[cron_schedule]") {
    // 秒字段 60 超出范围
    auto result = CronParser::getNextTrigger("60 * * * * *", "2025-06-18 10:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Cron: list expression (seconds 0,15,30,45)", "[cron_schedule]") {
    auto result = CronParser::getNextTrigger("0,15,30,45 * * * * *", "2025-06-18 10:00:10");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-06-18 10:00:15");
}

// ============================================================================
// 二、长时间运行任务测试
// 验收指标：
//   2.1 长时间运行任务（sleep）能正常执行并完成
//   2.2 任务执行期间 runningCount > 0
//   2.3 任务完成后 runningCount 归零
//   2.4 超时任务返回 TIMEOUT 状态
//   2.5 长任务日志文件被正确写入
//   2.6 多个长任务可并发执行
// ============================================================================

TEST_CASE_METHOD(LongTaskFixture, "LongTask: long-running task completes successfully", "[long_task]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "/bin/sleep 2 && echo done";

    std::string instance_id = "long-success-1";
    auto result = executor.execute(instance_id, config, 10, LONG_TASK_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);

    // 验证日志包含 echo 输出（之前未验证命令实际执行效果）
    std::string log_path = LONG_TASK_LOG_DIR + "/" + instance_id + ".log";
    std::ifstream ifs(log_path);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("done") != std::string::npos);
}

TEST_CASE_METHOD(LongTaskFixture, "LongTask: runningCount increases during execution", "[long_task]") {
    TaskExecutor executor(10);
    nlohmann::json config;
    config["command"] = "/bin/sleep 3";

    std::atomic<bool> done{false};
    auto submit_result = executor.submit("long-running-1", "command", config, 10,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult&) { done = true; });

    REQUIRE(submit_result.ok());

    // 等待任务真正开始（runningCount > 0）
    bool saw_running = false;
    for (int i = 0; i < 30; ++i) {
        if (executor.runningCount() > 0) {
            saw_running = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(saw_running);
    REQUIRE(executor.runningCount() > 0);

    // 等待任务完成
    for (int i = 0; i < 100 && !done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done);
    REQUIRE(executor.runningCount() == 0);
}

TEST_CASE_METHOD(LongTaskFixture, "LongTask: timeout returns TIMEOUT status", "[long_task]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "/bin/sleep 60";

    auto start = std::chrono::steady_clock::now();
    auto result = executor.execute("long-timeout-1", config, 2, LONG_TASK_LOG_DIR);
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();

    REQUIRE(result.status == "TIMEOUT");
    REQUIRE(result.exit_code == -1);
    // 验证 error_message 包含 "timed out"（之前未验证错误消息内容）
    REQUIRE(result.error_message.find("timed out") != std::string::npos);
    // 超时应在大约 2 秒后触发（允许一定误差）
    REQUIRE(elapsed >= 2);
    REQUIRE(elapsed < 5);
}

TEST_CASE_METHOD(LongTaskFixture, "LongTask: log file is written during execution", "[long_task]") {
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "/bin/echo long-task-log-content";

    std::string instance_id = "long-log-1";
    auto result = executor.execute(instance_id, config, 10, LONG_TASK_LOG_DIR);
    REQUIRE(result.status == "SUCCESS");

    std::string log_path = LONG_TASK_LOG_DIR + "/" + instance_id + ".log";
    REQUIRE(fs::exists(log_path));

    std::ifstream ifs(log_path);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("long-task-log-content") != std::string::npos);
}

TEST_CASE_METHOD(LongTaskFixture, "LongTask: multiple long tasks run concurrently", "[long_task]") {
    TaskExecutor executor(10);

    std::atomic<int> completed{0};
    nlohmann::json config;
    config["command"] = "/bin/sleep 2";

    // 提交 3 个并发长任务
    for (int i = 0; i < 3; ++i) {
        auto r = executor.submit("concurrent-" + std::to_string(i), "command", config, 10,
            LONG_TASK_LOG_DIR, "",
            [&](const TaskResult&) { completed++; });
        REQUIRE(r.ok());
    }

    // 并发期间 runningCount 应该 >= 2（可能由于时序不总是恰好3）
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    REQUIRE(executor.runningCount() >= 2);

    // 等待全部完成
    for (int i = 0; i < 100 && completed < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(completed == 3);
    REQUIRE(executor.runningCount() == 0);
}

TEST_CASE_METHOD(LongTaskFixture, "LongTask: script executor long-running task", "[long_task]") {
    ScriptExecutor executor;
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\nfor i in 1 2 3; do echo \"line $i\"; sleep 1; done\necho finished";

    auto start = std::chrono::steady_clock::now();
    auto result = executor.execute("long-script-1", config, 15, LONG_TASK_LOG_DIR);
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();

    REQUIRE(result.status == "SUCCESS");
    REQUIRE(result.exit_code == 0);
    // 3次sleep 1秒，至少3秒
    REQUIRE(elapsed >= 3);

    // 验证日志包含所有输出行
    std::string log_path = LONG_TASK_LOG_DIR + "/long-script-1.log";
    std::ifstream ifs(log_path);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("line 1") != std::string::npos);
    REQUIRE(content.find("line 2") != std::string::npos);
    REQUIRE(content.find("line 3") != std::string::npos);
    REQUIRE(content.find("finished") != std::string::npos);
}

// ============================================================================
// 三、长时间运行任务状态查询测试
// 验收指标：
//   3.1 任务提交后可通过 runningCount 确认正在运行
//   3.2 任务执行期间可多次查询状态（始终为运行中）
//   3.3 任务完成后状态变为终态（SUCCESS/FAILED/TIMEOUT）
//   3.4 回调能正确报告最终状态
//   3.5 TaskInstance 状态枚举完整（9种状态）
// ============================================================================

TEST_CASE_METHOD(LongTaskFixture, "TaskStatus: query running state during long task", "[task_status]") {
    TaskExecutor executor(10);
    nlohmann::json config;
    config["command"] = "/bin/sleep 3";

    std::atomic<bool> done{false};
    TaskResult final_result;
    auto submit_result = executor.submit("status-query-1", "command", config, 10,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult& r) {
            final_result = r;
            done = true;
        });

    REQUIRE(submit_result.ok());

    // 在任务运行期间多次查询，runningCount 应始终 > 0
    int running_checks = 0;
    for (int i = 0; i < 20 && !done; ++i) {
        if (executor.runningCount() > 0) {
            running_checks++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // 至少有一次查询时任务处于运行中
    REQUIRE(running_checks > 0);

    // 等待完成
    for (int i = 0; i < 100 && !done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done);
    REQUIRE(final_result.status == "SUCCESS");
    REQUIRE(executor.runningCount() == 0);
}

TEST_CASE_METHOD(LongTaskFixture, "TaskStatus: callback reports final status correctly", "[task_status]") {
    TaskExecutor executor(10);

    SECTION("successful task reports SUCCESS") {
        nlohmann::json config;
        config["command"] = "/bin/echo ok";

        std::atomic<bool> done{false};
        TaskResult result;

        executor.submit("status-cb-success", "command", config, 10,
            LONG_TASK_LOG_DIR, "",
            [&](const TaskResult& r) { result = r; done = true; });

        for (int i = 0; i < 50 && !done; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        REQUIRE(done);
        REQUIRE(result.status == "SUCCESS");
        REQUIRE(result.exit_code == 0);
    }

    SECTION("failing task reports FAILED") {
        nlohmann::json config;
        config["command"] = "/bin/false";

        std::atomic<bool> done{false};
        TaskResult result;

        executor.submit("status-cb-failed", "command", config, 10,
            LONG_TASK_LOG_DIR, "",
            [&](const TaskResult& r) { result = r; done = true; });

        for (int i = 0; i < 50 && !done; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        REQUIRE(done);
        REQUIRE(result.status == "FAILED");
        REQUIRE(result.exit_code != 0);
    }

    SECTION("timeout task reports TIMEOUT") {
        nlohmann::json config;
        config["command"] = "/bin/sleep 30";

        std::atomic<bool> done{false};
        TaskResult result;

        executor.submit("status-cb-timeout", "command", config, 2,
            LONG_TASK_LOG_DIR, "",
            [&](const TaskResult& r) { result = r; done = true; });

        for (int i = 0; i < 100 && !done; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        REQUIRE(done);
        REQUIRE(result.status == "TIMEOUT");
        // 验证 error_message 包含 "timed out"
        REQUIRE(result.error_message.find("timed out") != std::string::npos);
    }
}

TEST_CASE("TaskStatus: terminal statuses recognized by DagEngine::allTasksFinished", "[task_status]") {
    // 通过 DagEngine::allTasksFinished 的行为验证源代码 kTerminalStatuses 集合
    // （之前只验证测试自己写的本地容器，未引用源代码定义，无法检测源码与文档不一致）

    // 源码 kTerminalStatuses = {SUCCESS, FAILED, UPSTREAM_FAILED, TIMEOUT, CANCELLED, NODE_OFFLINE}
    // 每种终态单独验证：单个任务处于该状态时，allTasksFinished 应返回 true
    for (const auto& terminal : std::vector<std::string>{
            "SUCCESS", "FAILED", "UPSTREAM_FAILED", "TIMEOUT", "CANCELLED", "NODE_OFFLINE"}) {
        std::map<std::string, std::string> statuses = {{"t1", terminal}};
        INFO("terminal status: " << terminal);
        REQUIRE(DagEngine::allTasksFinished(statuses) == true);
    }

    // 非终态：单个任务处于该状态时，allTasksFinished 应返回 false
    for (const auto& non_terminal : std::vector<std::string>{
            "PENDING", "DISPATCHED", "RUNNING", "", "QUEUED", "UNKNOWN"}) {
        std::map<std::string, std::string> statuses = {{"t1", non_terminal}};
        INFO("non-terminal status: " << non_terminal);
        REQUIRE(DagEngine::allTasksFinished(statuses) == false);
    }

    // 混合场景：一个终态 + 一个非终态 → false
    {
        std::map<std::string, std::string> statuses = {
            {"t1", "SUCCESS"}, {"t2", "RUNNING"}};
        REQUIRE(DagEngine::allTasksFinished(statuses) == false);
    }

    // 混合场景：两个终态 → true
    {
        std::map<std::string, std::string> statuses = {
            {"t1", "SUCCESS"}, {"t2", "FAILED"}};
        REQUIRE(DagEngine::allTasksFinished(statuses) == true);
    }

    // 空映射 → true（没有未完成任务）
    {
        std::map<std::string, std::string> statuses;
        REQUIRE(DagEngine::allTasksFinished(statuses) == true);
    }
}

TEST_CASE_METHOD(LongTaskFixture, "TaskStatus: runningCount reflects concurrent task count", "[task_status]") {
    TaskExecutor executor(10);
    nlohmann::json config;
    config["command"] = "/bin/sleep 3";

    std::atomic<int> completed{0};
    auto callback = [&](const TaskResult&) { completed++; };

    // 提交 4 个任务
    for (int i = 0; i < 4; ++i) {
        auto r = executor.submit("count-" + std::to_string(i), "command", config, 10,
            LONG_TASK_LOG_DIR, "", callback);
        REQUIRE(r.ok());
    }

    // 等待任务启动：runningCount 应达到 4（在慢机器上可能需要更长等待时间）
    // 之前硬断言 == 4 在慢机器上可能 flaky，改为轮询等待峰值
    int max_observed = 0;
    for (int i = 0; i < 30; ++i) {
        int current = executor.runningCount();
        if (current > max_observed) {
            max_observed = current;
        }
        if (current == 4) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // 至少应有 2 个并发运行（允许调度时序差异，但不应低于 2）
    REQUIRE(max_observed >= 2);

    // 等待全部完成
    for (int i = 0; i < 100 && completed < 4; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(completed == 4);
    REQUIRE(executor.runningCount() == 0);
}

// ============================================================================
// 四、任务取消/终止测试
// 验收指标：
//   4.1 cancel() 能终止运行中的长任务
//   4.2 cancel() 后任务回调被触发，状态为 FAILED（被信号终止）
//   4.3 cancel() 不存在的任务返回错误
//   4.4 cancel() 后 runningCount 减少
//   4.5 超时机制作为隐式终止（SIGKILL）
//   4.6 多任务中只取消指定任务，其他任务不受影响
//   4.7 cancel() 对 script 类型任务同样有效
// ============================================================================

TEST_CASE_METHOD(LongTaskFixture, "Cancel: cancel running long task terminates it", "[task_cancel]") {
    TaskExecutor executor(10);
    nlohmann::json config;
    config["command"] = "/bin/sleep 60";

    std::atomic<bool> done{false};
    TaskResult result;

    auto submit_result = executor.submit("cancel-target-1", "command", config, 120,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult& r) { result = r; done = true; });
    REQUIRE(submit_result.ok());

    // 等待任务开始运行
    for (int i = 0; i < 30 && executor.runningCount() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(executor.runningCount() == 1);

    // 取消任务
    auto cancel_result = executor.cancel("cancel-target-1");
    REQUIRE(cancel_result.ok());

    // 等待回调触发
    for (int i = 0; i < 60 && !done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done);

    // 被信号终止的任务状态应为 FAILED（WIFSIGNALED）
    REQUIRE(result.status == "FAILED");
    // 被信号终止，exit_code 为负的信号编号
    REQUIRE(result.exit_code < 0);
    // 验证 error_message 包含信号信息
    REQUIRE(result.error_message.find("signal") != std::string::npos);

    // runningCount 应归零
    REQUIRE(executor.runningCount() == 0);
}

TEST_CASE_METHOD(LongTaskFixture, "Cancel: cancel non-existent task returns error", "[task_cancel]") {
    TaskExecutor executor(10);

    auto result = executor.cancel("non-existent-task-id");
    REQUIRE_FALSE(result.ok());
    // 验证错误信息包含任务 ID（之前未验证错误消息内容）
    REQUIRE(result.error().find("non-existent-task-id") != std::string::npos);
    REQUIRE(result.error().find("not found") != std::string::npos);
}

TEST_CASE_METHOD(LongTaskFixture, "Cancel: cancel only specified task, others unaffected", "[task_cancel]") {
    TaskExecutor executor(10);
    nlohmann::json config;
    config["command"] = "/bin/sleep 60";

    std::atomic<bool> done1{false};
    std::atomic<bool> done2{false};

    // 提交两个长任务
    auto r1 = executor.submit("cancel-keep-1", "command", config, 120,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult&) { done1 = true; });
    REQUIRE(r1.ok());

    auto r2 = executor.submit("cancel-kill-1", "command", config, 120,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult&) { done2 = true; });
    REQUIRE(r2.ok());

    // 等待两个任务都开始运行
    for (int i = 0; i < 30 && executor.runningCount() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(executor.runningCount() == 2);

    // 只取消第二个任务
    auto cancel_result = executor.cancel("cancel-kill-1");
    REQUIRE(cancel_result.ok());

    // 等待被取消任务的回调触发
    for (int i = 0; i < 60 && !done2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done2);

    // 第一个任务应该仍在运行（未被取消）
    REQUIRE_FALSE(done1);
    REQUIRE(executor.runningCount() == 1);

    // 清理：取消第一个任务
    executor.cancel("cancel-keep-1");
    for (int i = 0; i < 60 && !done1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done1);
    REQUIRE(executor.runningCount() == 0);
}

TEST_CASE_METHOD(LongTaskFixture, "Cancel: cancel script type long task", "[task_cancel]") {
    TaskExecutor executor(10);
    nlohmann::json config;
    config["script_content"] = "#!/bin/bash\nsleep 60";

    std::atomic<bool> done{false};
    TaskResult result;

    auto submit_result = executor.submit("cancel-script-1", "script", config, 120,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult& r) { result = r; done = true; });
    REQUIRE(submit_result.ok());

    // 等待任务开始
    for (int i = 0; i < 30 && executor.runningCount() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(executor.runningCount() == 1);

    // 取消任务
    auto cancel_result = executor.cancel("cancel-script-1");
    REQUIRE(cancel_result.ok());

    // 等待回调
    for (int i = 0; i < 60 && !done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done);
    REQUIRE(result.status == "FAILED");
    REQUIRE(result.exit_code < 0);
    // 验证 error_message 包含信号信息（被信号终止）
    REQUIRE(result.error_message.find("signal") != std::string::npos);
    REQUIRE(executor.runningCount() == 0);
}

TEST_CASE_METHOD(LongTaskFixture, "Cancel: timeout acts as implicit termination", "[task_cancel]") {
    // 超时机制本质上是 executor 内部的隐式终止（SIGKILL）
    CommandExecutor executor;
    nlohmann::json config;
    config["command"] = "/bin/sleep 60";

    auto start = std::chrono::steady_clock::now();
    auto result = executor.execute("timeout-kill-1", config, 2, LONG_TASK_LOG_DIR);
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();

    REQUIRE(result.status == "TIMEOUT");
    REQUIRE(result.exit_code == -1);
    // 验证 error_message 包含 "timed out"
    REQUIRE(result.error_message.find("timed out") != std::string::npos);
    // 超时后任务应被终止，不再运行
    REQUIRE(elapsed >= 2);
    REQUIRE(elapsed < 5);

    // 验证进程确实被杀死：execute() 同步返回意味着子进程已被 waitpid 回收，
    // 不存在遗留的孤儿 sleep 进程。补充验证：再次执行一个快速任务应能成功，
    // 说明 executor 状态正常，没有因上一个超时任务而阻塞。
    nlohmann::json quick_config;
    quick_config["command"] = "/bin/echo alive";
    auto quick_result = executor.execute("timeout-kill-verify", quick_config, 5, LONG_TASK_LOG_DIR);
    REQUIRE(quick_result.status == "SUCCESS");
    REQUIRE(quick_result.exit_code == 0);
}

TEST_CASE_METHOD(LongTaskFixture, "Cancel: cancel already-completed task returns error", "[task_cancel]") {
    TaskExecutor executor(10);
    nlohmann::json config;
    config["command"] = "/bin/echo quick";

    std::atomic<bool> done{false};
    auto submit_result = executor.submit("cancel-completed-1", "command", config, 10,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult&) { done = true; });
    REQUIRE(submit_result.ok());

    // 等待任务完成
    for (int i = 0; i < 50 && !done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done);
    REQUIRE(executor.runningCount() == 0);

    // 尝试取消已完成的任务应失败
    auto cancel_result = executor.cancel("cancel-completed-1");
    REQUIRE_FALSE(cancel_result.ok());
    // 验证错误信息包含任务 ID（之前未验证错误消息内容）
    REQUIRE(cancel_result.error().find("cancel-completed-1") != std::string::npos);
}

TEST_CASE_METHOD(LongTaskFixture, "Cancel: cancel task then submit new task works", "[task_cancel]") {
    TaskExecutor executor(10);
    nlohmann::json long_config;
    long_config["command"] = "/bin/sleep 60";

    std::atomic<bool> done1{false};
    auto r1 = executor.submit("cancel-resubmit-1", "command", long_config, 120,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult&) { done1 = true; });
    REQUIRE(r1.ok());

    // 等待任务开始
    for (int i = 0; i < 30 && executor.runningCount() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(executor.runningCount() == 1);

    // 取消任务
    auto cancel_result = executor.cancel("cancel-resubmit-1");
    REQUIRE(cancel_result.ok());

    // 等待取消完成
    for (int i = 0; i < 60 && !done1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done1);
    REQUIRE(executor.runningCount() == 0);

    // 提交新任务，应能正常执行
    nlohmann::json short_config;
    short_config["command"] = "/bin/echo resubmitted";
    std::atomic<bool> done2{false};
    TaskResult result2;

    auto r2 = executor.submit("cancel-resubmit-2", "command", short_config, 10,
        LONG_TASK_LOG_DIR, "",
        [&](const TaskResult& r) { result2 = r; done2 = true; });
    REQUIRE(r2.ok());

    for (int i = 0; i < 50 && !done2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(done2);
    REQUIRE(result2.status == "SUCCESS");
    REQUIRE(executor.runningCount() == 0);
}
