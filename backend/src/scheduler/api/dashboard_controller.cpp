#include "scheduler/api/dashboard_controller.h"

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>
#include "scheduler/api/response_util.h"

namespace taskflow::scheduler::api {

namespace {

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
