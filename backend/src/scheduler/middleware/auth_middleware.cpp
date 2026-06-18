#include "scheduler/middleware/auth_middleware.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace taskflow::scheduler::middleware {

AuthFilter::AuthFilter(const std::string& jwt_secret)
    : jwt_secret_(jwt_secret) {}

void AuthFilter::doFilter(
    const drogon::HttpRequestPtr& req,
    drogon::FilterCallback&& fcb,
    drogon::FilterChainCallback&& fccb) {

    const auto& path = req->path();

    // 认证接口免认证
    if (path.find("/api/v1/auth/") == 0) {
        fccb();
        return;
    }

    // 健康检查免认证
    if (path == "/api/v1/health") {
        fccb();
        return;
    }

    // 提取 Authorization 头
    auto auth_header = req->getHeader("authorization");
    std::string token;

    if (!auth_header.empty() && auth_header.find("Bearer ") == 0) {
        token = auth_header.substr(7);  // "Bearer " 长度为 7
    } else {
        // SSE (EventSource) doesn't support custom headers, allow token via query parameter
        token = std::string(req->getParameter("token"));
    }

    if (token.empty()) {
        Json::Value resp;
        resp["code"] = 40101;
        resp["message"] = "未认证";
        resp["data"] = Json::nullValue;
        auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
        httpResp->setStatusCode(drogon::k401Unauthorized);
        fcb(httpResp);
        return;
    }

    auto result = common::util::JwtUtil::verifyToken(token, jwt_secret_);
    if (!result.ok()) {
        Json::Value resp;
        resp["code"] = 40101;
        resp["message"] = "Token 无效或已过期";
        resp["data"] = Json::nullValue;
        auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
        httpResp->setStatusCode(drogon::k401Unauthorized);
        fcb(httpResp);
        return;
    }

    const auto& payload = result.value();

    // 检查是否为 access token
    if (payload.type != "access") {
        Json::Value resp;
        resp["code"] = 40007;
        resp["message"] = "需要 access token";
        resp["data"] = Json::nullValue;
        auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
        httpResp->setStatusCode(drogon::k401Unauthorized);
        fcb(httpResp);
        return;
    }

    // 检查 token 是否已被加入黑名单（已登出）
    if (!payload.jti.empty() && common::util::TokenBlacklist::instance().isBlacklisted(payload.jti)) {
        Json::Value resp;
        resp["code"] = 40102;
        resp["message"] = "Token 已被撤销";
        resp["data"] = Json::nullValue;
        auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
        httpResp->setStatusCode(drogon::k401Unauthorized);
        fcb(httpResp);
        return;
    }

    // 将用户信息注入请求属性
    req->getAttributes()->insert("user_id", payload.user_id);
    req->getAttributes()->insert("username", payload.username);
    req->getAttributes()->insert("role", payload.role);
    req->getAttributes()->insert("jti", payload.jti);

    fccb();
}

}  // namespace taskflow::scheduler::middleware
