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

// ============================================================================
// Fix #258: DagValidator 边缺字段、自环、非数组类型等边界测试
// ============================================================================
TEST_CASE("DagValidator - edge missing source field", "[dag_validator]") {
    // Fix #258: 边缺少 source 字段应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"target", "b"}});  // 缺 source

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("source") != std::string::npos);
}

TEST_CASE("DagValidator - edge missing target field", "[dag_validator]") {
    // Fix #258: 边缺少 target 字段应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}});  // 缺 target

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("target") != std::string::npos);
}

TEST_CASE("DagValidator - edge source not string", "[dag_validator]") {
    // Fix #258: source 字段存在但非字符串类型应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", 123}, {"target", "b"}});  // source 为数字

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("source") != std::string::npos);
}

TEST_CASE("DagValidator - edge target not string", "[dag_validator]") {
    // Fix #258: target 字段存在但非字符串类型应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", 456}});  // target 为数字

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("target") != std::string::npos);
}

TEST_CASE("DagValidator - self-loop edge is cycle", "[dag_validator]") {
    // Fix #258: 自环边 a->a 应被判为环
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "a"}});  // 自环

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("cycle") != std::string::npos);
}

TEST_CASE("DagValidator - nodes field is not array", "[dag_validator]") {
    // Fix #258: nodes 字段存在但为非数组类型应失败
    nlohmann::json dag;
    dag["nodes"] = "not_an_array";
    dag["edges"] = nlohmann::json::array();

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("nodes") != std::string::npos);
}

TEST_CASE("DagValidator - node id not string", "[dag_validator]") {
    // Fix #258: id 字段存在但非字符串类型应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", 123}, {"task_id", "t1"}});  // id 为数字

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("id") != std::string::npos);
}

TEST_CASE("DagValidator - node task_id not string", "[dag_validator]") {
    // Fix #258: task_id 字段存在但非字符串类型应失败
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", 456}});  // task_id 为数字

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("task_id") != std::string::npos);
}

TEST_CASE("DagValidator - dependencies field is not array is tolerated", "[dag_validator]") {
    // Fix #258: dependencies 字段存在但非数组类型应被静默跳过（合法）
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}, {"dependencies", "not_an_array"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator - dependencies with non-string element is tolerated", "[dag_validator]") {
    // Fix #258: dependencies 数组含非字符串元素应被静默跳过（合法）
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}, {"dependencies", nlohmann::json::array({123, true})}});  // 非字符串元素
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator - edges and dependencies mixed cycle", "[dag_validator]") {
    // Fix #258: edges + dependencies 共同构成环应被检测
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    // edge: a -> b, dependencies: a 依赖 b（即 b -> a）形成环 a -> b -> a
    // dependencies 语义：node 的 dependencies 列表中的节点是该 node 的上游，
    // 即 dep -> node。所以 a 的 dependencies 含 b 意味着 b -> a。
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}, {"dependencies", nlohmann::json::array({"b"})}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("cycle") != std::string::npos);
}

// ============================================================================
// Fix #259: 失败用例补充 error() 消息断言
// ============================================================================
TEST_CASE("DagValidator - cycle error message", "[dag_validator]") {
    // Fix #259: 验证环检测的错误消息含 "cycle"
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});
    dag["edges"].push_back({{"source", "b"}, {"target", "a"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("cycle") != std::string::npos);
}

TEST_CASE("DagValidator - missing task_id error message", "[dag_validator]") {
    // Fix #259: 验证缺少 task_id 的错误消息含 "task_id"
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "b"}});  // missing task_id
    dag["edges"].push_back({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("task_id") != std::string::npos);
}

TEST_CASE("DagValidator - orphan node error message", "[dag_validator]") {
    // Fix #259: 验证孤立节点的错误消息含 "isolated" 和节点 ID
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
    REQUIRE(result.error().find("isolated") != std::string::npos);
    REQUIRE(result.error().find("c") != std::string::npos);
}

TEST_CASE("DagValidator - empty nodes error message", "[dag_validator]") {
    // Fix #259: 验证空 nodes 数组的错误消息
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("at least 1 node") != std::string::npos);
}

TEST_CASE("DagValidator - duplicate node ID error message", "[dag_validator]") {
    // Fix #259: 验证重复节点 ID 的错误消息含 "Duplicate"
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t2"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("Duplicate") != std::string::npos);
    REQUIRE(result.error().find("a") != std::string::npos);
}

TEST_CASE("DagValidator - edge source not a node error message", "[dag_validator]") {
    // Fix #259: 验证边 source 不存在的错误消息
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "x"}, {"target", "a"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("source") != std::string::npos);
    REQUIRE(result.error().find("x") != std::string::npos);
}

TEST_CASE("DagValidator - edge target not a node error message", "[dag_validator]") {
    // Fix #259: 验证边 target 不存在的错误消息
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}});
    dag["edges"].push_back({{"source", "a"}, {"target", "y"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("target") != std::string::npos);
    REQUIRE(result.error().find("y") != std::string::npos);
}

TEST_CASE("DagValidator - missing nodes array error message", "[dag_validator]") {
    // Fix #259: 验证缺少 nodes 数组的错误消息
    nlohmann::json dag;
    dag["edges"] = nlohmann::json::array();

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("nodes") != std::string::npos);
}

TEST_CASE("DagValidator - node missing id field error message", "[dag_validator]") {
    // Fix #259: 验证节点缺少 id 字段的错误消息含 "id"
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"task_id", "t1"}});  // 缺 id

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("id") != std::string::npos);
}

TEST_CASE("DagValidator - dependencies cycle error message", "[dag_validator]") {
    // Fix #259: 验证 dependencies 形成环的错误消息含 "cycle"
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}, {"dependencies", nlohmann::json::array({"b"})}});
    dag["nodes"].push_back({{"id", "b"}, {"task_id", "t2"}, {"dependencies", nlohmann::json::array({"a"})}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("cycle") != std::string::npos);
}

TEST_CASE("DagValidator - dependencies references non-existent error message", "[dag_validator]") {
    // Fix #259: 验证 dependencies 引用不存在节点的错误消息
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    dag["nodes"].push_back({{"id", "a"}, {"task_id", "t1"}, {"dependencies", nlohmann::json::array({"x"})}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("x") != std::string::npos);
}

// ============================================================================
// Fix #285: DagValidator hasCycle 递归栈溢出 + edges 非数组静默忽略
// ============================================================================

TEST_CASE("DagValidator - edges not array returns error", "[dag_validator_edges]") {
    // Fix #285: edges 存在但非数组应返回错误，而非静默忽略
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array({{{"id", "a"}, {"task_id", "t1"}}});
    dag["edges"] = "not-an-array";  // 非数组

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("edges") != std::string::npos);
}

TEST_CASE("DagValidator - edges as object returns error", "[dag_validator_edges]") {
    // Fix #285: edges 为对象应返回错误
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array({{{"id", "a"}, {"task_id", "t1"}}});
    dag["edges"] = nlohmann::json::object({{"source", "a"}, {"target", "b"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("DagValidator - deep chain does not stack overflow", "[dag_validator_deep]") {
    // Fix #285: 深链 DAG 不应栈溢出（迭代式 DFS）
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    const int depth = 5000;
    for (int i = 0; i < depth; ++i) {
        dag["nodes"].push_back({{"id", "n" + std::to_string(i)}, {"task_id", "t" + std::to_string(i)}});
    }
    for (int i = 0; i < depth - 1; ++i) {
        dag["edges"].push_back({{"source", "n" + std::to_string(i)}, {"target", "n" + std::to_string(i + 1)}});
    }

    // Should not crash (stack overflow) and should validate as a valid DAG
    auto result = DagValidator::validate(dag);
    REQUIRE(result.ok());
}

TEST_CASE("DagValidator - deep chain with cycle is detected", "[dag_validator_deep]") {
    // Fix #285: 深链 + 环应被正确检测（迭代式 DFS）
    nlohmann::json dag;
    dag["nodes"] = nlohmann::json::array();
    dag["edges"] = nlohmann::json::array();
    const int depth = 1000;
    for (int i = 0; i < depth; ++i) {
        dag["nodes"].push_back({{"id", "n" + std::to_string(i)}, {"task_id", "t" + std::to_string(i)}});
    }
    for (int i = 0; i < depth - 1; ++i) {
        dag["edges"].push_back({{"source", "n" + std::to_string(i)}, {"target", "n" + std::to_string(i + 1)}});
    }
    // Add cycle: last -> first
    dag["edges"].push_back({{"source", "n" + std::to_string(depth - 1)}, {"target", "n0"}});

    auto result = DagValidator::validate(dag);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("cycle") != std::string::npos);
}
