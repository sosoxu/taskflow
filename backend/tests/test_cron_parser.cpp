#include <catch2/catch_test_macros.hpp>
#include "scheduler/engine/cron_parser.h"

using namespace taskflow::scheduler::engine;

TEST_CASE("CronParser - every minute", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("0 * * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:01:00");
}

TEST_CASE("CronParser - every 5 minutes", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("0 */5 * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:05:00");
}

TEST_CASE("CronParser - specific hour", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("0 0 8 * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 08:00:00");
}

TEST_CASE("CronParser - invalid expression (wrong field count)", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("invalid", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - next time is in the future", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("0 0 8 * * *", "2025-01-01 10:00:00");
    REQUIRE(result.ok());
    // After 10:00, next 8:00 should be the next day
    REQUIRE(result.value() == "2025-01-02 08:00:00");
}

TEST_CASE("CronParser - invalid from_time format", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("0 * * * * *", "not-a-time");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - specific second", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("30 0 8 * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 08:00:30");
}

// ============================================================================
// Fix #247: CronParser 边界测试
// 原测试缺少月/日越界、范围表达式、闰年、from_time 正好触发等场景
// ============================================================================

TEST_CASE("CronParser - month field out of range (13) rejected", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("0 0 0 * 13 *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - day field out of range (32) rejected", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("0 0 0 32 * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - hour field out of range (24) rejected", "[cron_parser]") {
    auto result = CronParser::getNextTrigger("0 0 24 * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - weekday field out of range (7) rejected", "[cron_parser]") {
    // weekday 范围 0-6，7 应被拒绝
    auto result = CronParser::getNextTrigger("0 0 0 * * 7", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - range expression in hour field", "[cron_parser]") {
    // Fix #247: 范围表达式 8-18 表示每小时触发一次
    auto result = CronParser::getNextTrigger("0 0 8-18 * * *", "2025-01-01 07:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 08:00:00");
}

TEST_CASE("CronParser - range expression with step", "[cron_parser]") {
    // Fix #247: 范围带步长 0-30/10 表示 0,10,20,30 秒
    auto result = CronParser::getNextTrigger("0-30/10 * * * * *", "2025-01-01 00:00:05");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:10");
}

TEST_CASE("CronParser - from_time exactly at trigger moment", "[cron_parser]") {
    // Fix #247: from_time 正好是触发时刻时，下一次应为下一个周期（不含当前）
    auto result = CronParser::getNextTrigger("0 * * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    // from_time+1s 开始搜索，所以 00:00:00 的下一次是 00:01:00
    REQUIRE(result.value() == "2025-01-01 00:01:00");
}

TEST_CASE("CronParser - leap year Feb 29 trigger", "[cron_parser]") {
    // Fix #247: 闰年 2 月 29 日触发
    // 2025-01-01 之后最近的 2 月 29 日是 2028-02-29（2024 已过，2028 是闰年）
    auto result = CronParser::getNextTrigger("0 0 0 29 2 *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2028-02-29 00:00:00");
}

TEST_CASE("CronParser - non-leap year skips Feb 29", "[cron_parser]") {
    // Fix #247: 非闰年 2 月 29 日不存在，应跳到下一年
    // 2024-03-01 之后最近的 2 月 29 日是 2028-02-29
    auto result = CronParser::getNextTrigger("0 0 0 29 2 *", "2024-03-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2028-02-29 00:00:00");
}

TEST_CASE("CronParser - combined day and weekday (both specified)", "[cron_parser]") {
    // Fix #247: day 和 weekday 都指定时，标准 cron 行为是匹配任一
    // 1 日或周一 00:00:00
    auto result = CronParser::getNextTrigger("0 0 0 1 * 1", "2025-01-02 00:00:00");
    REQUIRE(result.ok());
    // 2025-01-02 是周四，下一个周一是 2025-01-06；下一个 1 日是 2025-02-01
    // 取更早的：2025-01-06
    REQUIRE(result.value() == "2025-01-06 00:00:00");
}

TEST_CASE("CronParser - negative step rejected", "[cron_parser]") {
    // Fix #247: 负步长应被拒绝
    auto result = CronParser::getNextTrigger("0 */-5 * * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - zero step rejected", "[cron_parser]") {
    // Fix #247: 步长为 0 应被拒绝
    auto result = CronParser::getNextTrigger("0 */0 * * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - invalid range (start > end) rejected", "[cron_parser]") {
    // Fix #247: 范围起始大于结束应被拒绝
    auto result = CronParser::getNextTrigger("0 0 18-8 * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser - cross-year trigger", "[cron_parser]") {
    // Fix #247: 跨年触发 —— 12 月 31 日 23:59 之后，每年 1 月 1 日触发
    auto result = CronParser::getNextTrigger("0 0 0 1 1 *", "2025-12-31 23:59:59");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2026-01-01 00:00:00");
}

TEST_CASE("CronParser - specific month and day", "[cron_parser]") {
    // Fix #247: 每年 6 月 15 日 12:00:00 触发
    auto result = CronParser::getNextTrigger("0 0 12 15 6 *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-06-15 12:00:00");
}
