// TaskFlow 性能测试 (Microbenchmark)
//
// 测试目标：测量核心组件的吞吐量和延迟，不依赖完整服务启动。
// 覆盖组件：
//   1. CronParser      - cron 表达式解析与下次触发时间计算
//   2. DagEngine       - DAG 拓扑排序、就绪任务查找
//   3. CryptoUtil      - AES-256-GCM 加解密
//   4. TaskExecutor    - 并发任务提交吞吐量与延迟
//
// 编译：见 /tmp/test-build 链接命令
// 运行：./perf_tests [tag]    例如 ./perf_tests [cron]

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <future>
#include <iomanip>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "scheduler/engine/cron_parser.h"
#include "scheduler/engine/dag_engine.h"
#include "common/util/crypto_util.h"
#include "worker/executor/task_executor.h"
#include "worker/executor/command_executor.h"

using namespace taskflow::scheduler::engine;
using namespace taskflow::common::util;
using namespace taskflow::worker::executor;
namespace fs = std::filesystem;

// ============================================================================
// 计时辅助工具
// ============================================================================

class Timer {
public:
    Timer() : start_(std::chrono::steady_clock::now()) {}
    void reset() { start_ = std::chrono::steady_clock::now(); }
    double elapsedMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }
    double elapsedUs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(now - start_).count();
    }
private:
    std::chrono::steady_clock::time_point start_;
};

// 计算统计信息
struct Stats {
    double min = 0;
    double max = 0;
    double mean = 0;
    double p50 = 0;
    double p95 = 0;
    double p99 = 0;
};

Stats computeStats(std::vector<double>& samples) {
    Stats s;
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    s.min = samples.front();
    s.max = samples.back();
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    s.mean = sum / samples.size();
    s.p50 = samples[samples.size() * 50 / 100];
    s.p95 = samples[samples.size() * 95 / 100];
    s.p99 = samples[samples.size() * 99 / 100];
    return s;
}

void printStats(const std::string& name, const Stats& s, const std::string& unit = "us") {
    printf("  %-40s min=%8.2f  mean=%8.2f  p50=%8.2f  p95=%8.2f  p99=%8.2f  max=%8.2f %s\n",
           name.c_str(), s.min, s.mean, s.p50, s.p95, s.p99, s.max, unit.c_str());
}

// 运行 N 次并返回每次耗时(微秒)
std::vector<double> runBenchmark(int iterations, const std::function<void()>& fn) {
    std::vector<double> times;
    times.reserve(iterations);
    // 预热
    for (int i = 0; i < std::min(100, iterations / 10 + 1); ++i) fn();
    for (int i = 0; i < iterations; ++i) {
        Timer t;
        fn();
        times.push_back(t.elapsedUs());
    }
    return times;
}

// 运行固定时长，返回执行次数和每次耗时
struct ThroughputResult {
    int count;
    double totalMs;
    double perOpUs;
};

ThroughputResult runForDuration(double durationMs, const std::function<void()>& fn) {
    // 预热
    for (int i = 0; i < 50; ++i) fn();
    int count = 0;
    Timer t;
    while (t.elapsedMs() < durationMs) {
        fn();
        ++count;
    }
    ThroughputResult r;
    r.count = count;
    r.totalMs = t.elapsedMs();
    r.perOpUs = r.totalMs * 1000.0 / count;
    return r;
}

// ============================================================================
// 一、CronParser 性能测试
// 测量 cron 表达式解析与下次触发时间计算的吞吐量
// ============================================================================

TEST_CASE("Perf: CronParser throughput for various expressions", "[perf][cron]") {
    printf("\n========== CronParser 性能测试 ==========\n");

    struct TestCase {
        std::string name;
        std::string expr;
        std::string from;
    };
    std::vector<TestCase> cases = {
        {"every_second",          "* * * * * *",       "2025-06-18 10:00:00"},
        {"every_minute",          "0 * * * * *",       "2025-06-18 10:00:30"},
        {"specific_daily",        "0 30 9 * * *",      "2025-06-18 08:00:00"},
        {"step_5min",             "0 */5 * * * *",     "2025-06-18 10:02:00"},
        {"list_seconds",          "0,15,30,45 * * * * *", "2025-06-18 10:00:10"},
        {"cross_month",           "0 0 0 1 * *",       "2025-01-31 12:00:00"},
        {"weekday_friday",        "0 0 0 * * 5",       "2025-06-18 12:00:00"},
    };

    const int ITERS = 10000;
    for (const auto& tc : cases) {
        auto times = runBenchmark(ITERS, [&]() {
            auto r = CronParser::getNextTrigger(tc.expr, tc.from);
            (void)r;
        });
        auto stats = computeStats(times);
        double opsPerSec = 1000000.0 / stats.mean;
        printf("  %-25s %8.0f ops/sec  (mean=%.2fus, p99=%.2fus)\n",
               tc.name.c_str(), opsPerSec, stats.mean, stats.p99);
    }
}

TEST_CASE("Perf: CronParser chained computation (1000 triggers)", "[perf][cron]") {
    printf("\n--- CronParser 链式计算 (1000次连续触发) ---\n");
    std::string expr = "0 */10 * * * *";
    std::string current = "2025-01-01 00:00:00";

    Timer t;
    int ok_count = 0;
    for (int i = 0; i < 1000; ++i) {
        auto r = CronParser::getNextTrigger(expr, current);
        if (r.ok()) {
            current = r.value();
            ++ok_count;
        }
    }
    double elapsed = t.elapsedMs();
    printf("  1000次链式计算: %.2fms (%.2fus/op), 成功=%d\n",
           elapsed, elapsed * 1000.0 / 1000, ok_count);
    REQUIRE(ok_count == 1000);
}

TEST_CASE("Perf: CronParser multi-threaded throughput", "[perf][cron]") {
    printf("\n--- CronParser 多线程吞吐量 ---\n");
    const std::string expr = "0 */5 * * * *";
    const std::string from = "2025-06-18 10:02:00";
    const int THREADS = 4;
    const int ITERS_PER_THREAD = 50000;

    auto worker = [&]() {
        volatile int ok = 0;
        for (int i = 0; i < ITERS_PER_THREAD; ++i) {
            auto r = CronParser::getNextTrigger(expr, from);
            if (r.ok()) ok++;
        }
        return ok;
    };

    Timer t;
    std::vector<std::future<int>> futures;
    for (int i = 0; i < THREADS; ++i) {
        futures.push_back(std::async(std::launch::async, worker));
    }
    int total_ok = 0;
    for (auto& f : futures) total_ok += f.get();
    double elapsed = t.elapsedMs();

    int total_ops = THREADS * ITERS_PER_THREAD;
    printf("  %d线程 x %d次: %.2fms, %.0f ops/sec (总成功=%d)\n",
           THREADS, ITERS_PER_THREAD, elapsed,
           total_ops / (elapsed / 1000.0), total_ok);
    REQUIRE(total_ok == total_ops);
}

// ============================================================================
// 二、DagEngine 性能测试
// 测量大规模 DAG 的拓扑排序和就绪任务查找性能
// ============================================================================

// 生成线性 DAG: n1 -> n2 -> n3 -> ... -> nN
nlohmann::json makeLinearDag(int n) {
    nlohmann::json dag;
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json edges = nlohmann::json::array();
    for (int i = 0; i < n; ++i) {
        nodes.push_back({{"id", "n" + std::to_string(i)},
                         {"task_id", "t1"}, {"task_name", "task"}, {"task_type", "command"}});
        if (i > 0) {
            edges.push_back({{"source", "n" + std::to_string(i - 1)},
                             {"target", "n" + std::to_string(i)}});
        }
    }
    dag["nodes"] = nodes;
    dag["edges"] = edges;
    return dag;
}

// 生成宽 DAG: n0 -> n1, n0 -> n2, ..., n0 -> nN (扇出)
nlohmann::json makeFanOutDag(int n) {
    nlohmann::json dag;
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json edges = nlohmann::json::array();
    for (int i = 0; i < n; ++i) {
        nodes.push_back({{"id", "n" + std::to_string(i)},
                         {"task_id", "t1"}, {"task_name", "task"}, {"task_type", "command"}});
        if (i > 0) {
            edges.push_back({{"source", "n0"}, {"target", "n" + std::to_string(i)}});
        }
    }
    dag["nodes"] = nodes;
    dag["edges"] = edges;
    return dag;
}

// 生成菱形 DAG: 多层并行
nlohmann::json makeDiamondDag(int layers, int width) {
    nlohmann::json dag;
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json edges = nlohmann::json::array();
    for (int l = 0; l < layers; ++l) {
        for (int w = 0; w < width; ++w) {
            std::string id = "L" + std::to_string(l) + "W" + std::to_string(w);
            nodes.push_back({{"id", id}, {"task_id", "t1"}, {"task_name", "task"}, {"task_type", "command"}});
            if (l > 0) {
                // 连接上一层所有节点
                for (int pw = 0; pw < width; ++pw) {
                    std::string prev = "L" + std::to_string(l - 1) + "W" + std::to_string(pw);
                    edges.push_back({{"source", prev}, {"target", id}});
                }
            }
        }
    }
    dag["nodes"] = nodes;
    dag["edges"] = edges;
    return dag;
}

// 生成随机 DAG
nlohmann::json makeRandomDag(int n, int edgeDensity, unsigned seed = 42) {
    std::mt19937 rng(seed);
    nlohmann::json dag;
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json edges = nlohmann::json::array();
    for (int i = 0; i < n; ++i) {
        nodes.push_back({{"id", "n" + std::to_string(i)},
                         {"task_id", "t1"}, {"task_name", "task"}, {"task_type", "command"}});
    }
    // 每个节点连接到后面的 edgeDensity 个随机节点
    for (int i = 0; i < n; ++i) {
        int remaining = n - i - 1;
        int numEdges = std::min(remaining, edgeDensity);
        for (int e = 0; e < numEdges; ++e) {
            int target = i + 1 + (rng() % remaining);
            edges.push_back({{"source", "n" + std::to_string(i)},
                             {"target", "n" + std::to_string(target)}});
        }
    }
    dag["nodes"] = nodes;
    dag["edges"] = edges;
    return dag;
}

TEST_CASE("Perf: DagEngine topologicalSort on various DAG sizes", "[perf][dag]") {
    printf("\n========== DagEngine 拓扑排序性能测试 ==========\n");

    struct TestCase {
        std::string name;
        nlohmann::json dag;
    };
    std::vector<TestCase> cases = {
        {"linear_100",      makeLinearDag(100)},
        {"linear_1000",     makeLinearDag(1000)},
        {"fanout_100",      makeFanOutDag(100)},
        {"fanout_1000",     makeFanOutDag(1000)},
        {"diamond_10x10",   makeDiamondDag(10, 10)},    // 100 nodes
        {"diamond_20x20",   makeDiamondDag(20, 20)},    // 400 nodes
        {"diamond_50x20",   makeDiamondDag(50, 20)},    // 1000 nodes
        {"random_100_d5",   makeRandomDag(100, 5)},
        {"random_500_d3",   makeRandomDag(500, 3)},
        {"random_1000_d2",  makeRandomDag(1000, 2)},
    };

    const int ITERS = 1000;
    for (const auto& tc : cases) {
        int nodeCount = tc.dag["nodes"].size();
        int edgeCount = tc.dag.contains("edges") ? tc.dag["edges"].size() : 0;

        // 先验证正确性
        auto verify = DagEngine::topologicalSort(tc.dag);
        REQUIRE(verify.ok());

        auto times = runBenchmark(ITERS, [&]() {
            auto r = DagEngine::topologicalSort(tc.dag);
            (void)r;
        });
        auto stats = computeStats(times);
        printf("  %-20s nodes=%4d edges=%4d  mean=%7.2fus  p99=%7.2fus  max=%7.2fus\n",
               tc.name.c_str(), nodeCount, edgeCount, stats.mean, stats.p99, stats.max);
    }
}

TEST_CASE("Perf: DagEngine findReadyTasks performance", "[perf][dag]") {
    printf("\n--- DagEngine findReadyTasks 性能 ---\n");

    // 创建一个 1000 节点的随机 DAG，一半任务已完成
    auto dag = makeRandomDag(1000, 2);
    std::map<std::string, std::string> statuses;
    for (int i = 0; i < 1000; ++i) {
        std::string id = "n" + std::to_string(i);
        if (i < 500) {
            statuses[id] = "SUCCESS";
        } else {
            statuses[id] = "PENDING";
        }
    }

    const int ITERS = 500;
    auto times = runBenchmark(ITERS, [&]() {
        auto ready = DagEngine::findReadyTasks(dag, statuses);
        (void)ready;
    });
    auto stats = computeStats(times);
    printf("  findReadyTasks(1000 nodes): mean=%.2fus, p99=%.2fus, max=%.2fus\n",
           stats.mean, stats.p99, stats.max);

    // 测试 allTasksFinished
    auto times2 = runBenchmark(ITERS, [&]() {
        bool done = DagEngine::allTasksFinished(statuses);
        (void)done;
    });
    auto stats2 = computeStats(times2);
    printf("  allTasksFinished(1000 nodes): mean=%.2fus, p99=%.2fus, max=%.2fus\n",
           stats2.mean, stats2.p99, stats2.max);
}

// ============================================================================
// 三、CryptoUtil 性能测试
// 测量 AES-256-GCM 加解密吞吐量
// ============================================================================

TEST_CASE("Perf: CryptoUtil AES-256-GCM encrypt/decrypt", "[perf][crypto]") {
    printf("\n========== CryptoUtil AES-256-GCM 性能测试 ==========\n");

    // 32 字节密钥
    std::string key(32, 'K');
    std::string key2(32, 'K');

    struct TestCase {
        std::string name;
        std::string plaintext;
    };
    std::vector<TestCase> cases = {
        {"16B",   std::string(16, 'A')},
        {"64B",   std::string(64, 'A')},
        {"256B",  std::string(256, 'A')},
        {"1KB",   std::string(1024, 'A')},
        {"4KB",   std::string(4096, 'A')},
        {"16KB",  std::string(16384, 'A')},
    };

    for (const auto& tc : cases) {
        // 先加密一次获取密文用于解密测试
        auto enc = CryptoUtil::encrypt(tc.plaintext, key);
        REQUIRE(enc.ok());
        std::string ciphertext = enc.value();

        // 加密吞吐量
        const int ENC_ITERS = 1000;
        auto enc_times = runBenchmark(ENC_ITERS, [&]() {
            auto r = CryptoUtil::encrypt(tc.plaintext, key);
            (void)r;
        });
        auto enc_stats = computeStats(enc_times);

        // 解密吞吐量
        auto dec_times = runBenchmark(ENC_ITERS, [&]() {
            auto r = CryptoUtil::decrypt(ciphertext, key);
            (void)r;
        });
        auto dec_stats = computeStats(dec_times);

        size_t size = tc.plaintext.size();
        double enc_throughput = size / (enc_stats.mean / 1000000.0) / 1024.0;  // KB/s
        double dec_throughput = size / (dec_stats.mean / 1000000.0) / 1024.0;

        printf("  %-6s enc: %7.2fus (%6.0f KB/s)  dec: %7.2fus (%6.0f KB/s)\n",
               tc.name.c_str(),
               enc_stats.mean, enc_throughput,
               dec_stats.mean, dec_throughput);
    }
}

TEST_CASE("Perf: CryptoUtil multi-threaded encrypt", "[perf][crypto]") {
    printf("\n--- CryptoUtil 多线程加密吞吐量 (4线程, 1KB) ---\n");
    std::string key(32, 'K');
    std::string plaintext(1024, 'X');
    const int THREADS = 4;
    const int ITERS_PER_THREAD = 2000;

    auto worker = [&]() {
        int ok = 0;
        for (int i = 0; i < ITERS_PER_THREAD; ++i) {
            auto r = CryptoUtil::encrypt(plaintext, key);
            if (r.ok()) ok++;
        }
        return ok;
    };

    Timer t;
    std::vector<std::future<int>> futures;
    for (int i = 0; i < THREADS; ++i) {
        futures.push_back(std::async(std::launch::async, worker));
    }
    int total_ok = 0;
    for (auto& f : futures) total_ok += f.get();
    double elapsed = t.elapsedMs();

    int total_ops = THREADS * ITERS_PER_THREAD;
    double throughput = total_ops / (elapsed / 1000.0);
    printf("  %d线程 x %d次: %.2fms, %.0f ops/sec, %.0f KB/s (成功=%d)\n",
           THREADS, ITERS_PER_THREAD, elapsed, throughput,
           throughput * 1.0, total_ok);  // 1KB per op
    REQUIRE(total_ok == total_ops);
}

// ============================================================================
// 四、TaskExecutor 性能测试
// 测量并发任务提交吞吐量和延迟
// ============================================================================

static const std::string PERF_LOG_DIR = "/tmp/taskflow_perf_logs";

TEST_CASE("Perf: TaskExecutor task submission throughput", "[perf][executor]") {
    printf("\n========== TaskExecutor 任务提交吞吐量测试 ==========\n");
    fs::create_directories(PERF_LOG_DIR);

    struct TestCase {
        std::string name;
        std::string command;
        int timeout;
    };
    std::vector<TestCase> cases = {
        {"noop_echo",   "/bin/echo ok",      10},
        {"short_sleep", "/bin/sleep 0.01",   10},
    };

    for (const auto& tc : cases) {
        TaskExecutor executor(1000);
        nlohmann::json config;
        config["command"] = tc.command;

        const int N = 500;
        std::atomic<int> completed{0};
        std::atomic<int> accepted{0};
        std::vector<double> latencies;
        latencies.reserve(N);
        std::mutex lat_mutex;

        Timer t;
        for (int i = 0; i < N; ++i) {
            auto start = std::chrono::steady_clock::now();
            std::string id = "perf-" + tc.name + "-" + std::to_string(i);
            auto r = executor.submit(id, "command", config, tc.timeout,
                PERF_LOG_DIR, "",
                [&, start](const TaskResult&) {
                    auto end = std::chrono::steady_clock::now();
                    double lat = std::chrono::duration<double, std::milli>(end - start).count();
                    {
                        std::lock_guard<std::mutex> lk(lat_mutex);
                        latencies.push_back(lat);
                    }
                    completed.fetch_add(1);
                });
            if (r.ok()) accepted.fetch_add(1);
        }

        // 等待全部完成
        while (completed.load() < accepted.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        double elapsed = t.elapsedMs();

        auto stats = computeStats(latencies);
        double throughput = accepted.load() / (elapsed / 1000.0);
        printf("  %-15s N=%d accepted=%d  total=%.1fms  throughput=%.0f ops/sec  "
               "latency: mean=%.2fms p50=%.2fms p95=%.2fms p99=%.2fms max=%.2fms\n",
               tc.name.c_str(), N, accepted.load(), elapsed, throughput,
               stats.mean, stats.p50, stats.p95, stats.p99, stats.max);
    }
    // 析构会等待所有线程结束
}

TEST_CASE("Perf: TaskExecutor concurrent burst (100 tasks)", "[perf][executor]") {
    printf("\n--- TaskExecutor 并发突发 (100个echo任务) ---\n");
    fs::create_directories(PERF_LOG_DIR);

    TaskExecutor executor(200);
    nlohmann::json config;
    config["command"] = "/bin/echo burst";

    const int N = 100;
    std::atomic<int> completed{0};
    std::atomic<int> accepted{0};

    Timer t;
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&, i]() {
            std::string id = "burst-" + std::to_string(i);
            auto r = executor.submit(id, "command", config, 10,
                PERF_LOG_DIR, "",
                [&](const TaskResult&) { completed.fetch_add(1); });
            if (r.ok()) accepted.fetch_add(1);
        });
    }
    for (auto& th : threads) th.join();

    // 所有提交完成
    double submitMs = t.elapsedMs();

    // 等待全部执行完成
    while (completed.load() < accepted.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    double totalMs = t.elapsedMs();

    printf("  提交 %d 任务: %.2fms (submit), %.2fms (total incl. exec), accepted=%d\n",
           N, submitMs, totalMs, accepted.load());
    printf("  完成数: %d\n", completed.load());
    REQUIRE(completed.load() == accepted.load());
    REQUIRE(accepted.load() > 0);
}

TEST_CASE("Perf: TaskExecutor max_tasks rejection", "[perf][executor]") {
    printf("\n--- TaskExecutor 容量限制测试 (max_tasks=5) ---\n");
    fs::create_directories(PERF_LOG_DIR);

    TaskExecutor executor(5);
    nlohmann::json config;
    config["command"] = "/bin/sleep 2";

    int accepted = 0;
    int rejected = 0;

    // 快速提交 20 个任务，超过容量的应被拒绝
    for (int i = 0; i < 20; ++i) {
        std::string id = "cap-" + std::to_string(i);
        auto r = executor.submit(id, "command", config, 30,
            PERF_LOG_DIR, "",
            [](const TaskResult&) {});
        if (r.ok()) {
            accepted++;
        } else {
            rejected++;
        }
    }

    printf("  提交 20 个 sleep 2s 任务 (max=5): 接受=%d, 拒绝=%d, running=%d\n",
           accepted, rejected, executor.runningCount());
    REQUIRE(accepted <= 5);
    REQUIRE(rejected >= 15);
    // executor 析构会取消所有运行中的任务并等待
}

// ============================================================================
// 五、综合性能汇总
// ============================================================================

TEST_CASE("Perf: Summary - key operations per second", "[perf][summary]") {
    printf("\n========== 性能汇总 (ops/sec) ==========\n");
    printf("%-35s %12s %12s %12s\n", "操作", "ops/sec", "mean(us)", "p99(us)");
    printf("%.75s\n", std::string(75, '-').c_str());

    // CronParser
    {
        std::string expr = "0 */5 * * * *";
        std::string from = "2025-06-18 10:02:00";
        auto times = runBenchmark(10000, [&]() {
            CronParser::getNextTrigger(expr, from);
        });
        auto s = computeStats(times);
        printf("%-35s %12.0f %12.2f %12.2f\n", "CronParser.getNextTrigger",
               1000000.0 / s.mean, s.mean, s.p99);
    }

    // DagEngine topologicalSort (100 nodes)
    {
        auto dag = makeRandomDag(100, 3);
        auto times = runBenchmark(2000, [&]() {
            DagEngine::topologicalSort(dag);
        });
        auto s = computeStats(times);
        printf("%-35s %12.0f %12.2f %12.2f\n", "DagEngine.topologicalSort(100)",
               1000000.0 / s.mean, s.mean, s.p99);
    }

    // DagEngine topologicalSort (1000 nodes)
    {
        auto dag = makeRandomDag(1000, 2);
        auto times = runBenchmark(500, [&]() {
            DagEngine::topologicalSort(dag);
        });
        auto s = computeStats(times);
        printf("%-35s %12.0f %12.2f %12.2f\n", "DagEngine.topologicalSort(1000)",
               1000000.0 / s.mean, s.mean, s.p99);
    }

    // CryptoUtil encrypt (1KB)
    {
        std::string key(32, 'K');
        std::string pt(1024, 'A');
        auto times = runBenchmark(1000, [&]() {
            CryptoUtil::encrypt(pt, key);
        });
        auto s = computeStats(times);
        printf("%-35s %12.0f %12.2f %12.2f\n", "CryptoUtil.encrypt(1KB)",
               1000000.0 / s.mean, s.mean, s.p99);
    }

    // CryptoUtil decrypt (1KB)
    {
        std::string key(32, 'K');
        std::string pt(1024, 'A');
        auto ct = CryptoUtil::encrypt(pt, key).value();
        auto times = runBenchmark(1000, [&]() {
            CryptoUtil::decrypt(ct, key);
        });
        auto s = computeStats(times);
        printf("%-35s %12.0f %12.2f %12.2f\n", "CryptoUtil.decrypt(1KB)",
               1000000.0 / s.mean, s.mean, s.p99);
    }

    printf("%.75s\n", std::string(75, '-').c_str());
    printf("(注: TaskExecutor 吞吐量见 [perf][executor] 测试结果)\n");
}
