#include "scheduler/api/workflow_controller.h"

#include <cctype>
#include <optional>
#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

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

nlohmann::json jsoncppToNlohmann(const Json::Value& v) {
    Json::StreamWriterBuilder builder;
    std::string s = Json::writeString(builder, v);
    return nlohmann::json::parse(s);
}

}  // namespace

WorkflowController::WorkflowController(std::shared_ptr<service::WorkflowService> workflow_service)
    : workflow_service_(std::move(workflow_service)) {}

void WorkflowController::createWorkflow(
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
    std::string description = (*json).get("description", "").asString();
    std::string schedule_strategy = (*json)["schedule_strategy"].asString();
    if (schedule_strategy.empty()) {
        schedule_strategy = "random";
    }
    std::string target_worker_id = (*json).get("target_worker_id", "").asString();
    std::string cron_expression = (*json).get("cron_expression", "").asString();
    bool cron_enabled = (*json).isMember("cron_enabled") ? (*json)["cron_enabled"].asBool() : !cron_expression.empty();
    std::string creator_id = req->getAttributes()->get<std::string>("user_id");

    nlohmann::json dag_json;
    if ((*json).isMember("dag")) {
        dag_json = jsoncppToNlohmann((*json)["dag"]);
    } else if ((*json).isMember("dag_json")) {
        dag_json = jsoncppToNlohmann((*json)["dag_json"]);
    }

    auto result = workflow_service_->createWorkflow(
        name, description, dag_json, schedule_strategy,
        target_worker_id, cron_expression, cron_enabled, creator_id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40004, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void WorkflowController::listWorkflows(
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

    std::string keyword = std::string(req->getParameter("keyword"));

    // Resource-level permission: non-admin users can only see their own workflows
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    std::string creator_id;
    if (role != "admin") {
        creator_id = user_id;
    } else {
        // Admin can optionally filter by creator_id
        creator_id = std::string(req->getParameter("creator_id"));
    }

    auto result = workflow_service_->listWorkflows(page, page_size, keyword, creator_id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 50001, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void WorkflowController::getWorkflow(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = workflow_service_->getWorkflow(id, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        int code = 40004;
        if (result.error().find("权限不足") != std::string::npos ||
            result.error().find("Permission denied") != std::string::npos) {
            status = 403;
            code = 40301;
        } else if (result.error().find("不存在") != std::string::npos ||
                   result.error().find("已删除") != std::string::npos ||
                   result.error().find("not found") != std::string::npos) {
            status = 404;
            code = 40402;
        }
        sendError(std::move(callback), status, code, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void WorkflowController::updateWorkflow(
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
    std::string description = (*json).get("description", "").asString();
    std::string schedule_strategy = (*json).isMember("schedule_strategy") ? (*json)["schedule_strategy"].asString() : "";
    std::string target_worker_id = (*json).get("target_worker_id", "").asString();
    std::string cron_expression = (*json).get("cron_expression", "").asString();
    std::optional<bool> cron_enabled;
    if ((*json).isMember("cron_enabled")) {
        cron_enabled = (*json)["cron_enabled"].asBool();
    }
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

    nlohmann::json dag_json;
    if ((*json).isMember("dag")) {
        dag_json = jsoncppToNlohmann((*json)["dag"]);
    } else if ((*json).isMember("dag_json")) {
        dag_json = jsoncppToNlohmann((*json)["dag_json"]);
    }

    auto result = workflow_service_->updateWorkflow(
        id, name, description, dag_json, schedule_strategy,
        target_worker_id, cron_expression, cron_enabled, user_id, role);

    if (!result.ok()) {
        int status = 400;
        int code = 40004;
        if (result.error().find("权限不足") != std::string::npos) {
            status = 403;
            code = 40301;
        } else if (result.error().find("不存在") != std::string::npos ||
                   result.error().find("已删除") != std::string::npos) {
            status = 404;
            code = 40402;
        }
        sendError(std::move(callback), status, code, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void WorkflowController::deleteWorkflow(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

    auto result = workflow_service_->deleteWorkflow(id, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        int code = 40004;
        if (result.error().find("权限不足") != std::string::npos ||
            result.error().find("Permission denied") != std::string::npos) {
            status = 403;
            code = 40301;
        } else if (result.error().find("不存在") != std::string::npos ||
                   result.error().find("已删除") != std::string::npos ||
                   result.error().find("not found") != std::string::npos) {
            status = 404;
            code = 40402;
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

void WorkflowController::triggerWorkflow(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    // Fix #311: Wrap the entire handler in try-catch to prevent unhandled
    // Json::LogicError exceptions (from isMember on non-object Json::Value
    // under concurrent load) from causing HTTP 500 with empty body.
    try {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    std::string creator_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

    // Read param_overrides from request body
    nlohmann::json param_overrides = nlohmann::json::object();
    auto json = req->getJsonObject();
    // Fix #311: Guard isMember with isObject — under concurrent load,
    // getJsonObject() can return a non-objectValue Json::Value, and
    // isMember() throws Json::LogicError on non-object types, causing
    // an unhandled exception → HTTP 500.
    if (json && (*json).isObject() && (*json).isMember("param_overrides")) {
        param_overrides = jsoncppToNlohmann((*json)["param_overrides"]);
    }

    auto result = workflow_service_->triggerWorkflow(id, creator_id, role, param_overrides);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        int code = 40004;
        if (result.error().find("权限不足") != std::string::npos ||
            result.error().find("Permission denied") != std::string::npos) {
            status = 403;
            code = 40301;
        } else if (result.error().find("不存在") != std::string::npos ||
                   result.error().find("已删除") != std::string::npos ||
                   result.error().find("not found") != std::string::npos) {
            status = 404;
            code = 40402;
        }
        sendError(std::move(callback), status, code, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());

    } catch (const std::exception& e) {
        // Fix #311: Catch any Json::LogicError or other exceptions to prevent
        // HTTP 500 with empty body. Log the error for diagnosis.
        spdlog::error("triggerWorkflow exception: {}", e.what());
        sendError(std::move(callback), 500, 50001, "Internal server error");
    }
}

}  // namespace taskflow::scheduler::api
