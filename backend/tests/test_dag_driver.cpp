#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "scheduler/engine/dag_driver.h"

using namespace taskflow::scheduler::engine;

// ============================================================================
// Fix #275: DagDriver resolveString/resolvePlaceholders 占位符解析测试
// 验收指标：
//   1. 基本变量替换 ${var}
//   2. 多变量替换
//   3. 变量不存在时保留原样
//   4. 空变量名 ${} 行为
//   5. 未闭合的 ${ 行为
//   6. 字符串值替换
//   7. 非字符串值（数字/对象/数组）替换为 JSON
//   8. resolvePlaceholders 递归处理 object
//   9. resolvePlaceholders 递归处理 array
//  10. 嵌套对象处理
//  11. 无占位符的字符串不变
//  12. 空字符串处理
// ============================================================================

TEST_CASE("DagDriver::resolveString - basic variable substitution", "[dag_driver_resolve]") {
    // Fix #275: 基本变量替换 ${var}
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("hello ${name}", params);
    REQUIRE(result == "hello taskflow");
}

TEST_CASE("DagDriver::resolveString - multiple variables", "[dag_driver_resolve]") {
    // Fix #275: 多变量替换
    nlohmann::json params = {{"first", "Hello"}, {"second", "World"}};
    std::string result = DagDriver::resolveString("${first} ${second}!", params);
    REQUIRE(result == "Hello World!");
}

TEST_CASE("DagDriver::resolveString - variable not found keeps placeholder", "[dag_driver_resolve]") {
    // Fix #275: 变量不存在时保留原样
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("hello ${unknown}", params);
    REQUIRE(result == "hello ${unknown}");
}

TEST_CASE("DagDriver::resolveString - empty variable name", "[dag_driver_resolve]") {
    // Fix #275: 空变量名 ${} —— 查找空字符串 key
    nlohmann::json params = {{"", "empty_key_value"}};
    std::string result = DagDriver::resolveString("val: ${}", params);
    // 如果 params 含空字符串 key，则替换；否则保留 ${}
    REQUIRE(result == "val: empty_key_value");
}

TEST_CASE("DagDriver::resolveString - empty variable name not in params", "[dag_driver_resolve]") {
    // Fix #275: 空变量名 ${} 且 params 无空字符串 key 时保留原样
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("val: ${}", params);
    REQUIRE(result == "val: ${}");
}

TEST_CASE("DagDriver::resolveString - unclosed brace keeps dollar sign", "[dag_driver_resolve]") {
    // Fix #275: 未闭合的 ${ —— 仅保留 $，后续字符下一轮处理
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("hello ${name", params);
    // 未找到 } 时，保留 $ 并继续处理后续字符
    // 源码第 657-660 行：result += input[i]（即 $），i++
    REQUIRE(result.find("$") != std::string::npos);
}

TEST_CASE("DagDriver::resolveString - no placeholder returns original", "[dag_driver_resolve]") {
    // Fix #275: 无占位符的字符串不变
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("plain text no placeholder", params);
    REQUIRE(result == "plain text no placeholder");
}

TEST_CASE("DagDriver::resolveString - empty string returns empty", "[dag_driver_resolve]") {
    // Fix #275: 空字符串处理
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("", params);
    REQUIRE(result.empty());
}

TEST_CASE("DagDriver::resolveString - dollar sign without brace", "[dag_driver_resolve]") {
    // Fix #275: $ 后非 { 时，$ 作为普通字符
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("price: $100", params);
    REQUIRE(result == "price: $100");
}

TEST_CASE("DagDriver::resolveString - integer value substituted as JSON", "[dag_driver_resolve]") {
    // Fix #275: 非字符串值（整数）替换为 JSON dump
    nlohmann::json params = {{"count", 42}};
    std::string result = DagDriver::resolveString("total: ${count}", params);
    // 整数 dump 为 "42"
    REQUIRE(result == "total: 42");
}

TEST_CASE("DagDriver::resolveString - boolean value substituted as JSON", "[dag_driver_resolve]") {
    // Fix #275: 布尔值替换为 JSON dump
    nlohmann::json params = {{"enabled", true}};
    std::string result = DagDriver::resolveString("flag: ${enabled}", params);
    REQUIRE(result == "flag: true");
}

TEST_CASE("DagDriver::resolveString - object value substituted as JSON", "[dag_driver_resolve]") {
    // Fix #275: 对象值替换为 JSON dump
    nlohmann::json params = {{"config", {{"key", "value"}}}};
    std::string result = DagDriver::resolveString("cfg: ${config}", params);
    REQUIRE(result.find("\"key\"") != std::string::npos);
    REQUIRE(result.find("value") != std::string::npos);
}

TEST_CASE("DagDriver::resolveString - array value substituted as JSON", "[dag_driver_resolve]") {
    // Fix #275: 数组值替换为 JSON dump
    nlohmann::json params = {{"items", nlohmann::json::array({1, 2, 3})}};
    std::string result = DagDriver::resolveString("items: ${items}", params);
    REQUIRE(result == "items: [1,2,3]");
}

TEST_CASE("DagDriver::resolveString - adjacent placeholders", "[dag_driver_resolve]") {
    // Fix #275: 相邻的占位符
    nlohmann::json params = {{"a", "X"}, {"b", "Y"}};
    std::string result = DagDriver::resolveString("${a}${b}", params);
    REQUIRE(result == "XY");
}

TEST_CASE("DagDriver::resolveString - placeholder at start and end", "[dag_driver_resolve]") {
    // Fix #275: 占位符在字符串开头和结尾
    nlohmann::json params = {{"start", "BEGIN"}, {"end", "END"}};
    std::string result = DagDriver::resolveString("${start} middle ${end}", params);
    REQUIRE(result == "BEGIN middle END");
}

TEST_CASE("DagDriver::resolvePlaceholders - string value in json", "[dag_driver_resolve]") {
    // Fix #275: resolvePlaceholders 处理 JSON 中的字符串值
    nlohmann::json config = {{"command", "echo ${name}"}};
    nlohmann::json params = {{"name", "taskflow"}};

    DagDriver::resolvePlaceholders(config, params);
    REQUIRE(config["command"] == "echo taskflow");
}

TEST_CASE("DagDriver::resolvePlaceholders - recursive object processing", "[dag_driver_resolve]") {
    // Fix #275: resolvePlaceholders 递归处理嵌套对象
    nlohmann::json config = {
        {"outer", {
            {"inner", "value: ${var}"}
        }}
    };
    nlohmann::json params = {{"var", "resolved"}};

    DagDriver::resolvePlaceholders(config, params);
    REQUIRE(config["outer"]["inner"] == "value: resolved");
}

TEST_CASE("DagDriver::resolvePlaceholders - array processing", "[dag_driver_resolve]") {
    // Fix #275: resolvePlaceholders 递归处理数组
    nlohmann::json config = nlohmann::json::array({"${a}", "${b}", "plain"});
    nlohmann::json params = {{"a", "X"}, {"b", "Y"}};

    DagDriver::resolvePlaceholders(config, params);
    REQUIRE(config[0] == "X");
    REQUIRE(config[1] == "Y");
    REQUIRE(config[2] == "plain");
}

TEST_CASE("DagDriver::resolvePlaceholders - mixed nested structure", "[dag_driver_resolve]") {
    // Fix #275: resolvePlaceholders 处理混合嵌套结构（对象含数组含对象）
    nlohmann::json config = {
        {"items", nlohmann::json::array({
            {{"name", "item-${id}"}},
            {{"value", "${val}"}}
        })}
    };
    nlohmann::json params = {{"id", "1"}, {"val", "42"}};

    DagDriver::resolvePlaceholders(config, params);
    REQUIRE(config["items"][0]["name"] == "item-1");
    REQUIRE(config["items"][1]["value"] == "42");
}

TEST_CASE("DagDriver::resolvePlaceholders - non-string values unchanged", "[dag_driver_resolve]") {
    // Fix #275: 非字符串值（数字、布尔、对象）不被处理
    nlohmann::json config = {
        {"count", 42},
        {"enabled", true},
        {"nested", {{"key", "value"}}}
    };
    nlohmann::json params = {{"var", "resolved"}};

    DagDriver::resolvePlaceholders(config, params);
    // 非字符串值应保持不变
    REQUIRE(config["count"] == 42);
    REQUIRE(config["enabled"] == true);
    REQUIRE(config["nested"]["key"] == "value");
}

TEST_CASE("DagDriver::resolvePlaceholders - empty params", "[dag_driver_resolve]") {
    // Fix #275: 空 params 时占位符保留原样
    nlohmann::json config = {{"cmd", "echo ${name}"}};
    nlohmann::json params = nlohmann::json::object();

    DagDriver::resolvePlaceholders(config, params);
    REQUIRE(config["cmd"] == "echo ${name}");
}

TEST_CASE("DagDriver::resolvePlaceholders - empty config", "[dag_driver_resolve]") {
    // Fix #275: 空 config 不崩溃
    nlohmann::json config = nlohmann::json::object();
    nlohmann::json params = {{"var", "value"}};

    REQUIRE_NOTHROW(DagDriver::resolvePlaceholders(config, params));
    REQUIRE(config.empty());
}

TEST_CASE("DagDriver::resolvePlaceholders - null config value", "[dag_driver_resolve]") {
    // Fix #275: config 含 null 值不崩溃
    nlohmann::json config = {{"null_field", nullptr}};
    nlohmann::json params = {{"var", "value"}};

    REQUIRE_NOTHROW(DagDriver::resolvePlaceholders(config, params));
    REQUIRE(config["null_field"].is_null());
}

TEST_CASE("DagDriver::resolveString - same variable used multiple times", "[dag_driver_resolve]") {
    // Fix #275: 同一变量被多次引用
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("${name} and ${name} again", params);
    REQUIRE(result == "taskflow and taskflow again");
}

// ============================================================================
// {var} implicit placeholder syntax tests
// ============================================================================

TEST_CASE("DagDriver::resolveString - {var} basic substitution", "[dag_driver_resolve]") {
    // {var} syntax: replaced when var is defined in params
    nlohmann::json params = {{"time", "10"}};
    std::string result = DagDriver::resolveString("sleep {time}", params);
    REQUIRE(result == "sleep 10");
}

TEST_CASE("DagDriver::resolveString - {var} not in params keeps literal", "[dag_driver_resolve]") {
    // {var} syntax: kept as-is when var is NOT defined in params
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("sleep {time}", params);
    REQUIRE(result == "sleep {time}");
}

TEST_CASE("DagDriver::resolveString - {var} with underscore and hyphen", "[dag_driver_resolve]") {
    // {var} syntax: variable names can contain underscores and hyphens
    nlohmann::json params = {{"my_var", "hello"}, {"db-name", "prod"}};
    std::string result = DagDriver::resolveString("{my_var} {db-name}", params);
    REQUIRE(result == "hello prod");
}

TEST_CASE("DagDriver::resolveString - {var} invalid chars kept as literal", "[dag_driver_resolve]") {
    // {var} syntax: variable names with special chars (dots, colons) are NOT treated as placeholders
    nlohmann::json params = {{"var", "value"}};
    std::string result = DagDriver::resolveString("echo {1..10} {host:port}", params);
    REQUIRE(result == "echo {1..10} {host:port}");
}

TEST_CASE("DagDriver::resolveString - mixed ${var} and {var}", "[dag_driver_resolve]") {
    // Both ${var} and {var} should work together
    nlohmann::json params = {{"name", "taskflow"}, {"count", "5"}};
    std::string result = DagDriver::resolveString("${name} runs {count} times", params);
    REQUIRE(result == "taskflow runs 5 times");
}

TEST_CASE("DagDriver::resolveString - {var} empty var name kept as literal", "[dag_driver_resolve]") {
    // {} with empty name should be kept as-is
    nlohmann::json params = {{"name", "taskflow"}};
    std::string result = DagDriver::resolveString("empty: {}", params);
    REQUIRE(result == "empty: {}");
}

TEST_CASE("DagDriver::resolveString - {var} with integer value", "[dag_driver_resolve]") {
    // {var} with non-string value should dump as JSON
    nlohmann::json params = {{"count", 42}};
    std::string result = DagDriver::resolveString("total: {count}", params);
    REQUIRE(result == "total: 42");
}

TEST_CASE("DagDriver::resolveString - JSON object literal not treated as placeholder", "[dag_driver_resolve]") {
    // {"key": "value"} should NOT be treated as a placeholder
    nlohmann::json params = {{"key", "value"}};
    std::string result = DagDriver::resolveString(R"(data: {"key": "value"})", params);
    // The {"key" part has a colon, so it's not a valid var name — kept as-is
    REQUIRE(result.find(R"("key")") != std::string::npos);
}

TEST_CASE("DagDriver::resolvePlaceholders - {var} in command config", "[dag_driver_resolve]") {
    // Real-world scenario: sleep {time} with time=10
    nlohmann::json config = {{"command", "sleep {time}"}};
    nlohmann::json params = {{"time", "10"}};
    DagDriver::resolvePlaceholders(config, params);
    REQUIRE(config["command"] == "sleep 10");
}
