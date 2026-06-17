#include "scheduler/api/task_controller.h"

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

nlohmann::json jsoncppToNlohmann(const Json::Value& v) {
    Json::StreamWriterBuilder builder;
    std::string s = Json::writeString(builder, v);
    return nlohmann::json::parse(s);
}

}  // namespace

TaskController::TaskController(std::shared_ptr<service::TaskService> task_service)
    : task_service_(std::move(task_service)) {}

void TaskController::createTask(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string name = (*json)["name"].asString();
    std::string type = (*json)["type"].asString();
    std::string description = (*json).get("description", "").asString();
    int timeout = (*json).get("timeout", 3600).asInt();
    int max_retries = (*json).get("max_retries", 0).asInt();
    int retry_interval = (*json).get("retry_interval", 60).asInt();
    std::string creator_id = req->getAttributes()->get<std::string>("user_id");

    nlohmann::json config_json;
    if ((*json).isMember("config_json")) {
        config_json = jsoncppToNlohmann((*json)["config_json"]);
    }

    nlohmann::json resource_tags;
    if ((*json).isMember("resource_tags")) {
        resource_tags = jsoncppToNlohmann((*json)["resource_tags"]);
    }

    auto result = task_service_->createTask(
        name, type, config_json, description,
        timeout, max_retries, retry_interval,
        resource_tags, creator_id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40003, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value(), 201);
}

void TaskController::listTasks(
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

    std::string type_filter = std::string(req->getParameter("type"));
    std::string keyword = std::string(req->getParameter("keyword"));
    std::string creator_id = std::string(req->getParameter("creator_id"));

    auto result = task_service_->listTasks(
        page, page_size, type_filter, keyword, creator_id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 50001, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void TaskController::getTask(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    auto result = task_service_->getTask(id);

    if (!result.ok()) {
        sendError(std::move(callback), 404, 40401, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void TaskController::updateTask(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string name = (*json).isMember("name") ? (*json)["name"].asString() : "";
    std::string type = (*json).isMember("type") ? (*json)["type"].asString() : "";
    std::string description = (*json).get("description", "").asString();
    int timeout = (*json).get("timeout", 3600).asInt();
    int max_retries = (*json).get("max_retries", 0).asInt();
    int retry_interval = (*json).get("retry_interval", 60).asInt();
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

    nlohmann::json config_json;
    if ((*json).isMember("config_json")) {
        config_json = jsoncppToNlohmann((*json)["config_json"]);
    }

    nlohmann::json resource_tags;
    if ((*json).isMember("resource_tags")) {
        resource_tags = jsoncppToNlohmann((*json)["resource_tags"]);
    }

    auto result = task_service_->updateTask(
        id, name, type, config_json, description,
        timeout, max_retries, retry_interval,
        resource_tags, user_id, role);

    if (!result.ok()) {
        int status = 400;
        int code = 40003;
        if (result.error().find("权限不足") != std::string::npos) {
            status = 403;
            code = 40301;
        }
        sendError(std::move(callback), status, code, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void TaskController::deleteTask(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

    auto result = task_service_->deleteTask(id, user_id, role);

    if (!result.ok()) {
        int status = 404;
        int code = 40401;
        if (result.error().find("权限不足") != std::string::npos) {
            status = 403;
            code = 40301;
        }
        sendError(std::move(callback), status, code, result.error());
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
