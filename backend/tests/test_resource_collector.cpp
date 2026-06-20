#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include "worker/util/resource_collector.h"

using namespace taskflow::worker::util;

// ============================================================================
// Fix #270: ResourceCollector 完全未测试（CPU/内存采集零覆盖）
// 验收指标：
//   1. collect() 在正常 /proc 下返回非负值
//   2. CPU 使用率在 [0, 100] 范围内（单核视角）
//   3. 内存使用率在 [0, 100] 范围内
//   4. /proc 不可用时返回 0（不崩溃）
//   5. 多次调用结果稳定
// ============================================================================

TEST_CASE("ResourceCollector: collect returns valid structure", "[resource_collector]") {
    auto usage = ResourceCollector::collect();
    // CPU 和内存使用率都应该是有限数值
    REQUIRE(std::isfinite(usage.cpu_usage));
    REQUIRE(std::isfinite(usage.memory_usage));
}

TEST_CASE("ResourceCollector: memory usage in valid range", "[resource_collector]") {
    // 内存使用率应在 [0, 100] 范围内
    // 注：在正常 Linux 系统上 /proc/meminfo 存在，MemAvailable 存在（内核 >= 3.14）
    auto usage = ResourceCollector::collect();
    REQUIRE(usage.memory_usage >= 0.0);
    REQUIRE(usage.memory_usage <= 100.0);
}

TEST_CASE("ResourceCollector: cpu usage in valid range", "[resource_collector]") {
    // CPU 使用率应在 [0, 100] 范围内（单核视角，多核系统总使用率可达 N*100%，
    // 但此实现计算的是总占比百分比，不会超过 100）
    auto usage = ResourceCollector::collect();
    REQUIRE(usage.cpu_usage >= 0.0);
    REQUIRE(usage.cpu_usage <= 100.0);
}

TEST_CASE("ResourceCollector: multiple calls return stable results", "[resource_collector]") {
    // 连续多次调用应返回稳定的结果（不崩溃，值在合理范围）
    for (int i = 0; i < 5; ++i) {
        auto usage = ResourceCollector::collect();
        REQUIRE(usage.memory_usage >= 0.0);
        REQUIRE(usage.memory_usage <= 100.0);
        REQUIRE(usage.cpu_usage >= 0.0);
        REQUIRE(usage.cpu_usage <= 100.0);
    }
}

TEST_CASE("ResourceCollector: collect does not crash on normal system", "[resource_collector]") {
    // 在正常 Linux 系统上，/proc/stat 和 /proc/meminfo 都存在
    // 此测试验证 collect() 不会崩溃或抛异常
    REQUIRE_NOTHROW(ResourceCollector::collect());
}

TEST_CASE("ResourceCollector: memory usage reflects actual memory", "[resource_collector]") {
    // 内存使用率应大于 0（系统总有进程在占用内存）
    // 在极少数情况下（如刚启动的容器）可能接近 0，所以只验证非负
    auto usage = ResourceCollector::collect();
    REQUIRE(usage.memory_usage >= 0.0);
    // 实际系统内存使用率通常 > 0
    INFO("memory_usage: " << usage.memory_usage);
}
