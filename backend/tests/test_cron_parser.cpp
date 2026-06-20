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

// ============================================================================
// Fix #269: CronParser 边界条件与错误消息内容未验证
// 验收指标：
//   1. 错误消息包含可区分的内容（字段名、范围、值）
//   2. 空白字符处理（前导/尾随/多空格/Tab）
//   3. 混合列表与边界范围
//   4. from_time 边界（空字符串、部分格式）
// ============================================================================

TEST_CASE("CronParser - error message contains field count info", "[cron_parser_error_msg]") {
    // Fix #269: 验证错误消息包含字段数信息
    auto result = CronParser::getNextTrigger("invalid", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("6 fields") != std::string::npos);
    REQUIRE(result.error().find("1") != std::string::npos);  // got 1 field
}

TEST_CASE("CronParser - error message contains range bounds for month", "[cron_parser_error_msg]") {
    // Fix #269: 月越界错误消息应包含有效范围
    auto result = CronParser::getNextTrigger("0 0 0 * 13 *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("out of bounds") != std::string::npos);
    REQUIRE(result.error().find("1-12") != std::string::npos);
}

TEST_CASE("CronParser - error message contains range bounds for day", "[cron_parser_error_msg]") {
    // Fix #269: 日越界错误消息应包含有效范围（与月越界可区分）
    auto result = CronParser::getNextTrigger("0 0 0 32 * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("out of bounds") != std::string::npos);
    REQUIRE(result.error().find("1-31") != std::string::npos);
}

TEST_CASE("CronParser - error message distinguishes negative step", "[cron_parser_error_msg]") {
    // Fix #269: 负步长错误消息应包含 "positive"
    auto result = CronParser::getNextTrigger("0 */-5 * * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("positive") != std::string::npos);
}

TEST_CASE("CronParser - error message distinguishes zero step", "[cron_parser_error_msg]") {
    // Fix #269: 零步长错误消息也应包含 "positive"（step <= 0）
    auto result = CronParser::getNextTrigger("0 */0 * * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("positive") != std::string::npos);
}

TEST_CASE("CronParser - error message for invalid from_time", "[cron_parser_error_msg]") {
    // Fix #269: 无效 from_time 错误消息应包含 "from_time"
    auto result = CronParser::getNextTrigger("0 * * * * *", "not-a-time");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("from_time") != std::string::npos);
}

TEST_CASE("CronParser - error message for invalid range start > end", "[cron_parser_error_msg]") {
    // Fix #269: 范围起始大于结束错误消息应包含 "out of bounds"
    auto result = CronParser::getNextTrigger("0 0 18-8 * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("out of bounds") != std::string::npos);
}

TEST_CASE("CronParser - leading whitespace is trimmed", "[cron_parser_whitespace]") {
    // Fix #269: 前导空格应被 istringstream 跳过
    // 使用 sec=5 使期望值明确：从 00:00:00 开始，下一个 sec=5 是 00:00:05
    auto result = CronParser::getNextTrigger("  5 * * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:05");
}

TEST_CASE("CronParser - trailing whitespace is trimmed", "[cron_parser_whitespace]") {
    // Fix #269: 尾随空格应被 istringstream 跳过
    auto result = CronParser::getNextTrigger("5 * * * * *  ", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:05");
}

TEST_CASE("CronParser - multiple spaces between fields", "[cron_parser_whitespace]") {
    // Fix #269: 字段间多空格应被正确处理
    auto result = CronParser::getNextTrigger("5  *  *  *  *  *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:05");
}

TEST_CASE("CronParser - tab separated fields", "[cron_parser_whitespace]") {
    // Fix #269: Tab 分隔应被正确处理
    auto result = CronParser::getNextTrigger("5\t*\t*\t*\t*\t*", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:05");
}

TEST_CASE("CronParser - single element list", "[cron_parser_list]") {
    // Fix #269: 单元素列表 "5" 等价于单值
    auto result = CronParser::getNextTrigger("5 * * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:05");
}

TEST_CASE("CronParser - same start and end range", "[cron_parser_list]") {
    // Fix #269: 相同起止范围 "5-5" 等价于单值
    auto result = CronParser::getNextTrigger("0 0 8 5-5 * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-05 08:00:00");
}

TEST_CASE("CronParser - step larger than range produces single value", "[cron_parser_list]") {
    // Fix #269: 步长大于范围时只产生起始值
    // 0-10/100 只产生 {0}，从 00:00:00 开始下一个 sec=0 是 00:01:00
    auto result = CronParser::getNextTrigger("0-10/100 * * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:01:00");
}

TEST_CASE("CronParser - mixed list and range", "[cron_parser_list]") {
    // Fix #269: 混合列表与范围 "0,15-30/15"
    auto result = CronParser::getNextTrigger("0,15-30/15 * * * * *", "2025-01-01 00:00:05");
    REQUIRE(result.ok());
    // 值为 0,15,30；从 00:00:06 开始搜索，下一个是 00:00:15
    REQUIRE(result.value() == "2025-01-01 00:00:15");
}

TEST_CASE("CronParser - empty from_time rejected", "[cron_parser_from_time]") {
    // Fix #269: 空 from_time 应被拒绝
    auto result = CronParser::getNextTrigger("0 * * * * *", "");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("from_time") != std::string::npos);
}

TEST_CASE("CronParser - partial from_time format uses lenient parsing", "[cron_parser_from_time]") {
    // Fix #269: std::get_time 对部分格式 "2025-01-01"（缺时间）采取宽松解析，
    // 缺失部分默认为 0，即等价于 "2025-01-01 00:00:00"。验证不崩溃且返回结果。
    auto result = CronParser::getNextTrigger("5 * * * * *", "2025-01-01");
    // 宽松解析接受部分格式，不返回错误
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:05");
}

TEST_CASE("CronParser - from_time with missing seconds uses lenient parsing", "[cron_parser_from_time]") {
    // Fix #269: "2025-01-01 00:00"（缺秒）被 std::get_time 宽松解析为 00:00:00
    auto result = CronParser::getNextTrigger("5 * * * * *", "2025-01-01 00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:05");
}

TEST_CASE("CronParser - from_time at boundary 23:59:59", "[cron_parser_from_time]") {
    // Fix #269: 边界时刻 23:59:59
    auto result = CronParser::getNextTrigger("0 * * * * *", "2025-01-01 23:59:59");
    REQUIRE(result.ok());
    // 下一秒是次日 00:00:00，但每分钟触发，下一个是 00:00:00 次日
    REQUIRE(result.value() == "2025-01-02 00:00:00");
}

TEST_CASE("CronParser - non-numeric field value rejected", "[cron_parser_invalid]") {
    // Fix #269: 非数字字符应被拒绝（stoi 抛异常被 catch）
    auto result = CronParser::getNextTrigger("abc * * * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("Invalid") != std::string::npos);
}

TEST_CASE("CronParser - value exceeding field range in list rejected", "[cron_parser_invalid]") {
    // Fix #269: 列表中含超范围值 "0,99,15"（99 超出秒范围 0-59）
    auto result = CronParser::getNextTrigger("0,99,15 * * * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("out of bounds") != std::string::npos);
    REQUIRE(result.error().find("0-59") != std::string::npos);
}
