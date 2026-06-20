#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "common/response/api_response.h"

using namespace taskflow::common::response;

// ============================================================================
// Fix #267: ApiResponse 完全未测试（9 个公开方法零覆盖）
// 验收指标：
//   1. success(data) 返回 code=0, message="success", data 转换
//   2. success(message, data) 返回 code=0, 自定义 message
//   3. paged 返回分页结构
//   4. error(code, message) 返回指定 code
//   5. badRequest/unauthorized/forbidden/notFound/internalError 返回正确 code 和默认消息
//   6. data 为 null 时转换为空对象 {}
// ============================================================================

TEST_CASE("ApiResponse: success with default data returns code 0", "[api_response]") {
    auto resp = ApiResponse::success();
    REQUIRE(resp["code"] == 0);
    REQUIRE(resp["message"] == "success");
    // data 为 null 时应转换为空对象
    REQUIRE(resp["data"].is_object());
    REQUIRE(resp["data"].empty());
}

TEST_CASE("ApiResponse: success with data object", "[api_response]") {
    nlohmann::json data = {{"key", "value"}, {"count", 42}};
    auto resp = ApiResponse::success(data);
    REQUIRE(resp["code"] == 0);
    REQUIRE(resp["message"] == "success");
    REQUIRE(resp["data"]["key"] == "value");
    REQUIRE(resp["data"]["count"] == 42);
}

TEST_CASE("ApiResponse: success with data array", "[api_response]") {
    nlohmann::json data = nlohmann::json::array({"a", "b", "c"});
    auto resp = ApiResponse::success(data);
    REQUIRE(resp["code"] == 0);
    REQUIRE(resp["data"].is_array());
    REQUIRE(resp["data"].size() == 3);
    REQUIRE(resp["data"][0] == "a");
}

TEST_CASE("ApiResponse: success with custom message and data", "[api_response]") {
    nlohmann::json data = {{"id", 1}};
    auto resp = ApiResponse::success("created", data);
    REQUIRE(resp["code"] == 0);
    REQUIRE(resp["message"] == "created");
    REQUIRE(resp["data"]["id"] == 1);
}

TEST_CASE("ApiResponse: success with custom message and null data converts to empty object", "[api_response]") {
    // Fix #267: 显式构造 std::string 避免与 json 重载产生歧义
    auto resp = ApiResponse::success(std::string("no content"));
    REQUIRE(resp["code"] == 0);
    REQUIRE(resp["message"] == "no content");
    // data 默认为 null，应转换为空对象
    REQUIRE(resp["data"].is_object());
    REQUIRE(resp["data"].empty());
}

TEST_CASE("ApiResponse: paged returns correct structure", "[api_response]") {
    nlohmann::json items = nlohmann::json::array({{"id", 1}, {"id", 2}});
    auto resp = ApiResponse::paged(items, 100, 1, 10);
    REQUIRE(resp["code"] == 0);
    REQUIRE(resp["data"]["items"].is_array());
    REQUIRE(resp["data"]["items"].size() == 2);
    REQUIRE(resp["data"]["total"] == 100);
    REQUIRE(resp["data"]["page"] == 1);
    REQUIRE(resp["data"]["page_size"] == 10);
}

TEST_CASE("ApiResponse: paged with empty items", "[api_response]") {
    nlohmann::json items = nlohmann::json::array();
    auto resp = ApiResponse::paged(items, 0, 1, 10);
    REQUIRE(resp["code"] == 0);
    REQUIRE(resp["data"]["items"].empty());
    REQUIRE(resp["data"]["total"] == 0);
}

TEST_CASE("ApiResponse: error returns specified code and message", "[api_response]") {
    auto resp = ApiResponse::error(40900, "conflict");
    REQUIRE(resp["code"] == 40900);
    REQUIRE(resp["message"] == "conflict");
    // error 响应的 data 为 null（不转换）
    REQUIRE(resp["data"].is_null());
}

TEST_CASE("ApiResponse: badRequest returns code 40000", "[api_response]") {
    auto resp = ApiResponse::badRequest("invalid input");
    REQUIRE(resp["code"] == 40000);
    REQUIRE(resp["message"] == "invalid input");
}

TEST_CASE("ApiResponse: unauthorized returns code 40100 with default message", "[api_response]") {
    auto resp = ApiResponse::unauthorized();
    REQUIRE(resp["code"] == 40100);
    REQUIRE(resp["message"] == "未认证");
}

TEST_CASE("ApiResponse: unauthorized with custom message", "[api_response]") {
    auto resp = ApiResponse::unauthorized("token expired");
    REQUIRE(resp["code"] == 40100);
    REQUIRE(resp["message"] == "token expired");
}

TEST_CASE("ApiResponse: forbidden returns code 40300 with default message", "[api_response]") {
    auto resp = ApiResponse::forbidden();
    REQUIRE(resp["code"] == 40300);
    REQUIRE(resp["message"] == "权限不足");
}

TEST_CASE("ApiResponse: forbidden with custom message", "[api_response]") {
    auto resp = ApiResponse::forbidden("admin only");
    REQUIRE(resp["code"] == 40300);
    REQUIRE(resp["message"] == "admin only");
}

TEST_CASE("ApiResponse: notFound returns code 40400 with default message", "[api_response]") {
    auto resp = ApiResponse::notFound();
    REQUIRE(resp["code"] == 40400);
    REQUIRE(resp["message"] == "资源不存在");
}

TEST_CASE("ApiResponse: notFound with custom message", "[api_response]") {
    auto resp = ApiResponse::notFound("workflow not found");
    REQUIRE(resp["code"] == 40400);
    REQUIRE(resp["message"] == "workflow not found");
}

TEST_CASE("ApiResponse: internalError returns code 50000 with default message", "[api_response]") {
    auto resp = ApiResponse::internalError();
    REQUIRE(resp["code"] == 50000);
    REQUIRE(resp["message"] == "内部错误");
}

TEST_CASE("ApiResponse: internalError with custom message", "[api_response]") {
    auto resp = ApiResponse::internalError("database connection lost");
    REQUIRE(resp["code"] == 50000);
    REQUIRE(resp["message"] == "database connection lost");
}

TEST_CASE("ApiResponse: all responses have exactly 3 fields", "[api_response]") {
    // 验证所有响应都恰好有 code/message/data 三个字段
    REQUIRE(ApiResponse::success().size() == 3);
    // Fix #267: 显式构造 std::string 避免与 json 重载产生歧义
    REQUIRE(ApiResponse::success(std::string("msg")).size() == 3);
    REQUIRE(ApiResponse::paged(nlohmann::json::array(), 0, 1, 10).size() == 3);
    REQUIRE(ApiResponse::error(400, "err").size() == 3);
    REQUIRE(ApiResponse::badRequest("err").size() == 3);
    REQUIRE(ApiResponse::unauthorized().size() == 3);
    REQUIRE(ApiResponse::forbidden().size() == 3);
    REQUIRE(ApiResponse::notFound().size() == 3);
    REQUIRE(ApiResponse::internalError().size() == 3);
}
