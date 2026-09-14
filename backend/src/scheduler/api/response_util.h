#pragma once

// Fix #342: controller 层共享响应辅助（此前 sendError/sendSuccess/
// isValidUUID/nlohmannToJsoncpp 在 7 个 controller 中逐字重复）。

#include <cctype>
#include <functional>
#include <string>

#include <drogon/drogon.h>
#include <json/json.h>
#include <nlohmann/json.hpp>

namespace taskflow::scheduler::api {

inline bool isValidUUID(const std::string& id) {
    if (id.length() != 36) return false;
    for (size_t i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return false;
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(id[i]))) return false;
        }
    }
    return true;
}

inline Json::Value nlohmannToJsoncpp(const nlohmann::json& j) {
    Json::Reader reader;
    Json::Value output;
    reader.parse(j.dump(), output);
    return output;
}

inline void sendError(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      int statusCode, int code, const std::string& message) {
    Json::Value resp;
    resp["code"] = code;
    resp["message"] = message;
    resp["data"] = Json::nullValue;
    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
    httpResp->setStatusCode(static_cast<drogon::HttpStatusCode>(statusCode));
    callback(httpResp);
}

inline void sendSuccess(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const nlohmann::json& data, int statusCode = 200) {
    Json::Value resp;
    resp["code"] = 0;
    resp["message"] = "success";
    resp["data"] = nlohmannToJsoncpp(data);
    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
    httpResp->setStatusCode(static_cast<drogon::HttpStatusCode>(statusCode));
    callback(httpResp);
}

// Fix #342: 统一 service 层 Result 错误到 HTTP 状态码的映射
// （此前 instance_controller 内同一段匹配重复 10 处）。
inline int errorStatusOf(const std::string& error) {
    if (error.find("Permission denied") != std::string::npos) {
        return 403;
    }
    if (error.find("not found") != std::string::npos ||
        error.find("不存在") != std::string::npos) {
        return 404;
    }
    return 400;
}

}  // namespace taskflow::scheduler::api
