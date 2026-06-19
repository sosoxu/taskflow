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

// ============================================================================
// Fix #241: DagValidator 补充测试
// 原测试只覆盖 edges 方式声明依赖，缺少 dependencies 字段、纯并行 DAG、
// 重复边、dependencies 引用不存在节点、dependencies 形成环等场景
// ============================================================================

TEST_CASE("DagValidator - dependencies field declares edges", "[dag_validator]") {
    // Fix #241: 节点内 dependencies 数组是 edges 之外的另一种依赖声明方式
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}, {"dependencies", nlohmann::json::array({"a"})}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator - dependencies field with cycle", "[dag_validator]") {
    // Fix #241: dependencies 声明的依赖形成环应被检测
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}, {"dependencies", nlohmann::json::array({"b"})}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}, {"dependencies", nlohmann::json::array({"a"})}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - dependencies references non-existent node", "[dag_validator]") {
    // Fix #241: dependencies 引用不存在的节点 ID 应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}, {"dependencies", nlohmann::json::array({"x"})}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - purely parallel DAG is valid", "[dag_validator]") {
    // Fix #241: 多节点无边（纯并行 DAG）合法 —— connected_nodes 为空时跳过孤立检查
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator - duplicate edges are tolerated", "[dag_validator]") {
    // Fix #241: 重复边（相同 source/target 出现多次）不应导致校验失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});  // 重复

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator - duplicate node ID", "[dag_validator]") {
    // Fix #241: 重复节点 ID 应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t2"}});  // 重复 ID

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - edge source not a node", "[dag_validator]") {
    // Fix #241: 边的 source 引用不存在的节点应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "x"}, {"target", "a"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - edge target not a node", "[dag_validator]") {
    // Fix #241: 边的 target 引用不存在的节点应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "y"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - missing nodes array", "[dag_validator]") {
    // Fix #241: 缺少 nodes 数组应失败
    nlohmann::json dag;
    dag["edges"] = nlohmann::json::array();

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - node missing id field", "[dag_validator]") {
    // Fix #241: 节点缺少 id 字段应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"task_id", "t1"}});  // 缺 id

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - mixed edges and dependencies", "[dag_validator]") {
    // Fix #241: 同时使用 edges 和 dependencies 两种方式声明依赖
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["nodes"].push_back({{"id", "c"}, {"task_id", "t3"}, {"dependencies", nlohmann::json::array({"b"})}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator - diamond DAG is valid", "[dag_validator]") {
    // Fix #241: 菱形 DAG: a -> b, a -> c, b -> d, c -> d
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

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}
