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
