#pragma once

#include <string>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <jwt-cpp/jwt.h>
#include "common/result/result.h"
#include "common/util/uuid.h"

namespace taskflow::common::util {

struct TokenPair {
    std::string access_token;
    std::string refresh_token;
    int expires_in;
};

struct TokenPayload {
    std::string user_id;
    std::string username;
    std::string role;
    std::string type;
    std::string jti;  // JWT ID for blacklisting
};

class JwtUtil {
public:
    JwtUtil() = delete;

    static common::result::Result<TokenPair> generateTokens(
            const std::string& user_id,
            const std::string& username,
            const std::string& role,
            const std::string& secret,
            int access_ttl,
            int refresh_ttl) {
        try {
            auto now = std::chrono::system_clock::now();

            auto access_token = jwt::create()
                .set_issuer("taskflow")
                .set_type("JWT")
                .set_subject(user_id)
                .set_id(generateUuid())
                .set_issued_at(now)
                .set_expires_at(now + std::chrono::seconds{access_ttl})
                .set_payload_claim("username", jwt::claim(username))
                .set_payload_claim("role", jwt::claim(role))
                .set_payload_claim("type", jwt::claim(std::string("access")))
                .sign(jwt::algorithm::hs256{secret});

            auto refresh_token = jwt::create()
                .set_issuer("taskflow")
                .set_type("JWT")
                .set_subject(user_id)
                .set_id(generateUuid())
                .set_issued_at(now)
                .set_expires_at(now + std::chrono::seconds{refresh_ttl})
                .set_payload_claim("username", jwt::claim(username))
                .set_payload_claim("role", jwt::claim(role))
                .set_payload_claim("type", jwt::claim(std::string("refresh")))
                .sign(jwt::algorithm::hs256{secret});

            return TokenPair{
                std::move(access_token),
                std::move(refresh_token),
                access_ttl
            };
        } catch (const std::exception& e) {
            return common::result::Result<TokenPair>::failure(
                std::string("Failed to generate tokens: ") + e.what());
        }
    }

    static common::result::Result<TokenPayload> verifyToken(
            const std::string& token,
            const std::string& secret) {
        try {
            auto decoded = jwt::decode(token);

            auto verifier = jwt::verify()
                .allow_algorithm(jwt::algorithm::hs256{secret})
                .with_issuer("taskflow");

            verifier.verify(decoded);

            TokenPayload payload;
            payload.user_id = decoded.get_subject();
            if (decoded.has_payload_claim("username")) {
                payload.username = decoded.get_payload_claim("username").as_string();
            }
            if (decoded.has_payload_claim("role")) {
                payload.role = decoded.get_payload_claim("role").as_string();
            }
            if (decoded.has_payload_claim("type")) {
                payload.type = decoded.get_payload_claim("type").as_string();
            }
            if (decoded.has_id()) {
                payload.jti = decoded.get_id();
            }

            return payload;
        } catch (const std::exception& e) {
            return common::result::Result<TokenPayload>::failure(
                std::string("Token verification failed: ") + e.what());
        }
    }

    static common::result::Result<TokenPayload> parseTokenUnverified(
            const std::string& token) {
        try {
            auto decoded = jwt::decode(token);

            TokenPayload payload;
            payload.user_id = decoded.get_subject();
            if (decoded.has_payload_claim("username")) {
                payload.username = decoded.get_payload_claim("username").as_string();
            }
            if (decoded.has_payload_claim("role")) {
                payload.role = decoded.get_payload_claim("role").as_string();
            }
            if (decoded.has_payload_claim("type")) {
                payload.type = decoded.get_payload_claim("type").as_string();
            }
            if (decoded.has_id()) {
                payload.jti = decoded.get_id();
            }

            return payload;
        } catch (const std::exception& e) {
            return common::result::Result<TokenPayload>::failure(
                std::string("Failed to parse token: ") + e.what());
        }
    }
};

class TokenBlacklist {
public:
    static TokenBlacklist& instance() {
        static TokenBlacklist inst;
        return inst;
    }
    void add(const std::string& jti) {
        std::lock_guard<std::mutex> lock(mutex_);
        blacklisted_.insert(jti);
    }
    bool isBlacklisted(const std::string& jti) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return blacklisted_.count(jti) > 0;
    }
private:
    TokenBlacklist() = default;
    mutable std::mutex mutex_;
    std::unordered_set<std::string> blacklisted_;
};

}  // namespace taskflow::common::util
