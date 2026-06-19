#include <catch2/catch_test_macros.hpp>
#include <string>
#include <memory>
#include <utility>
#include <stdexcept>
#include "common/result/result.h"

using namespace taskflow::common::result;

// ============================================================================
// Result<T> 模板类单元测试
// 验收指标：
//   1. 成功构造返回 ok()=true, value() 正确
//   2. failure 构造返回 ok()=false, error() 正确
//   3. value() 在失败时抛出异常
//   4. operator bool 与 ok() 一致
//   5. 移动语义正确（Fix #245: 原测试声称覆盖移动语义但实际缺失）
// ============================================================================

TEST_CASE("Result<T>: success construction", "[result]") {
    Result<int> r(42);
    REQUIRE(r.ok());
    REQUIRE(r.value() == 42);
    REQUIRE(static_cast<bool>(r));
}

TEST_CASE("Result<T>: failure construction", "[result]") {
    auto r = Result<int>::failure("something went wrong");
    REQUIRE_FALSE(r.ok());
    REQUIRE_FALSE(r);
    REQUIRE(r.error() == "something went wrong");
}

TEST_CASE("Result<T>: value() throws on failure", "[result]") {
    auto r = Result<int>::failure("error");
    REQUIRE_THROWS_AS(r.value(), std::runtime_error);
}

TEST_CASE("Result<T>: success with string", "[result]") {
    Result<std::string> r("hello world");
    REQUIRE(r.ok());
    REQUIRE(r.value() == "hello world");
}

TEST_CASE("Result<T>: failure with moved string", "[result]") {
    std::string err = "moved error";
    auto r = Result<int>::failure(std::move(err));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error() == "moved error");
}

TEST_CASE("Result<T>: empty error message", "[result]") {
    auto r = Result<int>::failure("");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().empty());
}

TEST_CASE("Result<void>: default construction is success", "[result]") {
    Result<void> r;
    REQUIRE(r.ok());
    REQUIRE(static_cast<bool>(r));
}

TEST_CASE("Result<void>: failure construction", "[result]") {
    auto r = Result<void>::failure("void error");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error() == "void error");
}

TEST_CASE("Result<void>: failure with moved string", "[result]") {
    auto r = Result<void>::failure(std::string("moved void error"));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error() == "moved void error");
}

TEST_CASE("Result<T>: multiple failures have independent errors", "[result]") {
    auto r1 = Result<int>::failure("error1");
    auto r2 = Result<int>::failure("error2");
    REQUIRE(r1.error() == "error1");
    REQUIRE(r2.error() == "error2");
}

TEST_CASE("Result<T>: success with complex type", "[result]") {
    Result<std::vector<int>> r(std::vector<int>{1, 2, 3});
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 3);
    REQUIRE(r.value()[0] == 1);
    REQUIRE(r.value()[2] == 3);
}

TEST_CASE("Result<T>: success with json type", "[result]") {
    nlohmann::json j = {{"key", "value"}, {"num", 42}};
    Result<nlohmann::json> r(j);
    REQUIRE(r.ok());
    REQUIRE(r.value()["key"] == "value");
    REQUIRE(r.value()["num"] == 42);
}

// ============================================================================
// Fix #245: Result<T> 移动语义测试
// 原文件头注释声称 "移动语义正确" 但无任何移动语义测试。
// 以下测试覆盖：move-only 类型、move 构造、move value() 提取、move failure。
// ============================================================================

TEST_CASE("Result<T>: supports move-only types (unique_ptr)", "[result_move]") {
    // Fix #245: 若 Result<T> 不支持移动语义，则无法持有 move-only 类型
    auto up = std::make_unique<int>(99);
    Result<std::unique_ptr<int>> r(std::move(up));
    REQUIRE(r.ok());
    REQUIRE(*r.value() == 99);
}

TEST_CASE("Result<T>: move construction transfers value", "[result_move]") {
    // Fix #245: 移动构造后原对象处于有效但未指定状态，新对象持有原值
    Result<std::string> r1(std::string("movable_value"));
    REQUIRE(r1.ok());

    Result<std::string> r2(std::move(r1));
    REQUIRE(r2.ok());
    REQUIRE(r2.value() == "movable_value");
}

TEST_CASE("Result<T>: move construction of failure transfers error", "[result_move]") {
    // Fix #245: 移动构造 failure 结果
    auto r1 = Result<int>::failure("move_error_msg");
    REQUIRE_FALSE(r1.ok());

    auto r2 = std::move(r1);
    REQUIRE_FALSE(r2.ok());
    REQUIRE(r2.error() == "move_error_msg");
}

TEST_CASE("Result<T>: rvalue value() extracts by move", "[result_move]") {
    // Fix #245: std::move(r).value() 应通过 T&& 重载返回右值引用，
    // 允许调用方移动提取值（对 move-only 类型尤其重要）
    Result<std::unique_ptr<int>> r(std::make_unique<int>(123));
    REQUIRE(r.ok());

    std::unique_ptr<int> extracted = std::move(r).value();
    REQUIRE(extracted != nullptr);
    REQUIRE(*extracted == 123);
}

TEST_CASE("Result<T>: failure with moved string", "[result_move]") {
    // Fix #245: failure(std::string&&) 应移动错误消息而非拷贝
    std::string err = "moved_failure_error";

    auto r = Result<int>::failure(std::move(err));
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error() == "moved_failure_error");
    // 验证错误消息内容正确（移动后原 string 可能为空或未指定状态）
}

TEST_CASE("Result<T>: move-only type with vector", "[result_move]") {
    // Fix #245: 验证持有 move-only 容器（如 vector<unique_ptr>）的场景
    std::vector<std::unique_ptr<int>> vec;
    vec.push_back(std::make_unique<int>(1));
    vec.push_back(std::make_unique<int>(2));

    Result<std::vector<std::unique_ptr<int>>> r(std::move(vec));
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 2);
    REQUIRE(*r.value()[0] == 1);
    REQUIRE(*r.value()[1] == 2);
}

TEST_CASE("Result<void>: move construction of failure", "[result_move]") {
    // Fix #245: Result<void> 的移动构造
    auto r1 = Result<void>::failure("void_move_error");
    REQUIRE_FALSE(r1.ok());

    auto r2 = std::move(r1);
    REQUIRE_FALSE(r2.ok());
    REQUIRE(r2.error() == "void_move_error");
}

TEST_CASE("Result<T>: value() const& returns reference (no copy of moved type)", "[result_move]") {
    // Fix #245: 验证 const 左值 value() 返回引用，不触发移动
    Result<std::shared_ptr<int>> r(std::make_shared<int>(42));
    REQUIRE(r.ok());

    const auto& ref = r.value();
    REQUIRE(*ref == 42);
    // 原 Result 仍持有有效值
    REQUIRE(r.value().use_count() >= 1);
}
