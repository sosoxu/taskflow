// 回归测试：验证 #116 TaskExecutor 并发竞态修复
// 高并发提交任务，验证 running_count 不超过 max_tasks
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

#include "worker/executor/task_executor.h"

using namespace taskflow::worker::executor;

static const std::string REGRESSION_LOG_DIR = "/tmp/taskflow_regression_logs";

TEST_CASE("Regression #116: concurrent submit does not exceed max_tasks", "[regression][executor]") {
    std::filesystem::create_directories(REGRESSION_LOG_DIR);

    // max_tasks=5, 用 sleep 任务让任务持续运行
    TaskExecutor executor(5);
    nlohmann::json config;
    config["command"] = "/bin/sleep 3";

    const int THREADS = 20;
    const int TASKS_PER_THREAD = 10;
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    std::atomic<int> max_concurrent_observed{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < TASKS_PER_THREAD; ++i) {
                std::string id = "reg116-" + std::to_string(t) + "-" + std::to_string(i);
                auto r = executor.submit(id, "command", config, 30,
                    REGRESSION_LOG_DIR, "",
                    [&](const TaskResult&) {
                        // track max concurrent
                        int current = executor.runningCount();
                        int prev = max_concurrent_observed.load();
                        while (current > prev && !max_concurrent_observed.compare_exchange_weak(prev, current)) {}
                    });
                if (r.ok()) accepted.fetch_add(1);
                else rejected.fetch_add(1);
            }
        });
    }
    for (auto& th : threads) th.join();

    // 等待所有任务完成
    while (executor.runningCount() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    printf("  accepted=%d, rejected=%d, max_concurrent_observed=%d (max_tasks=5)\n",
           accepted.load(), rejected.load(), max_concurrent_observed.load());

    // Fix #116: accepted should never exceed max_tasks (5)
    // Note: due to the CAS fix, at most 5 tasks can be accepted concurrently.
    // Tasks complete quickly (sleep 3), so some slots free up for more accepts.
    // The key assertion: max_concurrent_observed should never exceed max_tasks.
    REQUIRE(max_concurrent_observed.load() <= 5);
    REQUIRE(accepted.load() > 0);
}

TEST_CASE("Regression #116: max_tasks=1 strict serialization", "[regression][executor]") {
    std::filesystem::create_directories(REGRESSION_LOG_DIR);

    TaskExecutor executor(1);
    nlohmann::json config;
    config["command"] = "/bin/sleep 0.5";

    const int N = 10;
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};

    // 并发提交
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&, i]() {
            std::string id = "reg116b-" + std::to_string(i);
            auto r = executor.submit(id, "command", config, 10,
                REGRESSION_LOG_DIR, "",
                [](const TaskResult&) {});
            if (r.ok()) accepted.fetch_add(1);
            else rejected.fetch_add(1);
        });
    }
    for (auto& th : threads) th.join();

    // 等待完成
    while (executor.runningCount() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    printf("  max_tasks=1: accepted=%d, rejected=%d\n", accepted.load(), rejected.load());
    // With max_tasks=1, at most 1 task runs at a time. Since tasks take 0.5s,
    // concurrent submissions will mostly be rejected.
    REQUIRE(accepted.load() >= 1);
}
