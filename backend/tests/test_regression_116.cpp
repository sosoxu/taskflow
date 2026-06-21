// 回归测试：验证 #116 TaskExecutor 并发竞态修复
// 高并发提交任务，验证 running_count 不超过 max_tasks
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#include "worker/executor/task_executor.h"

using namespace taskflow::worker::executor;

static const std::string REGRESSION_LOG_DIR = "/tmp/taskflow_regression_logs";

// Fix #306: 使用 fixture 在析构时清理日志目录，避免 /tmp 残留文件累积
struct RegressionLogDirFixture {
    RegressionLogDirFixture() {
        std::filesystem::create_directories(REGRESSION_LOG_DIR);
    }
    ~RegressionLogDirFixture() {
        std::error_code ec;
        std::filesystem::remove_all(REGRESSION_LOG_DIR, ec);
    }
};

// Fix #264: 修正回归检测机制 —— 在 submit 成功瞬间采样 runningCount，
// 而非在回调中采样（回调执行时 running_count_ 已被递减，永远比真实峰值少 1）。
// 同时修正变量声明顺序避免 use-after-free（原子变量先于 executor 声明，
// 使 executor 先析构、先等待线程结束，保护回调中对原子变量的引用）。
TEST_CASE_METHOD(RegressionLogDirFixture, "Regression #116: concurrent submit does not exceed max_tasks", "[regression][executor]") {
    // Fix #306: 日志目录由 fixture 管理（创建+清理）

    // Fix #264: 原子变量先声明，确保 executor 析构时回调仍可安全访问
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    std::atomic<int> max_concurrent_observed{0};
    std::atomic<int> callback_count{0};

    // max_tasks=5, 用 sleep 任务让任务持续运行
    TaskExecutor executor(5);
    nlohmann::json config;
    config["command"] = "/bin/sleep 3";

    const int THREADS = 20;
    const int TASKS_PER_THREAD = 10;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < TASKS_PER_THREAD; ++i) {
                std::string id = "reg116-" + std::to_string(t) + "-" + std::to_string(i);
                // Fix #264: 在 submit 成功瞬间采样 runningCount（此时 running_count_ 已递增但未递减）
                auto r = executor.submit(id, "command", config, 30,
                    REGRESSION_LOG_DIR, "",
                    [&](const TaskResult&) {
                        callback_count.fetch_add(1);
                    });
                if (r.ok()) {
                    accepted.fetch_add(1);
                    // submit 成功后 running_count_ 已递增，采样峰值
                    int current = executor.runningCount();
                    int prev = max_concurrent_observed.load();
                    while (current > prev && !max_concurrent_observed.compare_exchange_weak(prev, current)) {}
                } else {
                    rejected.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    // Fix #264: 等待所有任务完成（带超时，避免测试挂死）
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (executor.runningCount() > 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL("Timeout waiting for tasks to complete");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Fix #264: 等待所有回调执行完毕（runningCount 归零后回调可能仍在执行）
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (callback_count.load() < accepted.load()) {
        if (std::chrono::steady_clock::now() > deadline) {
            break;  // 不阻塞测试，但记录差异
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    INFO("accepted=" << accepted.load() << " rejected=" << rejected.load()
         << " max_concurrent_observed=" << max_concurrent_observed.load()
         << " callback_count=" << callback_count.load() << " (max_tasks=5)");

    // Fix #264: 修正注释 —— max_concurrent_observed 是并发运行数峰值，不是累计 accepted
    // CAS 修复确保并发运行数不超过 max_tasks
    REQUIRE(max_concurrent_observed.load() <= 5);
    REQUIRE(accepted.load() > 0);
    // Fix #264: 验证任务总数守恒（accepted + rejected == 总提交数）
    REQUIRE(accepted.load() + rejected.load() == THREADS * TASKS_PER_THREAD);
}

// Fix #264: 第二个用例补充串行化断言、rejected 计数、任务总数守恒
TEST_CASE_METHOD(RegressionLogDirFixture, "Regression #116: max_tasks=1 strict serialization", "[regression][executor]") {
    // Fix #306: 日志目录由 fixture 管理（创建+清理）

    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    std::atomic<int> max_concurrent_observed{0};
    std::atomic<int> callback_count{0};

    TaskExecutor executor(1);
    nlohmann::json config;
    config["command"] = "/bin/sleep 1";  // Fix #264: 使用整数秒避免可移植性问题

    const int N = 10;

    // 并发提交
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&, i]() {
            std::string id = "reg116b-" + std::to_string(i);
            auto r = executor.submit(id, "command", config, 10,
                REGRESSION_LOG_DIR, "",
                [&](const TaskResult&) {
                    callback_count.fetch_add(1);
                });
            if (r.ok()) {
                accepted.fetch_add(1);
                int current = executor.runningCount();
                int prev = max_concurrent_observed.load();
                while (current > prev && !max_concurrent_observed.compare_exchange_weak(prev, current)) {}
            } else {
                rejected.fetch_add(1);
            }
        });
    }
    for (auto& th : threads) th.join();

    // Fix #264: 带超时的等待
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (executor.runningCount() > 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL("Timeout waiting for tasks to complete");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (callback_count.load() < accepted.load()) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    INFO("max_tasks=1: accepted=" << accepted.load() << " rejected=" << rejected.load()
         << " max_concurrent_observed=" << max_concurrent_observed.load());

    // Fix #264: 验证串行化语义 —— 任意时刻最多 1 个任务并发运行
    REQUIRE(max_concurrent_observed.load() <= 1);
    REQUIRE(accepted.load() >= 1);
    // Fix #264: 验证任务总数守恒
    REQUIRE(accepted.load() + rejected.load() == N);
}

// Fix #264: 补充 max_tasks=0 边界测试 —— 所有 submit 应被拒绝
TEST_CASE_METHOD(RegressionLogDirFixture, "Regression #116: max_tasks=0 rejects all submissions", "[regression][executor]") {
    // Fix #306: 日志目录由 fixture 管理（创建+清理）

    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};

    TaskExecutor executor(0);  // max_tasks=0
    nlohmann::json config;
    config["command"] = "/bin/sleep 1";

    const int N = 5;
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&, i]() {
            std::string id = "reg116c-" + std::to_string(i);
            auto r = executor.submit(id, "command", config, 10,
                REGRESSION_LOG_DIR, "", nullptr);
            if (r.ok()) accepted.fetch_add(1);
            else rejected.fetch_add(1);
        });
    }
    for (auto& th : threads) th.join();

    INFO("max_tasks=0: accepted=" << accepted.load() << " rejected=" << rejected.load());

    // max_tasks=0 时 CAS 循环 current < 0 永不成立，所有 submit 被拒绝
    REQUIRE(accepted.load() == 0);
    REQUIRE(rejected.load() == N);
    REQUIRE(executor.runningCount() == 0);
}

// Fix #264: 补充 max_tasks 负值边界测试
TEST_CASE("Regression #116: negative max_tasks rejects all submissions", "[regression][executor]") {
    std::filesystem::create_directories(REGRESSION_LOG_DIR);

    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};

    TaskExecutor executor(-1);  // 负值 max_tasks
    nlohmann::json config;
    config["command"] = "/bin/sleep 1";

    auto r = executor.submit("reg116d-0", "command", config, 10,
        REGRESSION_LOG_DIR, "", nullptr);
    if (r.ok()) accepted.fetch_add(1);
    else rejected.fetch_add(1);

    INFO("max_tasks=-1: accepted=" << accepted.load() << " rejected=" << rejected.load());

    REQUIRE(accepted.load() == 0);
    REQUIRE(rejected.load() == 1);
    REQUIRE(executor.runningCount() == 0);
}

// Fix #264: 补充未知 task_type 错误路径测试 —— 验证 running_count 正确回滚
TEST_CASE("Regression #116: unknown task_type rolls back running count", "[regression][executor]") {
    std::filesystem::create_directories(REGRESSION_LOG_DIR);

    TaskExecutor executor(5);
    nlohmann::json config;
    config["command"] = "/bin/echo test";

    // 提交未知 task_type，应返回 failure 且 running_count 不增加
    auto r = executor.submit("reg116e-0", "unknown_type", config, 10,
        REGRESSION_LOG_DIR, "", nullptr);
    REQUIRE_FALSE(r.ok());

    // running_count 应为 0（错误路径正确回滚）
    REQUIRE(executor.runningCount() == 0);

    // 后续正常 submit 应仍能成功（证明 running_count 未被错误占用）
    auto r2 = executor.submit("reg116e-1", "command", config, 10,
        REGRESSION_LOG_DIR, "", nullptr);
    REQUIRE(r2.ok());

    // 等待任务完成
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (executor.runningCount() > 0) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
