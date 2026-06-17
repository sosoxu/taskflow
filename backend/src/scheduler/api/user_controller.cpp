#include "scheduler/api/user_controller.h"

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

UserController::UserController(std::shared_ptr<service::UserService> user_service)
    : user_service_(std::move(user_service)) {}

void UserController::listUsers(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    int page = 1;
    int page_size = 10;

    std::string page_str = std::string(req->getParameter("page"));
    std::string page_size_str = std::string(req->getParameter("page_size"));

    if (!page_str.empty()) {
        try { page = std::stoi(page_str); } catch (...) {}
    }
    if (!page_size_str.empty()) {
        try { page_size = std::stoi(page_size_str); } catch (...) {}
    }

    auto result = user_service_->listUsers(page, page_size);
    if (!result.ok()) {
        sendError(std::move(callback), 400, 50001, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

void UserController::createUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();
    std::string role = (*json)["role"].asString();

    if (username.empty() || password.empty() || role.empty()) {
        sendError(std::move(callback), 400, 40002, "Username, password and role are required");
        return;
    }

    auto result = user_service_->createUser(username, password, role);
    if (!result.ok()) {
        sendError(std::move(callback), 400, 40003, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k201Created);
    callback(httpResp);
}

void UserController::updateUserRole(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string role = (*json)["role"].asString();

    if (role.empty()) {
        sendError(std::move(callback), 400, 40002, "Role is required");
        return;
    }

    auto result = user_service_->updateUserRole(id, role);
    if (!result.ok()) {
        sendError(std::move(callback), 400, 40405, result.error());
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

void UserController::deleteUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    std::string current_user_id = req->getAttributes()->get<std::string>("user_id");

    auto result = user_service_->deleteUser(id, current_user_id);
    if (!result.ok()) {
        sendError(std::move(callback), 400, 40405, result.error());
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
