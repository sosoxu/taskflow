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
