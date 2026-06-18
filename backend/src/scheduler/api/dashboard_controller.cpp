#include "scheduler/api/dashboard_controller.h"

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

DashboardController::DashboardController(std::shared_ptr<service::DashboardService> dashboard_service)
    : dashboard_service_(std::move(dashboard_service)) {}

void DashboardController::getStats(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto result = dashboard_service_->getStats();

    if (!result.ok()) {
        sendError(std::move(callback), 500, 50001, result.error());
        return;
    }

    sendSuccess(std::move(callback), result.value());
}

}  // namespace taskflow::scheduler::api
