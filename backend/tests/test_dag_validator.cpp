#include <catch2/catch_test_macros.hpp>
#include "scheduler/service/dag_validator.h"

using namespace taskflow::scheduler::service;

TEST_CASE("DagValidator - valid DAG", "[dag_validator]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator - cycle detected", "[dag_validator]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "a"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - missing task_id field", "[dag_validator]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}});  // missing task_id
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - orphan node", "[dag_validator]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    // 'c' is orphan

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - empty nodes", "[dag_validator]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - single node no edges", "[dag_validator]") {
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}
