#include <catch2/catch_test_macros.hpp>
#include <string>
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
//   5. 移动语义正确
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
