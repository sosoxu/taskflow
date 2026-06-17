#include "scheduler/service/auth_service.h"

#include "common/util/password_util.h"
#include "common/util/jwt_util.h"
#include "common/models/user.h"

namespace taskflow::scheduler::service {

AuthService::AuthService(const std::string& jwt_secret, int access_ttl, int refresh_ttl)
    : jwt_secret_(jwt_secret), access_ttl_(access_ttl), refresh_ttl_(refresh_ttl) {}

common::result::Result<nlohmann::json> AuthService::registerUser(
    const std::string& username, const std::string& password) {

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

    // Create user with default role "operator"
    auto createResult = user_dao_.create(username, hashResult.value(), "operator");
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

    const auto& user = userResult.value();

    // Generate tokens
    auto tokenResult = common::util::JwtUtil::generateTokens(
        user.id, user.username, user.role,
        jwt_secret_, access_ttl_, refresh_ttl_);
    if (!tokenResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to generate tokens: " + tokenResult.error());
    }

    nlohmann::json response = {
        {"user", user.toSafeJson()},
        {"access_token", tokenResult.value().access_token},
        {"refresh_token", tokenResult.value().refresh_token},
        {"expires_in", 86400}
    };

    return response;
}

common::result::Result<nlohmann::json> AuthService::login(
    const std::string& username, const std::string& password) {

    // Find user by username
    auto userResult = user_dao_.findByUsername(username);
    if (!userResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid username or password");
    }

    const auto& user = userResult.value();

    // Verify password
    auto verifyResult = common::util::PasswordUtil::verifyPassword(password, user.password_hash);
    if (!verifyResult.ok() || !verifyResult.value()) {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid username or password");
    }

    // Generate tokens
    auto tokenResult = common::util::JwtUtil::generateTokens(
        user.id, user.username, user.role,
        jwt_secret_, access_ttl_, refresh_ttl_);
    if (!tokenResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to generate tokens: " + tokenResult.error());
    }

    nlohmann::json response = {
        {"access_token", tokenResult.value().access_token},
        {"refresh_token", tokenResult.value().refresh_token},
        {"expires_in", 86400}
    };

    return response;
}

common::result::Result<nlohmann::json> AuthService::refreshToken(
    const std::string& refresh_token) {

    // Verify refresh token
    auto verifyResult = common::util::JwtUtil::verifyToken(refresh_token, jwt_secret_);
    if (!verifyResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Invalid or expired refresh token: " + verifyResult.error());
    }

    const auto& payload = verifyResult.value();

    // Check token type is "refresh"
    if (payload.type != "refresh") {
        return common::result::Result<nlohmann::json>::failure(
            "Token is not a refresh token");
    }

    // Generate new tokens (only access token is returned in response)
    auto tokenResult = common::util::JwtUtil::generateTokens(
        payload.user_id, payload.username, payload.role,
        jwt_secret_, access_ttl_, refresh_ttl_);
    if (!tokenResult.ok()) {
        return common::result::Result<nlohmann::json>::failure(
            "Failed to generate token: " + tokenResult.error());
    }

    nlohmann::json response = {
        {"access_token", tokenResult.value().access_token},
        {"expires_in", 86400}
    };

    return response;
}

common::result::Result<common::util::TokenPayload> AuthService::verifyAccessToken(
    const std::string& token) {

    auto verifyResult = common::util::JwtUtil::verifyToken(token, jwt_secret_);
    if (!verifyResult.ok()) {
        return common::result::Result<common::util::TokenPayload>::failure(
            verifyResult.error());
    }

    const auto& payload = verifyResult.value();

    if (payload.type != "access") {
        return common::result::Result<common::util::TokenPayload>::failure(
            "Token is not an access token");
    }

    return payload;
}

common::result::Result<void> AuthService::logout(const std::string& access_token) {
    auto verifyResult = common::util::JwtUtil::verifyToken(access_token, jwt_secret_);
    if (!verifyResult.ok()) {
        return common::result::Result<void>::failure("Invalid token");
    }
    const auto& payload = verifyResult.value();
    if (payload.type != "access") {
        return common::result::Result<void>::failure("Token is not an access token");
    }
    // Add jti to blacklist
    if (!payload.jti.empty()) {
        common::util::TokenBlacklist::instance().add(payload.jti);
    }
    return common::result::Result<void>();
}

}  // namespace taskflow::scheduler::service
