#pragma once

#include <string>
#include <chrono>
#include <jwt-cpp/jwt.h>
#include "common/result/result.h"

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

            return payload;
        } catch (const std::exception& e) {
            return common::result::Result<TokenPayload>::failure(
                std::string("Failed to parse token: ") + e.what());
        }
    }
};

}  // namespace taskflow::common::util
