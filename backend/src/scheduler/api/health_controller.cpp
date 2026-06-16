#include "scheduler/api/health_controller.h"
#include <drogon/HttpResponse.h>

namespace taskflow::scheduler::api {

void HealthController::healthCheck(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value data;
    data["status"] = "ok";

    auto resp = drogon::HttpResponse::newHttpJsonResponse(data);
    resp->setStatusCode(drogon::k200OK);
    callback(resp);
}

}  // namespace taskflow::scheduler::api
