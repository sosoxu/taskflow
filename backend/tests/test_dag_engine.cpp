#include <catch2/catch_test_macros.hpp>
#include "scheduler/engine/dag_engine.h"
#include <vector>
#include <string>
#include <map>
#include <set>

using namespace taskflow::scheduler::engine;

TEST_CASE("DagEngine topological sort - linear DAG", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();

    // A -> B -> C
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "c"}});

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE(result.ok());

    auto levels = result.value();
    REQUIRE(levels.size() == 3);
    REQUIRE(levels[0][0] == "a");
    REQUIRE(levels[1][0] == "b");
    REQUIRE(levels[2][0] == "c");
}

TEST_CASE("DagEngine topological sort - parallel DAG", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();

    // A -> B, A -> C
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE(result.ok());

    auto levels = result.value();
    REQUIRE(levels.size() == 2);
    REQUIRE(levels[0].size() == 1);
    REQUIRE(levels[0][0] == "a");
    REQUIRE(levels[1].size() == 2);
}

TEST_CASE("DagEngine topological sort - cycle detection", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();

    // A -> B -> C -> A (cycle)
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "c"}});
    dag["edges"].push_back({{"source", "c"}, {"target", "a"}});

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagEngine findReadyTasks", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();

    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});

    // With all tasks PENDING and no upstream, only 'a' is ready
    std::map<std::string, std::string> statuses1 = {
        {"a", "PENDING"}, {"b", "PENDING"}, {"c", "PENDING"}
    };
    auto ready = DagEngine::findReadyTasks(dag, statuses1);
    REQUIRE(ready.size() == 1);
    REQUIRE(ready.count("a") == 1);

    // With 'a' succeeded, both 'b' and 'c' are ready
    std::map<std::string, std::string> statuses2 = {
        {"a", "SUCCESS"}, {"b", "PENDING"}, {"c", "PENDING"}
    };
    ready = DagEngine::findReadyTasks(dag, statuses2);
    REQUIRE(ready.size() == 2);
    REQUIRE(ready.count("b") == 1);
    REQUIRE(ready.count("c") == 1);
}

TEST_CASE("DagEngine findBlockedTasks", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();

    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    // 'a' FAILED, 'b' should be blocked
    std::map<std::string, std::string> statuses = {
        {"a", "FAILED"}, {"b", "PENDING"}
    };
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.size() == 1);
    REQUIRE(blocked.count("b") == 1);
}

TEST_CASE("DagEngine allTasksFinished", "[dag_engine]") {
    std::map<std::string, std::string> finished = {
        {"a", "SUCCESS"}, {"b", "FAILED"}
    };
    REQUIRE(DagEngine::allTasksFinished(finished));

    std::map<std::string, std::string> not_finished = {
        {"a", "SUCCESS"}, {"b", "RUNNING"}
    };
    REQUIRE_FALSE(DagEngine::allTasksFinished(not_finished));
}

// ============================================================================
// Fix #240: DagEngine 边界测试
// 原测试缺少：空 DAG、缺失 nodes、DISPATCHED 状态、CANCELLED 上游、
// 空 statuses map、重复节点 ID、无效边端点、所有终态判定
// ============================================================================

TEST_CASE("DagEngine topological sort - missing nodes array", "[dag_engine]") {
    nlohmann::json dag;
    dag["edges"] = nlohmann::json::array();

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagEngine topological sort - empty nodes array", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagEngine topological sort - duplicate node ID", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t2"}});  // 重复 ID

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagEngine topological sort - edge source not a node", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "x"}, {"target", "a"}});  // x 不存在

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagEngine topological sort - edge target not a node", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "y"}});  // y 不存在

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagEngine topological sort - no edges field", "[dag_engine]") {
    // 边界：只有 nodes，没有 edges 字段 —— 合法的纯并行 DAG
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);  // 单层，两个并行节点
    REQUIRE(result.value()[0].size() == 2);
}

TEST_CASE("DagEngine findReadyTasks - DISPATCHED is not ready", "[dag_engine]") {
    // Fix #240: DISPATCHED 状态的任务不应被选为就绪（只有 PENDING 才是就绪候选）
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});

    std::map<std::string, std::string> statuses = {{"a", "DISPATCHED"}};
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.empty());
}

TEST_CASE("DagEngine findReadyTasks - RUNNING is not ready", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});

    std::map<std::string, std::string> statuses = {{"a", "RUNNING"}};
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.empty());
}

TEST_CASE("DagEngine findReadyTasks - empty statuses map", "[dag_engine]") {
    // Fix #240: 空 map 边界 —— 没有任何状态记录，无就绪任务
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});

    std::map<std::string, std::string> statuses;
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.empty());
}

TEST_CASE("DagEngine findReadyTasks - missing nodes returns empty", "[dag_engine]") {
    nlohmann::json dag;
    std::map<std::string, std::string> statuses = {{"a", "PENDING"}};
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.empty());
}

TEST_CASE("DagEngine findReadyTasks - upstream CANCELLED blocks downstream", "[dag_engine]") {
    // Fix #240: CANCELLED 属于失败状态集合，应阻塞下游 PENDING 任务
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    std::map<std::string, std::string> statuses = {{"a", "CANCELLED"}, {"b", "PENDING"}};
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.empty());  // b 的上游 a 未 SUCCESS，b 不就绪
}

TEST_CASE("DagEngine findReadyTasks - upstream TIMEOUT blocks downstream", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    std::map<std::string, std::string> statuses = {{"a", "TIMEOUT"}, {"b", "PENDING"}};
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.empty());
}

TEST_CASE("DagEngine findBlockedTasks - CANCELLED upstream blocks", "[dag_engine]") {
    // Fix #240: CANCELLED 是失败状态，应使下游 PENDING 任务出现在 blocked 集合中
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    std::map<std::string, std::string> statuses = {{"a", "CANCELLED"}, {"b", "PENDING"}};
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.size() == 1);
    REQUIRE(blocked.count("b") == 1);
}

TEST_CASE("DagEngine findBlockedTasks - SUCCESS upstream does not block", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    std::map<std::string, std::string> statuses = {{"a", "SUCCESS"}, {"b", "PENDING"}};
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.empty());
}

TEST_CASE("DagEngine findBlockedTasks - empty statuses map", "[dag_engine]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});

    std::map<std::string, std::string> statuses;
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.empty());
}

TEST_CASE("DagEngine allTasksFinished - empty map", "[dag_engine]") {
    // Fix #240: 空 map 边界 —— 没有任务，视为全部完成
    std::map<std::string, std::string> statuses;
    REQUIRE(DagEngine::allTasksFinished(statuses));
}

TEST_CASE("DagEngine allTasksFinished - all terminal statuses", "[dag_engine]") {
    // Fix #240: 验证所有终态都被识别为完成
    std::map<std::string, std::string> statuses = {
        {"a", "SUCCESS"},
        {"b", "FAILED"},
        {"c", "UPSTREAM_FAILED"},
        {"d", "TIMEOUT"},
        {"e", "CANCELLED"},
        {"f", "NODE_OFFLINE"}
    };
    REQUIRE(DagEngine::allTasksFinished(statuses));
}

TEST_CASE("DagEngine allTasksFinished - PENDING is not finished", "[dag_engine]") {
    std::map<std::string, std::string> statuses = {{"a", "PENDING"}};
    REQUIRE_FALSE(DagEngine::allTasksFinished(statuses));
}

TEST_CASE("DagEngine allTasksFinished - DISPATCHED is not finished", "[dag_engine]") {
    std::map<std::string, std::string> statuses = {{"a", "DISPATCHED"}};
    REQUIRE_FALSE(DagEngine::allTasksFinished(statuses));
}

// ============================================================================
// Fix #257: topologicalSort 畸形输入测试
// 源码对节点缺 id、边缺 source/target 字段会抛 nlohmann::json 异常
// nodes/edges 存在但非数组应返回 failure
// ============================================================================
TEST_CASE("DagEngine topological sort - nodes field is not array", "[dag_engine]") {
    // Fix #257: nodes 字段存在但为字符串类型应返回 failure
    nlohmann::json dag;
    dag["nodes"] = "not_an_array";
    dag["edges"] = nlohmann::json::array();
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("nodes") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - edges field is not array", "[dag_engine]") {
    // Fix #257: edges 字段存在但为对象类型应被忽略（不抛异常），nodes 仍正常处理
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"] = "not_an_array";  // 非数组，is_array() 为 false，edges 被跳过
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
}

TEST_CASE("DagEngine topological sort - node id not string throws", "[dag_engine]") {
    // Fix #257: 节点 id 字段类型错误（非字符串），源码 node["id"].get<std::string>() 抛 type_error
    // Fix #279: 修复后 topologicalSort 使用 contains()+is_string() 校验，返回 failure 而非抛异常
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", 123}, {"task_id", "t1"}});  // id 是数字，非字符串
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("id") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - edge source not string throws", "[dag_engine]") {
    // Fix #257: 边 source 字段类型错误（非字符串），源码 edge["source"].get<std::string>() 抛 type_error
    // Fix #279: 修复后返回 failure 而非抛异常
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", 123}, {"target", "b"}});  // source 是数字
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("source") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - edge target not string throws", "[dag_engine]") {
    // Fix #257: 边 target 字段类型错误（非字符串），源码 edge["target"].get<std::string>() 抛 type_error
    // Fix #279: 修复后返回 failure 而非抛异常
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", 123}});  // target 是数字
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("target") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - self-loop edge detected as cycle", "[dag_engine]") {
    // Fix #257: 自环边 a->a 使 in_degree[a] 永不为 0，最终落入环检测分支
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "a"}});  // 自环
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("cycle") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - diamond DAG layers", "[dag_engine]") {
    // Fix #257: 菱形 DAG a->b, a->c, b->d, c->d 验证分层正确性
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

    auto result = DagEngine::topologicalSort(dag);
    REQUIRE(result.ok());
    auto levels = result.value();
    REQUIRE(levels.size() == 3);
    REQUIRE(levels[0].size() == 1);
    REQUIRE(levels[0][0] == "a");
    REQUIRE(levels[1].size() == 2);
    // 第二层应包含 b 和 c（顺序不确定，用集合验证）
    std::set<std::string> layer1(levels[1].begin(), levels[1].end());
    REQUIRE(layer1.count("b") == 1);
    REQUIRE(layer1.count("c") == 1);
    REQUIRE(levels[2].size() == 1);
    REQUIRE(levels[2][0] == "d");
}

// ============================================================================
// Fix #259: 失败用例补充 error() 消息断言
// ============================================================================
TEST_CASE("DagEngine topological sort - missing nodes error message", "[dag_engine]") {
    // Fix #259: 验证缺失 nodes 字段的错误消息
    nlohmann::json dag;
    dag["edges"] = nlohmann::json::array();
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("nodes") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - empty nodes error message", "[dag_engine]") {
    // Fix #259: 验证空 nodes 数组的错误消息
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("at least 1 node") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - duplicate node ID error message", "[dag_engine]") {
    // Fix #259: 验证重复节点 ID 的错误消息含 "Duplicate"
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t2"}});
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("Duplicate") != std::string::npos);
    REQUIRE(result.error().find("a") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - edge source not a node error message", "[dag_engine]") {
    // Fix #259: 验证边 source 不存在的错误消息
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "x"}, {"target", "a"}});
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("source") != std::string::npos);
    REQUIRE(result.error().find("x") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - edge target not a node error message", "[dag_engine]") {
    // Fix #259: 验证边 target 不存在的错误消息
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "y"}});
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("target") != std::string::npos);
    REQUIRE(result.error().find("y") != std::string::npos);
}

TEST_CASE("DagEngine topological sort - cycle error message", "[dag_engine]") {
    // Fix #259: 验证环检测的错误消息含 "cycle"
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "a"}});
    auto result = DagEngine::topologicalSort(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("cycle") != std::string::npos);
}

// ============================================================================
// Fix #260: findBlockedTasks 多上游、上游缺失、终态覆盖不全测试
// ============================================================================
TEST_CASE("DagEngine findBlockedTasks - UPSTREAM_FAILED upstream blocks", "[dag_engine]") {
    // Fix #260: UPSTREAM_FAILED 属于 kFailedStatuses，应阻塞下游
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    std::map<std::string, std::string> statuses = {{"a", "UPSTREAM_FAILED"}, {"b", "PENDING"}};
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.size() == 1);
    REQUIRE(blocked.count("b") == 1);
}

TEST_CASE("DagEngine findBlockedTasks - TIMEOUT upstream blocks", "[dag_engine]") {
    // Fix #260: TIMEOUT 属于 kFailedStatuses，应阻塞下游
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    std::map<std::string, std::string> statuses = {{"a", "TIMEOUT"}, {"b", "PENDING"}};
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.size() == 1);
    REQUIRE(blocked.count("b") == 1);
}

TEST_CASE("DagEngine findBlockedTasks - NODE_OFFLINE upstream blocks", "[dag_engine]") {
    // Fix #260: NODE_OFFLINE 属于 kFailedStatuses，应阻塞下游
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    std::map<std::string, std::string> statuses = {{"a", "NODE_OFFLINE"}, {"b", "PENDING"}};
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.size() == 1);
    REQUIRE(blocked.count("b") == 1);
}

TEST_CASE("DagEngine findBlockedTasks - missing nodes returns empty", "[dag_engine]") {
    // Fix #260: 缺失 nodes 字段返回空（与 findReadyTasks 对称）
    nlohmann::json dag;
    std::map<std::string, std::string> statuses = {{"a", "FAILED"}, {"b", "PENDING"}};
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.empty());
}

TEST_CASE("DagEngine findBlockedTasks - upstream not in statuses does not block", "[dag_engine]") {
    // Fix #260: 上游不在 statuses map 中时不阻塞（验证当前行为，与 findReadyTasks 相反）
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    // a 不在 statuses 中（上游缺失），b 不应被阻塞
    std::map<std::string, std::string> statuses = {{"b", "PENDING"}};
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.empty());
}

TEST_CASE("DagEngine findBlockedTasks - diamond with one failed upstream", "[dag_engine]") {
    // Fix #260: 多上游（菱形）场景：a->c, b->c，a 成功 b 失败，c 应被阻塞
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "c"}});

    std::map<std::string, std::string> statuses = {
        {"a", "SUCCESS"}, {"b", "FAILED"}, {"c", "PENDING"}
    };
    auto blocked = DagEngine::findBlockedTasks(dag, statuses);
    REQUIRE(blocked.size() == 1);
    REQUIRE(blocked.count("c") == 1);
}

TEST_CASE("DagEngine findReadyTasks - upstream not in statuses blocks", "[dag_engine]") {
    // Fix #260: findReadyTasks 上游不在 statuses map 中时视为未成功，阻塞下游
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    // a 不在 statuses 中，b 不应就绪
    std::map<std::string, std::string> statuses = {{"b", "PENDING"}};
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.empty());
}

TEST_CASE("DagEngine findReadyTasks - diamond with all upstream success", "[dag_engine]") {
    // Fix #260: 多上游（菱形）场景：a->c, b->c，a 和 b 都成功，c 应就绪
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "c"}});

    std::map<std::string, std::string> statuses = {
        {"a", "SUCCESS"}, {"b", "SUCCESS"}, {"c", "PENDING"}
    };
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.size() == 1);
    REQUIRE(ready.count("c") == 1);
}

TEST_CASE("DagEngine findReadyTasks - diamond with one upstream not success", "[dag_engine]") {
    // Fix #260: 多上游（菱形）场景：a->c, b->c，a 成功 b 失败，c 不应就绪
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "c"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "c"}});

    std::map<std::string, std::string> statuses = {
        {"a", "SUCCESS"}, {"b", "FAILED"}, {"c", "PENDING"}
    };
    auto ready = DagEngine::findReadyTasks(dag, statuses);
    REQUIRE(ready.empty());
}

TEST_CASE("DagEngine allTasksFinished - unknown status is not finished", "[dag_engine]") {
    // Fix #260: 未知/非法状态字符串应返回 false（非终态）
    std::map<std::string, std::string> statuses = {{"a", "UNKNOWN_STATUS"}};
    REQUIRE_FALSE(DagEngine::allTasksFinished(statuses));
}

TEST_CASE("DagEngine allTasksFinished - RUNNING is not finished", "[dag_engine]") {
    // Fix #260: RUNNING 是非终态
    std::map<std::string, std::string> statuses = {{"a", "RUNNING"}};
    REQUIRE_FALSE(DagEngine::allTasksFinished(statuses));
}

// ============================================================================
// Fix #279: DagEngine const json operator[] 访问缺失 id 字段为 UB
// ============================================================================

TEST_CASE("DagEngine topologicalSort - node missing id returns failure not crash", "[dag_engine_ub]") {
    // Fix #279: 节点缺失 id 字段应返回错误而非 UB/SIGABRT
    nlohmann::json dag = nlohmann::json::array({nlohmann::json::object({{"task_id", "t1"}})});
    nlohmann::json dag_json;
    dag_json["nodes"] = dag;
    auto result = DagEngine::topologicalSort(dag_json);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("id") != std::string::npos);
}

TEST_CASE("DagEngine topologicalSort - node id not string returns failure", "[dag_engine_ub]") {
    // Fix #279: id 为非字符串类型应返回错误
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({nlohmann::json::object({{"id", 123}})});
    auto result = DagEngine::topologicalSort(dag_json);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("id") != std::string::npos);
}

TEST_CASE("DagEngine topologicalSort - edge missing source returns failure", "[dag_engine_ub]") {
    // Fix #279: edge 缺失 source 字段应返回错误
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({
        nlohmann::json::object({{"id", "a"}}),
        nlohmann::json::object({{"id", "b"}})
    });
    dag_json["edges"] = nlohmann::json::array({
        nlohmann::json::object({{"target", "b"}})  // missing source
    });
    auto result = DagEngine::topologicalSort(dag_json);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("source") != std::string::npos);
}

TEST_CASE("DagEngine topologicalSort - edge source not string returns failure", "[dag_engine_ub]") {
    // Fix #279: edge source 为非字符串类型应返回错误
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({
        nlohmann::json::object({{"id", "a"}}),
        nlohmann::json::object({{"id", "b"}})
    });
    dag_json["edges"] = nlohmann::json::array({
        nlohmann::json::object({{"source", 123}, {"target", "b"}})
    });
    auto result = DagEngine::topologicalSort(dag_json);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagEngine findReadyTasks - node missing id returns empty not crash", "[dag_engine_ub]") {
    // Fix #279: findReadyTasks 节点缺失 id 应返回空集而非崩溃
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({nlohmann::json::object({{"task_id", "t1"}})});
    std::map<std::string, std::string> statuses;
    auto ready = DagEngine::findReadyTasks(dag_json, statuses);
    REQUIRE(ready.empty());
}

TEST_CASE("DagEngine findBlockedTasks - node missing id returns empty not crash", "[dag_engine_ub]") {
    // Fix #279: findBlockedTasks 节点缺失 id 应返回空集而非崩溃
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({nlohmann::json::object({{"task_id", "t1"}})});
    std::map<std::string, std::string> statuses;
    auto blocked = DagEngine::findBlockedTasks(dag_json, statuses);
    REQUIRE(blocked.empty());
}

TEST_CASE("DagEngine findReadyTasks - edge with non-string source is skipped", "[dag_engine_ub]") {
    // Fix #279: edge source 为非字符串类型应跳过而非崩溃
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({
        nlohmann::json::object({{"id", "a"}}),
        nlohmann::json::object({{"id", "b"}})
    });
    dag_json["edges"] = nlohmann::json::array({
        nlohmann::json::object({{"source", 123}, {"target", "b"}})
    });
    std::map<std::string, std::string> statuses = {{"a", "SUCCESS"}, {"b", "PENDING"}};
    auto ready = DagEngine::findReadyTasks(dag_json, statuses);
    // b's upstream (invalid edge) is skipped, so b is ready
    REQUIRE(ready.count("b") > 0);
}

// Fix #293: 边端点引用不存在的节点时应跳过，不污染 upstream map
TEST_CASE("DagEngine findReadyTasks - edge with nonexistent source skipped", "[dag_engine_ub]") {
    // Fix #293: edge source 引用不存在的节点 "ghost" 应跳过
    // 否则 "ghost" 会进入 upstream["b"]，导致 b 永远无法 ready（找不到 ghost 的状态）
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({
        nlohmann::json::object({{"id", "a"}}),
        nlohmann::json::object({{"id", "b"}})
    });
    dag_json["edges"] = nlohmann::json::array({
        nlohmann::json::object({{"source", "ghost"}, {"target", "b"}})
    });
    std::map<std::string, std::string> statuses = {{"a", "SUCCESS"}, {"b", "PENDING"}};
    auto ready = DagEngine::findReadyTasks(dag_json, statuses);
    // ghost 不在节点列表中，边被跳过，b 无上游，应 ready
    REQUIRE(ready.count("b") > 0);
}

// Fix #293: edge target 引用不存在的节点时应跳过
TEST_CASE("DagEngine findReadyTasks - edge with nonexistent target skipped", "[dag_engine_ub]") {
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({
        nlohmann::json::object({{"id", "a"}}),
        nlohmann::json::object({{"id", "b"}})
    });
    dag_json["edges"] = nlohmann::json::array({
        nlohmann::json::object({{"source", "a"}, {"target", "ghost"}})
    });
    std::map<std::string, std::string> statuses = {{"a", "SUCCESS"}, {"b", "PENDING"}};
    auto ready = DagEngine::findReadyTasks(dag_json, statuses);
    // ghost 不在节点列表中，边被跳过，b 无上游，应 ready
    REQUIRE(ready.count("b") > 0);
}

// Fix #293: findBlockedTasks 同样应跳过引用不存在节点的边
TEST_CASE("DagEngine findBlockedTasks - edge with nonexistent source skipped", "[dag_engine_ub]") {
    // Fix #293: edge source 引用不存在的节点 "ghost" 应跳过
    // 否则 "ghost" 会进入 upstream["b"]，但 ghost 不在 statuses 中，不会触发阻塞
    // 但更重要的是不应污染 upstream map（与 findReadyTasks 行为一致）
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({
        nlohmann::json::object({{"id", "a"}}),
        nlohmann::json::object({{"id", "b"}})
    });
    dag_json["edges"] = nlohmann::json::array({
        nlohmann::json::object({{"source", "ghost"}, {"target", "b"}})
    });
    // 即使 ghost 状态为 FAILED，也不应阻塞 b（因为 ghost 不是真实节点）
    std::map<std::string, std::string> statuses = {
        {"a", "SUCCESS"}, {"b", "PENDING"}, {"ghost", "FAILED"}
    };
    auto blocked = DagEngine::findBlockedTasks(dag_json, statuses);
    // ghost 不在节点列表中，边被跳过，b 不应被阻塞
    REQUIRE(blocked.count("b") == 0);
}

// Fix #293: findBlockedTasks edge target 引用不存在的节点应跳过
TEST_CASE("DagEngine findBlockedTasks - edge with nonexistent target skipped", "[dag_engine_ub]") {
    nlohmann::json dag_json;
    dag_json["nodes"] = nlohmann::json::array({
        nlohmann::json::object({{"id", "a"}}),
        nlohmann::json::object({{"id", "b"}})
    });
    dag_json["edges"] = nlohmann::json::array({
        nlohmann::json::object({{"source", "a"}, {"target", "ghost"}})
    });
    std::map<std::string, std::string> statuses = {
        {"a", "FAILED"}, {"b", "PENDING"}
    };
    auto blocked = DagEngine::findBlockedTasks(dag_json, statuses);
    // ghost 不在节点列表中，边被跳过；b 的上游 a 为 FAILED 但没有 a->b 的边
    // 所以 b 不应被阻塞
    REQUIRE(blocked.count("b") == 0);
}
