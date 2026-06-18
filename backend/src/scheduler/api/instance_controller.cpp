#include "scheduler/api/instance_controller.h"

#include <cctype>
#include <sstream>
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

}  // namespace

InstanceController::InstanceController(std::shared_ptr<service::InstanceService> instance_service,
                                       const std::string& jwt_secret)
    : instance_service_(std::move(instance_service)), jwt_secret_(jwt_secret) {}

void InstanceController::pauseInstance(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    auto result = instance_service_->pauseInstance(id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40008, result.error());
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

void InstanceController::resumeInstance(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    auto result = instance_service_->resumeInstance(id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40008, result.error());
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

void InstanceController::cancelInstance(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    auto result = instance_service_->cancelInstance(id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40008, result.error());
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

void InstanceController::retryTask(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id,
    const std::string& taskInstanceId) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }
    if (!isValidUUID(taskInstanceId)) {
        sendError(std::move(callback), 400, 40001, "Invalid task instance ID format: must be a valid UUID");
        return;
    }

    auto result = instance_service_->retryTask(id, taskInstanceId);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40009, result.error());
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

void InstanceController::killTask(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id,
    const std::string& taskInstanceId) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }
    if (!isValidUUID(taskInstanceId)) {
        sendError(std::move(callback), 400, 40001, "Invalid task instance ID format: must be a valid UUID");
        return;
    }

    auto result = instance_service_->killTask(id, taskInstanceId);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40009, result.error());
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

void InstanceController::getInstance(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    auto result = instance_service_->getInstance(id);

    if (!result.ok()) {
        sendError(std::move(callback), 404, 40403, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void InstanceController::listInstances(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

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

    auto result = instance_service_->listInstances(id, page, page_size);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 50001, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void InstanceController::listAllInstances(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    int page = 1;
    int page_size = 10;

    std::string page_str = std::string(req->getParameter("page"));
    std::string page_size_str = std::string(req->getParameter("page_size"));
    std::string workflow_id = std::string(req->getParameter("workflow_id"));

    if (!page_str.empty()) {
        try { page = std::stoi(page_str); } catch (...) {}
    }
    if (!page_size_str.empty()) {
        try { page_size = std::stoi(page_size_str); } catch (...) {}
    }

    // If workflow_id is provided, filter by it
    if (!workflow_id.empty()) {
        if (!isValidUUID(workflow_id)) {
            sendError(std::move(callback), 400, 40001, "Invalid workflow_id format: must be a valid UUID");
            return;
        }
        auto result = instance_service_->listInstances(workflow_id, page, page_size);
        if (!result.ok()) {
            sendError(std::move(callback), 400, 50001, result.error());
            return;
        }
        sendSuccess(std::move(callback), result.value());
        return;
    }

    auto result = instance_service_->listAllInstances(page, page_size);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 50001, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void InstanceController::getTaskLog(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id,
    const std::string& taskInstanceId) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }
    if (!isValidUUID(taskInstanceId)) {
        sendError(std::move(callback), 400, 40001, "Invalid task instance ID format: must be a valid UUID");
        return;
    }

    auto result = instance_service_->getTaskLog(id, taskInstanceId);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40404, result.error());
        return;
    }

    nlohmann::json response = {
        {"log", result.value()}
    };

    sendSuccess(std::move(callback), response);
}

void InstanceController::streamTaskLog(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id,
    const std::string& taskInstanceId) {

    // Validate UUID format
    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }
    if (!isValidUUID(taskInstanceId)) {
        sendError(std::move(callback), 400, 40001, "Invalid task instance ID format: must be a valid UUID");
        return;
    }

    // Validate the task instance exists
    auto validate_result = instance_service_->validateTaskInstance(id, taskInstanceId);
    if (!validate_result.ok()) {
        sendError(std::move(callback), 400, 40404, validate_result.error());
        return;
    }

    // Create SSE response with proper headers
    auto httpResp = drogon::HttpResponse::newHttpResponse();
    httpResp->setStatusCode(drogon::k200OK);
    httpResp->setContentTypeString("text/event-stream");
    httpResp->addHeader("Cache-Control", "no-cache");
    httpResp->addHeader("Connection", "keep-alive");

    // Fetch log content via gRPC and send as SSE events
    auto log_result = instance_service_->getTaskLog(id, taskInstanceId);

    if (!log_result.ok()) {
        std::string sse_data = "data: " + log_result.error() + "\n\n";
        httpResp->setBody(sse_data);
        callback(httpResp);
        return;
    }

    const std::string& log_content = log_result.value();

    // Send log content as SSE events, line by line
    std::string sse_body;
    std::istringstream stream(log_content);
    std::string line;
    while (std::getline(stream, line)) {
        sse_body += "data: " + line + "\n\n";
    }
    // Send final event to signal completion
    sse_body += "event: done\ndata: \n\n";

    httpResp->setBody(sse_body);
    callback(httpResp);
}

}  // namespace taskflow::scheduler::api
