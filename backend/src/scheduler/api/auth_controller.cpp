#include "scheduler/api/auth_controller.h"

#include <drogon/HttpResponse.h>
#include "scheduler/api/response_util.h"

namespace taskflow::scheduler::api {

namespace {

}  // namespace

AuthController::AuthController(std::shared_ptr<service::AuthService> auth_service)
    : auth_service_(std::move(auth_service)) {}

void AuthController::registerUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();

    if (username.empty() || password.empty()) {
        sendError(std::move(callback), 400, 40002, "Username and password are required");
        return;
    }

    // Public registration always creates an operator user; role is ignored to
    // prevent privilege escalation (completed-features.md section 2.1).
    auto result = auth_service_->registerUser(username, password, "");
    if (!result.ok()) {
        sendError(std::move(callback), 400, 40003, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void AuthController::login(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();

    if (username.empty() || password.empty()) {
        sendError(std::move(callback), 400, 40002, "Username and password are required");
        return;
    }

    auto result = auth_service_->login(username, password);
    if (!result.ok()) {
        sendError(std::move(callback), 401, 40103, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void AuthController::refreshToken(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string refresh_token = (*json)["refresh_token"].asString();

    if (refresh_token.empty()) {
        sendError(std::move(callback), 400, 40002, "Refresh token is required");
        return;
    }

    auto result = auth_service_->refreshToken(refresh_token);
    if (!result.ok()) {
        sendError(std::move(callback), 401, 40104, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void AuthController::logout(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    // Try to get access_token from Authorization header first
    std::string access_token;
    auto auth_header = req->getHeader("authorization");
    if (!auth_header.empty() && auth_header.find("Bearer ") == 0) {
        access_token = auth_header.substr(7);
    }

    // Fallback: try to get access_token from request body
    if (access_token.empty()) {
        auto json = req->getJsonObject();
        // Fix #311: Guard isMember with isObject to prevent unhandled
        // Json::LogicError exception under concurrent load.
        if (json && (*json).isObject() && (*json).isMember("access_token")) {
            access_token = (*json)["access_token"].asString();
        }
    }

    if (access_token.empty()) {
        sendError(std::move(callback), 400, 40002, "Access token is required (via Authorization header or request body)");
        return;
    }

    // Fix #330: body 中的 refresh_token（可选）一并吊销，登出终结整个会话
    std::string refresh_token;
    auto body = req->getJsonObject();
    if (body && (*body).isObject() && (*body).isMember("refresh_token")) {
        refresh_token = (*body)["refresh_token"].asString();
    }

    auto result = auth_service_->logout(access_token, refresh_token);
    if (!result.ok()) {
        sendError(std::move(callback), 500, 40105, result.error());
        return;
    }

    Json::Value resp;
    resp["code"] = 0;
    resp["message"] = "success";
    resp["data"] = Json::nullValue;
    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

// Fix #335: 校验 Authorization 头中的 access token，返回签发时声明的
// 身份信息（user_id/username/role）。前端在会话首次导航时调用以复核
// localStorage 中可能被篡改的 role。
void AuthController::me(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    std::string access_token;
    auto auth_header = req->getHeader("authorization");
    if (!auth_header.empty() && auth_header.find("Bearer ") == 0) {
        access_token = auth_header.substr(7);
    }
    if (access_token.empty()) {
        sendError(std::move(callback), 401, 40101, "Access token is required");
        return;
    }

    auto result = auth_service_->verifyAccessToken(access_token);
    if (!result.ok()) {
        sendError(std::move(callback), 401, 40101, result.error());
        return;
    }

    const auto& payload = result.value();
    Json::Value data;
    data["user_id"] = payload.user_id;
    data["username"] = payload.username;
    data["role"] = payload.role;
    Json::Value resp;
    resp["code"] = 0;
    resp["message"] = "success";
    resp["data"] = data;
    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

}  // namespace taskflow::scheduler::api
