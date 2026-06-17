#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "scheduler/dao/user_dao.h"

namespace taskflow::scheduler::service {

class UserService {
public:
    UserService();

    // List users with pagination
    common::result::Result<nlohmann::json> listUsers(int page, int page_size);

    // Get user by ID
    common::result::Result<nlohmann::json> getUser(const std::string& id);

    // Create user (admin creates new user)
    common::result::Result<nlohmann::json> createUser(
        const std::string& username, const std::string& password, const std::string& role);

    // Update user role
    common::result::Result<void> updateUserRole(const std::string& id, const std::string& role);

    // Delete user
    common::result::Result<void> deleteUser(const std::string& id, const std::string& current_user_id);

private:
    dao::UserDao user_dao_;
};

}  // namespace taskflow::scheduler::service
