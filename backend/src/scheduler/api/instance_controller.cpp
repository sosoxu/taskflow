#include "scheduler/api/instance_controller.h"

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

InstanceController::InstanceController(std::shared_ptr<service::InstanceService> instance_service)
    : instance_service_(std::move(instance_service)) {}

void InstanceController::pauseInstance(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    auto result = instance_service_->pauseInstance(id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40000, result.error());
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

    auto result = instance_service_->resumeInstance(id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40000, result.error());
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

    auto result = instance_service_->cancelInstance(id);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40000, result.error());
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

    auto result = instance_service_->retryTask(id, taskInstanceId);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40000, result.error());
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

    auto result = instance_service_->killTask(id, taskInstanceId);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40000, result.error());
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

    auto result = instance_service_->getInstance(id);

    if (!result.ok()) {
        sendError(std::move(callback), 404, 40400, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

void InstanceController::listInstances(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

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
        sendError(std::move(callback), 400, 40000, result.error());
        return;
    }

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(result.value()));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

void InstanceController::getTaskLog(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id,
    const std::string& taskInstanceId) {

    auto result = instance_service_->getTaskLog(id, taskInstanceId);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 40000, result.error());
        return;
    }

    nlohmann::json response = {
        {"log", result.value()}
    };

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(
        nlohmannToJsoncpp(response));
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

}  // namespace taskflow::scheduler::api
