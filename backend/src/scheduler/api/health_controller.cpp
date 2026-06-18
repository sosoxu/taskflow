#include "scheduler/api/health_controller.h"
#include <drogon/HttpResponse.h>

namespace taskflow::scheduler::api {

void HealthController::healthCheck(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value resp;
    resp["code"] = 0;
    resp["message"] = "success";
    resp["data"]["status"] = "ok";

    auto httpResp = drogon::HttpResponse::newHttpJsonResponse(resp);
    httpResp->setStatusCode(drogon::k200OK);
    callback(httpResp);
}

}  // namespace taskflow::scheduler::api
