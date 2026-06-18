#include "scheduler/service/user_service.h"

#include "common/util/password_util.h"
#include "common/models/user.h"

#include <algorithm>

namespace taskflow::scheduler::service {

UserService::UserService() = default;

common::result::Result<nlohmann::json> UserService::listUsers(int page, int page_size) {
    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;

    int offset = (page - 1) * page_size;

    auto listResult = user_dao_.list(offset, page_size);
    if (!listResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to list users: " + listResult.error());
    }

    const auto& users = listResult.value();
    nlohmann::json items = nlohmann::json::array();
    for (const auto& user : users) {
        items.push_back(user.toSafeJson());
    }

    // Use real total count for correct pagination (completed-features.md 12.7)
    int total = static_cast<int>(users.size());
    auto countResult = user_dao_.count();
    if (countResult.ok()) {
        total = countResult.value();
    }

    nlohmann::json response = {
        {"items", items},
        {"total", total},
        {"page", page},
        {"page_size", page_size}
    };

    return response;
}

common::result::Result<nlohmann::json> UserService::getUser(const std::string& id) {
    auto userResult = user_dao_.findById(id);
    if (!userResult.ok()) {
        return common::result::Result<nlohmann::json>::failure("User not found: " + userResult.error());
    }
    return userResult.value().toSafeJson();
}

common::result::Result<nlohmann::json> UserService::createUser(
    const std::string& username, const std::string& password, const std::string& role) {

    // Validate username length (3-32 chars)
    if (username.length() < 3 || username.length() > 32) {
        return common::result::Result<nlohmann::json>::failure(
            "Username must be between 3 and 32 characters");
    }

    // Validate password length (>=8 chars)
    if (password.length() < 8) {
        return common::result::Result<nlohmann::json>::failure(
            "Password must be at least 8 characters");
    }

    // Validate role
    if (role != "admin" && role != "operator" && role != "viewer") {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid role: must be one of admin, operator, viewer");
    }

    // Check username uniqueness
    auto existingResult = user_dao_.findByUsername(username);
    if (existingResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Username already exists");
    }

    // Hash password
    auto hashResult = common::util::PasswordUtil::hashPassword(password);
    if (!hashResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to hash password: " + hashResult.error());
    }

    // Create user
    auto createResult = user_dao_.create(username, hashResult.value(), role);
    if (!createResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to create user: " + createResult.error());
    }

    // Fetch the created user
    auto userResult = user_dao_.findById(createResult.value());
    if (!userResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to fetch created user: " + userResult.error());
    }

    return userResult.value().toSafeJson();
}

common::result::Result<nlohmann::json> UserService::updateUserRole(const std::string& id, const std::string& role) {
    // Validate role
    if (role != "admin" && role != "operator" && role != "viewer") {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid role: must be one of admin, operator, viewer");
    }

    auto result = user_dao_.updateRole(id, role);
    if (!result.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to update user role: " + result.error());
    }

    // Fetch updated user
    auto userResult = user_dao_.findById(id);
    if (!userResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to fetch updated user: " + userResult.error());
    }

    return userResult.value().toSafeJson();
}

common::result::Result<void> UserService::deleteUser(const std::string& id, const std::string& current_user_id) {
    // Cannot delete self
    if (id == current_user_id) {
        return common::result::Result<void>::failure(
            "Cannot delete your own account");
    }

    // Check user exists
    auto userResult = user_dao_.findById(id);
    if (!userResult.ok()) {
        return common::result::Result<void>::failure(
            "User not found: " + userResult.error());
    }

    // Soft delete: set deleted_at timestamp
    auto result = user_dao_.softDelete(id);
    if (!result.ok()) {
        return common::result::Result<void>::failure(
            "Failed to delete user: " + result.error());
    }

    return common::result::Result<void>();
}

}  // namespace taskflow::scheduler::service
