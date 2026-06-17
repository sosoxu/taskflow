#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/service/workflow_service.h"

namespace taskflow::scheduler::api {

class WorkflowController : public drogon::HttpController<WorkflowController, false> {
public:
    explicit WorkflowController(std::shared_ptr<service::WorkflowService> workflow_service);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WorkflowController::createWorkflow, "/api/v1/workflows", drogon::Post, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(WorkflowController::listWorkflows, "/api/v1/workflows", drogon::Get, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(WorkflowController::getWorkflow, "/api/v1/workflows/{id}", drogon::Get, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(WorkflowController::updateWorkflow, "/api/v1/workflows/{id}", drogon::Put, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(WorkflowController::deleteWorkflow, "/api/v1/workflows/{id}", drogon::Delete, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(WorkflowController::triggerWorkflow, "/api/v1/workflows/{id}/trigger", drogon::Post, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    METHOD_LIST_END

    void createWorkflow(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void listWorkflows(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void getWorkflow(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id);

    void updateWorkflow(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id);

    void deleteWorkflow(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id);

    void triggerWorkflow(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id);

private:
    std::shared_ptr<service::WorkflowService> workflow_service_;
};

}  // namespace taskflow::scheduler::api
