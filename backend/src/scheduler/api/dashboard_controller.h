#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/service/dashboard_service.h"

namespace taskflow::scheduler::api {

class DashboardController : public drogon::HttpController<DashboardController, false> {
public:
    explicit DashboardController(std::shared_ptr<service::DashboardService> dashboard_service);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DashboardController::getStats,
                  "/api/v1/dashboard/stats",
                  drogon::Get,
                  "taskflow::scheduler::middleware::AuthFilter",
                  "taskflow::scheduler::middleware::RoleFilter");
    METHOD_LIST_END

    void getStats(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    std::shared_ptr<service::DashboardService> dashboard_service_;
};

}  // namespace taskflow::scheduler::api
