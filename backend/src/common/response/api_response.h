#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace taskflow::common::response {

class ApiResponse {
public:
    // 成功响应
    static nlohmann::json success(const nlohmann::json& data = nullptr) {
        return {
            {"code", 0},
            {"message", "success"},
            {"data", data.is_null() ? nlohmann::json::object() : data}
        };
    }

    // 成功响应带消息
    static nlohmann::json success(const std::string& message, const nlohmann::json& data = nullptr) {
        return {
            {"code", 0},
            {"message", message},
            {"data", data.is_null() ? nlohmann::json::object() : data}
        };
    }

    // 分页响应
    // Fix #283: 校验参数非负，page_size >= 1，防止非法分页响应和下游除零
    static nlohmann::json paged(const nlohmann::json& items, int total, int page, int page_size) {
        if (total < 0) total = 0;
        if (page < 0) page = 0;
        if (page_size < 1) page_size = 1;
        return success({
            {"items", items},
            {"total", total},
            {"page", page},
            {"page_size", page_size}
        });
    }

    // 错误响应
    // Fix #283: 校验 code > 0，防止与 success 的 code=0 冲突
    static nlohmann::json error(int code, const std::string& message) {
        if (code <= 0) code = 50000;
        return {
            {"code", code},
            {"message", message},
            {"data", nullptr}
        };
    }

    // 参数校验错误
    static nlohmann::json badRequest(const std::string& message) {
        return error(40000, message);
    }

    // 认证错误
    static nlohmann::json unauthorized(const std::string& message = "未认证") {
        return error(40100, message);
    }

    // 权限错误
    static nlohmann::json forbidden(const std::string& message = "权限不足") {
        return error(40300, message);
    }

    // 资源不存在
    static nlohmann::json notFound(const std::string& message = "资源不存在") {
        return error(40400, message);
    }

    // 内部错误
    static nlohmann::json internalError(const std::string& message = "内部错误") {
        return error(50000, message);
    }
};

}  // namespace taskflow::common::response
