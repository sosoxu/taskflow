#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/service/worker_service.h"

namespace taskflow::scheduler::api {

class WorkerController : public drogon::HttpController<WorkerController, false> {
public:
    explicit WorkerController(std::shared_ptr<service::WorkerService> worker_service);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WorkerController::listWorkers, "/api/v1/workers", drogon::Get, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    METHOD_LIST_END

    void listWorkers(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    std::shared_ptr<service::WorkerService> worker_service_;
};

}  // namespace taskflow::scheduler::api
