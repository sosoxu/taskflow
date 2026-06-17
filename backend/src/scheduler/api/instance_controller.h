#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/service/instance_service.h"

namespace taskflow::scheduler::api {

class InstanceController : public drogon::HttpController<InstanceController, false> {
public:
    explicit InstanceController(std::shared_ptr<service::InstanceService> instance_service);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(InstanceController::pauseInstance, "/api/v1/instances/{id}/pause", drogon::Post, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(InstanceController::resumeInstance, "/api/v1/instances/{id}/resume", drogon::Post, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(InstanceController::cancelInstance, "/api/v1/instances/{id}/cancel", drogon::Post, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(InstanceController::retryTask, "/api/v1/instances/{id}/tasks/{taskInstanceId}/retry", drogon::Post, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(InstanceController::killTask, "/api/v1/instances/{id}/tasks/{taskInstanceId}/kill", drogon::Post, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(InstanceController::getInstance, "/api/v1/instances/{id}", drogon::Get, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(InstanceController::listInstances, "/api/v1/workflows/{id}/instances", drogon::Get, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(InstanceController::getTaskLog, "/api/v1/instances/{id}/tasks/{taskInstanceId}/logs", drogon::Get, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    ADD_METHOD_TO(InstanceController::streamTaskLog, "/api/v1/instances/{id}/tasks/{taskInstanceId}/logs/stream", drogon::Get, "taskflow::scheduler::middleware::AuthFilter", "taskflow::scheduler::middleware::RoleFilter");
    METHOD_LIST_END

    void pauseInstance(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id);

    void resumeInstance(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id);

    void cancelInstance(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id);

    void retryTask(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id,
                   const std::string& taskInstanceId);

    void killTask(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id,
                  const std::string& taskInstanceId);

    void getInstance(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id);

    void listInstances(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id);

    void getTaskLog(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id,
                    const std::string& taskInstanceId);

    void streamTaskLog(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id,
                       const std::string& taskInstanceId);

private:
    std::shared_ptr<service::InstanceService> instance_service_;
};

}  // namespace taskflow::scheduler::api
