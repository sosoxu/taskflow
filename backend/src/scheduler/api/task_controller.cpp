#include "scheduler/api/task_controller.h"

#include <cctype>
#include <drogon/HttpResponse.h>

namespace taskflow::scheduler::api {

namespace {

static bool isValidUUID(const std::string& id) {
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
    // Fix #311: Ensure the parsed JSON is an object before calling isMember.
    if (!(*json).isObject()) {
        sendError(std::move(callback), 400, 40001, "Request body must be a JSON object");
        return;
    }

    std::string name = (*json)["name"].asString();
    std::string type = (*json)["type"].asString();
    std::string description = (*json).get("description", "").asString();
    int timeout = (*json).get("timeout", 3600).asInt();
    int max_retries = (*json).get("max_retries", 0).asInt();
    int retry_interval = (*json).get("retry_interval", 60).asInt();
    std::string creator_id = req->getAttributes()->get<std::string>("user_id");

    // Fix #220: Validate numeric inputs. Without this, timeout=0 or negative
    // makes tasks never time out, max_retries<0 disables retries, and extreme
    // values can overflow DB integer columns.
    if (timeout <= 0 || timeout > 86400) {
        sendError(std::move(callback), 400, 40002,
                  "timeout must be between 1 and 86400 seconds");
        return;
    }
    if (max_retries < 0 || max_retries > 100) {
        sendError(std::move(callback), 400, 40002,
                  "max_retries must be between 0 and 100");
        return;
    }
    if (retry_interval < 0 || retry_interval > 3600) {
        sendError(std::move(callback), 400, 40002,
                  "retry_interval must be between 0 and 3600 seconds");
        return;
    }

    nlohmann::json config_json;
    if ((*json).isMember("config")) {
        config_json = jsoncppToNlohmann((*json)["config"]);
    } else if ((*json).isMember("config_json")) {
        config_json = jsoncppToNlohmann((*json)["config_json"]);
    }

    nlohmann::json resource_tags;
    if ((*json).isMember("resource_tags")) {
        resource_tags = jsoncppToNlohmann((*json)["resource_tags"]);
    }

    nlohmann::json parameters_json;
    if ((*json).isMember("parameters")) {
        parameters_json = jsoncppToNlohmann((*json)["parameters"]);
    } else if ((*json).isMember("parameters_json")) {
        parameters_json = jsoncppToNlohmann((*json)["parameters_json"]);
    }

    auto result = task_service_->createTask(
        name, type, config_json, description,
        timeout, max_retries, retry_interval,
        resource_tags, parameters_json, creator_id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40003, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
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

    // Resource-level permission: non-admin users can only see their own tasks
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    std::string creator_id;
    if (role != "admin") {
        creator_id = user_id;
    } else {
        // Admin can optionally filter by creator_id
        creator_id = std::string(req->getParameter("creator_id"));
    }

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

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

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

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }
    // Fix #311: Ensure the parsed JSON is an object before calling isMember.
    if (!(*json).isObject()) {
        sendError(std::move(callback), 400, 40001, "Request body must be a JSON object");
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

    // Fix #220: Same numeric validation as createTask
    if (timeout <= 0 || timeout > 86400) {
        sendError(std::move(callback), 400, 40002,
                  "timeout must be between 1 and 86400 seconds");
        return;
    }
    if (max_retries < 0 || max_retries > 100) {
        sendError(std::move(callback), 400, 40002,
                  "max_retries must be between 0 and 100");
        return;
    }
    if (retry_interval < 0 || retry_interval > 3600) {
        sendError(std::move(callback), 400, 40002,
                  "retry_interval must be between 0 and 3600 seconds");
        return;
    }

    nlohmann::json config_json;
    if ((*json).isMember("config")) {
        config_json = jsoncppToNlohmann((*json)["config"]);
    } else if ((*json).isMember("config_json")) {
        config_json = jsoncppToNlohmann((*json)["config_json"]);
    }

    nlohmann::json resource_tags;
    if ((*json).isMember("resource_tags")) {
        resource_tags = jsoncppToNlohmann((*json)["resource_tags"]);
    }

    nlohmann::json parameters_json;
    if ((*json).isMember("parameters")) {
        parameters_json = jsoncppToNlohmann((*json)["parameters"]);
    } else if ((*json).isMember("parameters_json")) {
        parameters_json = jsoncppToNlohmann((*json)["parameters_json"]);
    }

    auto result = task_service_->updateTask(
        id, name, type, config_json, description,
        timeout, max_retries, retry_interval,
        resource_tags, parameters_json, user_id, role);

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

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

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
