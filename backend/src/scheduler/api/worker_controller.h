#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/service/worker_service.h"
#include "scheduler/service/worker_deploy_service.h"

namespace taskflow::scheduler::api {

class WorkerController : public drogon::HttpController<WorkerController, false> {
public:
    WorkerController(std::shared_ptr<service::WorkerService> worker_service,
                     std::shared_ptr<service::WorkerDeployService> worker_deploy_service);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WorkerController::listWorkers, "/api/v1/workers", drogon::Get, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(WorkerController::deployWorker, "/api/v1/workers/deploy", drogon::Post, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    METHOD_LIST_END

    void listWorkers(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void deployWorker(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    std::shared_ptr<service::WorkerService> worker_service_;
    std::shared_ptr<service::WorkerDeployService> worker_deploy_service_;
};

}  // namespace taskflow::scheduler::api
