#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <map>
#include "scheduler/service/dag_validator.h"
#include "scheduler/engine/dag_engine.h"
#include "scheduler/engine/cron_parser.h"

using namespace taskflow::scheduler::service;
using namespace taskflow::scheduler::engine;

// ============================================================================
// DagValidator 扩展测试
// 验收指标：
//   1. 自环边被拒绝
//   2. 边指向不存在的节点被拒绝
//   3. 边源从不存在的节点被拒绝
//   4. 重复节点 ID 被拒绝
//   5. 多节点无孤立节点合法
//   6. 菱形 DAG 合法
//   7. 缺少 nodes 字段被拒绝
//   8. 缺少 edges 字段合法（仅依赖 dependencies 字段声明依赖）
// ============================================================================

TEST_CASE("DagValidator: self-loop edge rejected", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "a"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    // 验证错误消息包含 "cycle"（自环被环检测捕获）
    REQUIRE(result.error().find("cycle") != std::string::npos);
}

TEST_CASE("DagValidator: edge to non-existent node rejected", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "nonexistent"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    // 验证错误消息包含 target 和不存在的节点 ID
    REQUIRE(result.error().find("target") != std::string::npos);
    REQUIRE(result.error().find("nonexistent") != std::string::npos);
}

TEST_CASE("DagValidator: edge from non-existent node rejected", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "nonexistent"}, {"target", "a"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    // 验证错误消息包含 source 和不存在的节点 ID
    REQUIRE(result.error().find("source") != std::string::npos);
    REQUIRE(result.error().find("nonexistent") != std::string::npos);
}

TEST_CASE("DagValidator: duplicate node id is rejected (Fix #158)", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t2"}});  // duplicate id

    // Fix #158: Duplicate node IDs are now rejected (previously deduped
    // silently by unordered_set, causing data loss).
    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("Duplicate node ID") != std::string::npos);
}

TEST_CASE("DagValidator: diamond DAG is valid", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    // A -> B, A -> C, B -> D, C -> D
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["nodes"].push_back({{"id", "d"}, {"task_id", "t4"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "d"}});
    dag["edges"].push_back({{"source", "c"}, {"target", "d"}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator: two nodes connected is valid", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator: node missing id field rejected", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"task_id", "t1"}});  // missing id

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    // 验证错误消息包含 "id"
    REQUIRE(result.error().find("id") != std::string::npos);
}

TEST_CASE("DagValidator: edge missing source rejected", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"target", "a"}});  // missing source

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    // 验证错误消息包含 "source"
    REQUIRE(result.error().find("source") != std::string::npos);
}

TEST_CASE("DagValidator: edge missing target rejected", "[dag_validator_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}});  // missing target

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    // 验证错误消息包含 "target"
    REQUIRE(result.error().find("target") != std::string::npos);
}

TEST_CASE("DagValidator: missing edges field is valid (dependencies only)", "[dag_validator_ext]") {
    // 验收指标 8：缺少 edges 字段合法（仅依赖 dependencies 字段声明依赖）
    // 之前文件头注释错误地声称"缺少 edges 字段被拒绝"，实际源码第 58 行
    // `if (dag_json.contains("edges") && dag_json["edges"].is_array())` 不存在 edges 时跳过
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    // 不设置 edges 字段，仅用 dependencies 声明依赖
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}, {"dependencies", nlohmann::json::array({"a"})}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator: missing nodes field rejected", "[dag_validator_ext]") {
    // 验收指标 7：缺少 nodes 字段被拒绝
    nlohmann::json dag;
    dag["edges"] = nlohmann::json::array();
    // 不设置 nodes 字段

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("nodes") != std::string::npos);
}

// ============================================================================
// DagEngine 扩展测试
// ============================================================================

TEST_CASE("DagEngine: diamond DAG topological sort", "[dag_engine_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    // A -> B, A -> C, B -> D, C -> D
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["nodes"].push_back({{"id", "d"}, {"task_id", "t4"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "d"}});
    dag["edges"].push_back({{"source", "c"}, {"target", "d"}});

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE(result.ok());

    auto levels = result.value();
    REQUIRE(levels.size() == 3);  // [a], [b,c], [d]
    REQUIRE(levels[0].size() == 1);
    REQUIRE(levels[0][0] == "a");
    // 验证 levels[1] 包含 b 和 c（之前未验证中间层成员内容）
    REQUIRE(levels[1].size() == 2);
    std::set<std::string> layer1(levels[1].begin(), levels[1].end());
    REQUIRE(layer1.count("b") == 1);
    REQUIRE(layer1.count("c") == 1);
    REQUIRE(levels[2].size() == 1);
    REQUIRE(levels[2][0] == "d");
}

TEST_CASE("DagEngine: single node topological sort", "[dag_engine_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    REQUIRE(result.value()[0].size() == 1);
    REQUIRE(result.value()[0][0] == "a");
}

TEST_CASE("DagEngine: findReadyTasks with diamond DAG", "[dag_engine_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["nodes"].push_back({{"id", "d"}, {"task_id", "t4"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "d"}});
    dag["edges"].push_back({{"source", "c"}, {"target", "d"}});

    // Initially only 'a' is ready
    std::map<std::string, std::string> s1 = {
        {"a", "PENDING"}, {"b", "PENDING"}, {"c", "PENDING"}, {"d", "PENDING"}
    };
    auto ready1 = DagEngine::findReadyTasks(dag, s1);
    REQUIRE(ready1.size() == 1);
    REQUIRE(ready1.count("a") == 1);

    // After 'a' succeeds, 'b' and 'c' are ready
    std::map<std::string, std::string> s2 = {
        {"a", "SUCCESS"}, {"b", "PENDING"}, {"c", "PENDING"}, {"d", "PENDING"}
    };
    auto ready2 = DagEngine::findReadyTasks(dag, s2);
    REQUIRE(ready2.size() == 2);
    REQUIRE(ready2.count("b") == 1);
    REQUIRE(ready2.count("c") == 1);

    // After 'b' and 'c' succeed, 'd' is ready
    std::map<std::string, std::string> s3 = {
        {"a", "SUCCESS"}, {"b", "SUCCESS"}, {"c", "SUCCESS"}, {"d", "PENDING"}
    };
    auto ready3 = DagEngine::findReadyTasks(dag, s3);
    REQUIRE(ready3.size() == 1);
    REQUIRE(ready3.count("d") == 1);
}

TEST_CASE("DagEngine: findBlockedTasks with diamond DAG", "[dag_engine_ext]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["nodes"].push_back({{"id", "d"}, {"task_id", "t4"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "d"}});
    dag["edges"].push_back({{"source", "c"}, {"target", "d"}});

    // 'a' FAILED: 'b' and 'c' are blocked
    std::map<std::string, std::string> s1 = {
        {"a", "FAILED"}, {"b", "PENDING"}, {"c", "PENDING"}, {"d", "PENDING"}
    };
    auto blocked1 = DagEngine::findBlockedTasks(dag, s1);
    REQUIRE(blocked1.size() == 2);
    REQUIRE(blocked1.count("b") == 1);
    REQUIRE(blocked1.count("c") == 1);

    // 'b' FAILED, 'c' SUCCESS: 'd' is blocked (has a failed upstream)
    std::map<std::string, std::string> s2 = {
        {"a", "SUCCESS"}, {"b", "FAILED"}, {"c", "SUCCESS"}, {"d", "PENDING"}
    };
    auto blocked2 = DagEngine::findBlockedTasks(dag, s2);
    REQUIRE(blocked2.size() == 1);
    REQUIRE(blocked2.count("d") == 1);
}

TEST_CASE("DagEngine: allTasksFinished with various statuses", "[dag_engine_ext]") {
    // TIMEOUT counts as finished
    std::map<std::string, std::string> s1 = {
        {"a", "SUCCESS"}, {"b", "TIMEOUT"}
    };
    REQUIRE(DagEngine::allTasksFinished(s1));

    // CANCELLED counts as finished
    std::map<std::string, std::string> s2 = {
        {"a", "CANCELLED"}, {"b", "FAILED"}
    };
    REQUIRE(DagEngine::allTasksFinished(s2));

    // UPSTREAM_FAILED counts as finished
    std::map<std::string, std::string> s3 = {
        {"a", "UPSTREAM_FAILED"}
    };
    REQUIRE(DagEngine::allTasksFinished(s3));

    // NODE_OFFLINE counts as finished（之前未覆盖此终态）
    std::map<std::string, std::string> s4 = {
        {"a", "NODE_OFFLINE"}, {"b", "SUCCESS"}
    };
    REQUIRE(DagEngine::allTasksFinished(s4));

    // DISPATCHED is not finished
    std::map<std::string, std::string> s5 = {
        {"a", "DISPATCHED"}
    };
    REQUIRE_FALSE(DagEngine::allTasksFinished(s5));

    // RUNNING is not finished（之前未覆盖此非终态）
    std::map<std::string, std::string> s6 = {
        {"a", "RUNNING"}, {"b", "SUCCESS"}
    };
    REQUIRE_FALSE(DagEngine::allTasksFinished(s6));

    // PENDING is not finished（之前未覆盖此非终态）
    std::map<std::string, std::string> s7 = {
        {"a", "PENDING"}
    };
    REQUIRE_FALSE(DagEngine::allTasksFinished(s7));

    // 空映射视为全部完成
    std::map<std::string, std::string> s8;
    REQUIRE(DagEngine::allTasksFinished(s8));

    // 未知状态不视为终态
    std::map<std::string, std::string> s9 = {
        {"a", "UNKNOWN_STATUS"}
    };
    REQUIRE_FALSE(DagEngine::allTasksFinished(s9));
}

// ============================================================================
// CronParser 扩展测试
// ============================================================================

TEST_CASE("CronParser: every second", "[cron_parser_ext]") {
    auto result = CronParser::getNextTrigger("* * * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    // Next second
    REQUIRE(result.value() == "2025-01-01 00:00:01");
}

TEST_CASE("CronParser: specific day of week", "[cron_parser_ext]") {
    // Every Monday at 9:00:00 (0=Sunday, 1=Monday in standard cron)
    auto result = CronParser::getNextTrigger("0 0 9 * * 1", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    // 2025-01-01 is Wednesday, next Monday is 2025-01-06
    // 之前只检查日期不检查时间，补充完整时间断言
    REQUIRE(result.value() == "2025-01-06 09:00:00");
}

TEST_CASE("CronParser: every 10 minutes", "[cron_parser_ext]") {
    auto result = CronParser::getNextTrigger("0 */10 * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:10:00");
}

TEST_CASE("CronParser: at second 30 every minute", "[cron_parser_ext]") {
    auto result = CronParser::getNextTrigger("30 * * * * *", "2025-01-01 00:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-01 00:00:30");
}

TEST_CASE("CronParser: 5-field expression (without seconds) is invalid", "[cron_parser_ext]") {
    auto result = CronParser::getNextTrigger("0 * * * *", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser: 7-field expression is invalid", "[cron_parser_ext]") {
    auto result = CronParser::getNextTrigger("0 0 0 0 0 0 0", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser: empty expression is invalid", "[cron_parser_ext]") {
    auto result = CronParser::getNextTrigger("", "2025-01-01 00:00:00");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("CronParser: midnight every day", "[cron_parser_ext]") {
    auto result = CronParser::getNextTrigger("0 0 0 * * *", "2025-01-01 12:00:00");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "2025-01-02 00:00:00");
}
