#pragma once

#include <memory>
#include <drogon/HttpController.h>
#include "scheduler/service/auth_service.h"

namespace taskflow::scheduler::api {

class AuthController : public drogon::HttpController<AuthController, false> {
public:
    explicit AuthController(std::shared_ptr<service::AuthService> auth_service);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::registerUser, "/api/v1/auth/register", drogon::Post);
    ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthController::refreshToken, "/api/v1/auth/refresh", drogon::Post);
    ADD_METHOD_TO(AuthController::logout, "/api/v1/auth/logout", drogon::Post);
    METHOD_LIST_END

    void registerUser(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void login(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void refreshToken(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void logout(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    std::shared_ptr<service::AuthService> auth_service_;
};

}  // namespace taskflow::scheduler::api
