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
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = instance_service_->pauseInstance(id, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        if (result.error().find("Permission denied") != std::string::npos) {
            status = 403;
        } else if (result.error().find("not found") != std::string::npos ||
                   result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 40008, result.error());
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
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = instance_service_->resumeInstance(id, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        if (result.error().find("Permission denied") != std::string::npos) {
            status = 403;
        } else if (result.error().find("not found") != std::string::npos ||
                   result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 40008, result.error());
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
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = instance_service_->cancelInstance(id, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        if (result.error().find("Permission denied") != std::string::npos) {
            status = 403;
        } else if (result.error().find("not found") != std::string::npos ||
                   result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 40008, result.error());
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
    const drogon::HttpRequestPtr& req,
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

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = instance_service_->retryTask(id, taskInstanceId, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        if (result.error().find("Permission denied") != std::string::npos) {
            status = 403;
        } else if (result.error().find("not found") != std::string::npos ||
                   result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 40009, result.error());
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
    const drogon::HttpRequestPtr& req,
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

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = instance_service_->killTask(id, taskInstanceId, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        if (result.error().find("Permission denied") != std::string::npos) {
            status = 403;
        } else if (result.error().find("not found") != std::string::npos ||
                   result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 40009, result.error());
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
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    if (!isValidUUID(id)) {
        sendError(std::move(callback), 400, 40001, "Invalid ID format: must be a valid UUID");
        return;
    }

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = instance_service_->getInstance(id, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        if (result.error().find("Permission denied") != std::string::npos) {
            status = 403;
        } else if (result.error().find("not found") != std::string::npos ||
                   result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 40403, result.error());
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
    // Fix #314: validate pagination parameters
    if (page < 1 || page_size < 1 || page_size > 100) {
        sendError(std::move(callback), 400, 40002, "page must be >= 1 and page_size must be between 1 and 100");
        return;
    }

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = instance_service_->listInstances(id, page, page_size, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        if (result.error().find("Permission denied") != std::string::npos) {
            status = 403;
        } else if (result.error().find("not found") != std::string::npos ||
                   result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 50001, result.error());
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
    std::string task_id = std::string(req->getParameter("task_id"));  // Fix #225

    if (!page_str.empty()) {
        try { page = std::stoi(page_str); } catch (...) {}
    }
    if (!page_size_str.empty()) {
        try { page_size = std::stoi(page_size_str); } catch (...) {}
    }
    // Fix #314: validate pagination parameters
    if (page < 1 || page_size < 1 || page_size > 100) {
        sendError(std::move(callback), 400, 40002, "page must be >= 1 and page_size must be between 1 and 100");
        return;
    }

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

    // Fix #225: If task_id is provided, filter instances by task_id (server-side
    // JOIN). This replaces the broken client-side filtering in TaskDetailView
    // that always returned an empty list because listAllInstances doesn't
    // include the tasks subarray.
    if (!task_id.empty()) {
        if (!isValidUUID(task_id)) {
            sendError(std::move(callback), 400, 40001, "Invalid task_id format: must be a valid UUID");
            return;
        }
        auto result = instance_service_->listInstancesByTaskId(
            task_id, page, page_size, user_id, role);
        if (!result.ok()) {
            sendError(std::move(callback), 400, 50001, result.error());
            return;
        }
        sendSuccess(std::move(callback), result.value());
        return;
    }

    // If workflow_id is provided, filter by it
    if (!workflow_id.empty()) {
        if (!isValidUUID(workflow_id)) {
            sendError(std::move(callback), 400, 40001, "Invalid workflow_id format: must be a valid UUID");
            return;
        }
        auto result = instance_service_->listInstances(workflow_id, page, page_size, user_id, role);
        if (!result.ok()) {
            // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
            int status = 400;
            if (result.error().find("Permission denied") != std::string::npos) {
                status = 403;
            } else if (result.error().find("not found") != std::string::npos ||
                       result.error().find("不存在") != std::string::npos) {
                status = 404;
            }
            sendError(std::move(callback), status, 50001, result.error());
            return;
        }
        sendSuccess(std::move(callback), result.value());
        return;
    }

    auto result = instance_service_->listAllInstances(page, page_size, user_id, role);

    if (!result.ok()) {
        // Fix #159: distinguish 403 (permission) / 404 (not found) / 400 (other)
        int status = 400;
        if (result.error().find("Permission denied") != std::string::npos) {
            status = 403;
        } else if (result.error().find("not found") != std::string::npos ||
                   result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 50001, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

void InstanceController::getTaskLog(
    const drogon::HttpRequestPtr& req,
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

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");
    auto result = instance_service_->getTaskLog(id, taskInstanceId, user_id, role);

    if (!result.ok()) {
        // Fix #137: distinguish not-found (404) from other errors (400)
        int status = 400;
        if (result.error().find("not found") != std::string::npos ||
            result.error().find("不存在") != std::string::npos) {
            status = 404;
        }
        sendError(std::move(callback), status, 40404, result.error());
        return;
    }

    nlohmann::json response = {
        {"log", result.value()}
    };

    sendSuccess(std::move(callback), response);
}

void InstanceController::streamTaskLog(
    const drogon::HttpRequestPtr& req,
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

    // Fix #134: resource-level permission check
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    std::string role = req->getAttributes()->get<std::string>("role");

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
    // Fix #318: Use follow=true for SSE streaming to get real-time log updates
    auto log_result = instance_service_->getTaskLog(id, taskInstanceId, user_id, role, true);

    if (!log_result.ok()) {
        // Fix #137: use error event instead of data event for errors
        std::string sse_data = "event: error\ndata: " + log_result.error() + "\n\n";
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
