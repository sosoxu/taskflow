#include "scheduler/api/workflow_controller.h"

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

    std::string name = (*json)["name"].asString();
    std::string description = (*json).get("description", "").asString();
    std::string schedule_strategy = (*json)["schedule_strategy"].asString();
    std::string target_worker_id = (*json).get("target_worker_id", "").asString();
    std::string cron_expression = (*json).get("cron_expression", "").asString();
    bool cron_enabled = (*json).get("cron_enabled", false).asBool();
    std::string creator_id = req->getAttributes()->get<std::string>("user_id");

    nlohmann::json dag_json;
    if ((*json).isMember("dag_json")) {
        dag_json = jsoncppToNlohmann((*json)["dag_json"]);
    }

    auto result = workflow_service_->createWorkflow(
        name, description, dag_json, schedule_strategy,
        target_worker_id, cron_expression, cron_enabled, creator_id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40004, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k201Created);
    callback(httpResp);
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
    std::string creator_id = std::string(req->getParameter("creator_id"));

    auto result = workflow_service_->listWorkflows(page, page_size, keyword, creator_id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 50001, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

void WorkflowController::getWorkflow(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    auto result = workflow_service_->getWorkflow(id);

    if (!result.ok()) {
        sendError(std::move(callback), 404, 40402, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

void WorkflowController::updateWorkflow(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    auto json = req->getJsonObject();
    if (!json) {
        sendError(std::move(callback), 400, 40001, "Request body must be JSON");
        return;
    }

    std::string name = (*json).isMember("name") ? (*json)["name"].asString() : "";
    std::string description = (*json).get("description", "").asString();
    std::string schedule_strategy = (*json).isMember("schedule_strategy") ? (*json)["schedule_strategy"].asString() : "";
    std::string target_worker_id = (*json).get("target_worker_id", "").asString();
    std::string cron_expression = (*json).get("cron_expression", "").asString();
    bool cron_enabled = (*json).get("cron_enabled", false).asBool();
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

    nlohmann::json dag_json;
    if ((*json).isMember("dag_json")) {
        dag_json = jsoncppToNlohmann((*json)["dag_json"]);
    }

    auto result = workflow_service_->updateWorkflow(
        id, name, description, dag_json, schedule_strategy,
        target_worker_id, cron_expression, cron_enabled, user_id, role);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40004, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

void WorkflowController::deleteWorkflow(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

    auto result = workflow_service_->deleteWorkflow(id, user_id, role);

    if (!result.ok()) {
        sendError(std::move(callback), 404, 40402, result.error());
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

    std::string creator_id = req->getAttributes()->get<std::string>("user_id");

    auto result = workflow_service_->triggerWorkflow(id, creator_id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40402, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

}  // namespace taskflow::scheduler::api
