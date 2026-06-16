#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/service/task_service.h"

namespace taskflow::scheduler::api {

class TaskController : public drogon::HttpController<TaskController, false> {
public:
    explicit TaskController(std::shared_ptr<service::TaskService> task_service);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TaskController::createTask, "/api/v1/tasks", drogon::Post);
    ADD_METHOD_TO(TaskController::listTasks, "/api/v1/tasks", drogon::Get);
    ADD_METHOD_TO(TaskController::getTask, "/api/v1/tasks/{id}", drogon::Get);
    ADD_METHOD_TO(TaskController::updateTask, "/api/v1/tasks/{id}", drogon::Put);
    ADD_METHOD_TO(TaskController::deleteTask, "/api/v1/tasks/{id}", drogon::Delete);
    METHOD_LIST_END

    void createTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void listTasks(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id);

    void getTask(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id);

    void updateTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id);

    void deleteTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id);

private:
    std::shared_ptr<service::TaskService> task_service_;
};

}  // namespace taskflow::scheduler::api
