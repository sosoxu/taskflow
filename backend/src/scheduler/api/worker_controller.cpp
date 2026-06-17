#include "scheduler/api/worker_controller.h"

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>

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

WorkerController::WorkerController(std::shared_ptr<service::WorkerService> worker_service)
    : worker_service_(std::move(worker_service)) {}

void WorkerController::listWorkers(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto result = worker_service_->listWorkers();

    if (!result.ok()) {
        sendError(std::move(callback), 400, 50001, result.error());
        return;
    }

    const auto& workers = result.value();
    nlohmann::json items = nlohmann::json::array();
    for (const auto& worker : workers) {
        items.push_back(worker.toJson());
    }

    nlohmann::json response = {
        {"items", items},
        {"total", static_cast<int>(workers.size())}
    };

    sendSuccess(std::move(callback), response);
}

}  // namespace taskflow::scheduler::api
