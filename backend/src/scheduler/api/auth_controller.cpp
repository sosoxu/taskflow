#include "scheduler/api/auth_controller.h"

#include <drogon/HttpResponse.h>

namespace taskflow::scheduler::api {

namespace {

Json::Value nlohmannToJsoncpp(const nlohmann::json& j) {
    Json::Reader reader;
    Json::Value output;
    reader.parse(j.dump(), output);
    return output;
}

void sendError(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               int statusCode, int code, const std::string& message) {
    Json::Value resp;
    resp["code"] = code;
    resp["message"] = message;
    resp["data"] = Json::nullValue;
    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
    httpResp->setStatusCode(static_cast<drogon::HttpStatusCode>(statusCode));
    callback(httpResp);
}

}  // namespace

AuthController::AuthController(std::shared_ptr<service::AuthService> auth_service)
    : auth_service_(std::move(auth_service)) {}

void AuthController::registerUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40000, "Request body must be JSON");
        return;
    }

    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();

    if (username.empty() || password.empty()) {
        sendError(std::move(callback), 400, 40000, "Username and password are required");
        return;
    }

    auto result = auth_service_->registerUser(username, password);
    if (!result.ok()) {
        sendError(std::move(callback), 400, 40000, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k201Created);
    callback(httpResp);
}

void AuthController::login(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40000, "Request body must be JSON");
        return;
    }

    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();

    if (username.empty() || password.empty()) {
        sendError(std::move(callback), 400, 40000, "Username and password are required");
        return;
    }

    auto result = auth_service_->login(username, password);
    if (!result.ok()) {
        sendError(std::move(callback), 401, 40100, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

void AuthController::refreshToken(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40000, "Request body must be JSON");
        return;
    }

    std::string refresh_token = (*json)["refresh_token"].asString();

    if (refresh_token.empty()) {
        sendError(std::move(callback), 400, 40000, "Refresh token is required");
        return;
    }

    auto result = auth_service_->refreshToken(refresh_token);
    if (!result.ok()) {
        sendError(std::move(callback), 401, 40100, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

}  // namespace taskflow::scheduler::api
