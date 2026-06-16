#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/service/user_service.h"

namespace taskflow::scheduler::api {

class UserController : public drogon::HttpController<UserController, false> {
public:
    explicit UserController(std::shared_ptr<service::UserService> user_service);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::listUsers, "/api/v1/users", drogon::Get);
    ADD_METHOD_TO(UserController::createUser, "/api/v1/users", drogon::Post);
    ADD_METHOD_TO(UserController::updateUserRole, "/api/v1/users/{id}/role", drogon::Put);
    ADD_METHOD_TO(UserController::deleteUser, "/api/v1/users/{id}", drogon::Delete);
    METHOD_LIST_END

    void listUsers(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void createUser(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void updateUserRole(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id);

    void deleteUser(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id);

private:
    std::shared_ptr<service::UserService> user_service_;
};

}  // namespace taskflow::scheduler::api
