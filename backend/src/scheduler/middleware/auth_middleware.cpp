#include "scheduler/middleware/auth_middleware.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include "common/util/sse_ticket_store.h"

namespace taskflow::scheduler::middleware {

AuthFilter::AuthFilter(const std::string& jwt_secret)
    : jwt_secret_(jwt_secret) {}

void AuthFilter::doFilter(
    const drogon::HttpRequestPtr& req,
    drogon::FilterCallback&& fcb,
    drogon::FilterChainCallback&& fccb) {

    const auto& path = req->path();

    // 非 /api/ 路径免认证（静态资源、前端页面等）
    if (path.size() < 5 || path.substr(0, 5) != "/api/") {
        fccb();
        return;
    }

    // register/login/refresh 免认证（completed-features.md 2.11）。
    // - register/login: 公开接口
    // - refresh: 使用 body 中的 refresh_token 换取新 access_token，调用时
    //   access_token 可能已过期，无法通过 Bearer 校验，故免认证。
    // logout 需经过认证过滤器以注入 user_id 等属性。
    if (path == "/api/v1/auth/register" ||
        path == "/api/v1/auth/login" ||
        path == "/api/v1/auth/refresh") {
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
    } else if (path.size() >= 12 && path.substr(path.size() - 12) == "/logs/stream") {
        // Fix #343: SSE 改用一次性 ticket，不再接受 URL 里的 access_token。
        // EventSource 不支持自定义头，但把长期有效的 access_token 放进 query
        // 会落进 nginx/代理访问日志与浏览器历史；ticket 短时（30s）、单次有效，
        // 且绑定签发时的实例与任务，泄露后可利用窗口极小。
        const auto& ticket = req->getParameter("ticket");
        if (!ticket.empty()) {
            auto consumed = common::util::SseTicketStore::instance().consume(ticket);
            if (consumed) {
                req->getAttributes()->insert("user_id", consumed->user_id);
                req->getAttributes()->insert("username", consumed->username);
                req->getAttributes()->insert("role", consumed->role);
                // 过滤器里读不到路径参数，把票据绑定的资源交给控制器核对
                req->getAttributes()->insert("sse_ticket_instance_id", consumed->instance_id);
                req->getAttributes()->insert("sse_ticket_task_instance_id", consumed->task_instance_id);
                fccb();
                return;
            }
            Json::Value resp;
            resp["code"] = 40101;
            resp["message"] = "票据无效或已使用";
            resp["data"] = Json::nullValue;
            auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
            httpResp->setStatusCode(drogon::k401Unauthorized);
            fcb(httpResp);
            return;
        }
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
