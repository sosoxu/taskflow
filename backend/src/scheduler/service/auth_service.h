#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "common/util/jwt_util.h"
#include "scheduler/dao/user_dao.h"

namespace taskflow::scheduler::service {

class AuthService {
public:
    AuthService(const std::string& jwt_secret, int access_ttl, int refresh_ttl);

    common::result::Result<nlohmann::json> registerUser(
        const std::string& username, const std::string& password,
        const std::string& role = "");

    common::result::Result<nlohmann::json> login(
        const std::string& username, const std::string& password);

    common::result::Result<nlohmann::json> refreshToken(
        const std::string& refresh_token);

    common::result::Result<common::util::TokenPayload> verifyAccessToken(
        const std::string& token);

    common::result::Result<void> logout(const std::string& access_token);

    // Add jti to token blacklist directly
    void blacklistJti(const std::string& jti);

private:
    std::string jwt_secret_;
    int access_ttl_;
    int refresh_ttl_;
    dao::UserDao user_dao_;
};

}  // namespace taskflow::scheduler::service
