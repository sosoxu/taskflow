#pragma once

#include <drogon/HttpController.h>

namespace taskflow::scheduler::api {

class HealthController : public drogon::HttpController<HealthController, false> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::healthCheck, "/api/v1/health", drogon::Get);
    METHOD_LIST_END

    void healthCheck(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace taskflow::scheduler::api
