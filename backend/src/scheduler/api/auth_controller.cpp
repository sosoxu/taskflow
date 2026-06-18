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

void sendSuccess(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const nlohmann::json& data, int statusCode = 200) {
    Json::Value resp;
    resp["code"] = 0;
    resp["message"] = "success";
    resp["data"] = nlohmannToJsoncpp(data);
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
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();
    std::string role = (*json).get("role", "").asString();

    if (username.empty() || password.empty()) {
        sendError(std::move(callback), 400, 40002, "Username and password are required");
        return;
    }

    auto result = auth_service_->registerUser(username, password, role);
    if (!result.ok()) {
        sendError(std::move(callback), 400, 40003, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value(), 201);
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
        if (json && (*json).isMember("access_token")) {
            access_token = (*json)["access_token"].asString();
        }
    }

    if (access_token.empty()) {
        sendError(std::move(callback), 400, 40002, "Access token is required (via Authorization header or request body)");
        return;
    }

    auto result = auth_service_->logout(access_token);
    if (!result.ok()) {
        sendError(std::move(callback), 400, 40101, result.error());
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

}  // namespace taskflow::scheduler::api
