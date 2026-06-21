#include "scheduler/middleware/role_middleware.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace taskflow::scheduler::middleware {

void RoleFilter::doFilter(
    const drogon::HttpRequestPtr& req,
    drogon::FilterCallback&& fcb,
    drogon::FilterChainCallback&& fccb) {

    auto attrs = req->getAttributes();
    if (!attrs->find("role")) {
        // Fix #299: 改为 fail-closed 设计 —— 若 role 属性不存在（AuthFilter 未执行），
        // 返回 403 Forbidden 而非放行。安全系统应默认拒绝。
        Json::Value resp;
        resp["code"] = 40301;
        resp["message"] = "权限不足：未认证";
        resp["data"] = Json::nullValue;
        auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
        httpResp->setStatusCode(drogon::k403Forbidden);
        fcb(httpResp);
        return;
    }

    const std::string& role = attrs->get<std::string>("role");
    const auto& path = req->path();
    auto method = req->method();

    // admin 拥有全部权限
    if (role == "admin") {
        fccb();
        return;
    }

    // 用户管理接口仅 admin 可访问
    if (path.find("/api/v1/users") != std::string::npos) {
        Json::Value resp;
        resp["code"] = 40301;
        resp["message"] = "权限不足";
        resp["data"] = Json::nullValue;
        auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
        httpResp->setStatusCode(drogon::k403Forbidden);
        fcb(httpResp);
        return;
    }

    // viewer 仅允许 GET 请求
    if (role == "viewer" && method != drogon::Get) {
        Json::Value resp;
        resp["code"] = 40301;
        resp["message"] = "权限不足，viewer 仅可查看";
        resp["data"] = Json::nullValue;
        auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
        httpResp->setStatusCode(drogon::k403Forbidden);
        fcb(httpResp);
        return;
    }

    // operator 允许读写操作（非 admin 专属接口）
    fccb();
}

}  // namespace taskflow::scheduler::middleware
