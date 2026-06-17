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
