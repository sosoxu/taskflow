#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
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
    int64_t exp{0};   // Fix #187: expiration timestamp (unix seconds) for blacklist cleanup
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

            // Fix #187: Parse exp (expiration) for blacklist expiration cleanup.
            try {
                payload.exp = std::chrono::duration_cast<std::chrono::seconds>(
                    decoded.get_expires_at().time_since_epoch()).count();
            } catch (...) {
                payload.exp = 0;
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

            // Fix #187: Parse exp (expiration) for blacklist expiration cleanup.
            try {
                payload.exp = std::chrono::duration_cast<std::chrono::seconds>(
                    decoded.get_expires_at().time_since_epoch()).count();
            } catch (...) {
                payload.exp = 0;
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

    // Fix #187: Record the token's exp so expired entries can be purged.
    // If exp_timestamp is 0 (unknown), a 24h default is used. This prevents
    // unbounded memory growth from tokens that have already expired naturally.
    //
    // NOTE: The blacklist is stored in-process memory and is lost on restart.
    // Full persistence (e.g. a DB table) is a future enhancement; for now
    // expired tokens are naturally rejected by JWT verification after restart.
    void add(const std::string& jti, int64_t exp_timestamp = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t now = nowSeconds();
        if (exp_timestamp == 0) {
            exp_timestamp = now + 86400;  // default 24h
        }
        // Purge already-expired entries to bound memory usage.
        for (auto it = blacklisted_.begin(); it != blacklisted_.end(); ) {
            if (it->second < now) {
                it = blacklisted_.erase(it);
            } else {
                ++it;
            }
        }
        blacklisted_[jti] = exp_timestamp;
    }

    bool isBlacklisted(const std::string& jti) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = blacklisted_.find(jti);
        if (it == blacklisted_.end()) {
            return false;
        }
        // Fix #187: An expired entry means the token has expired naturally,
        // so blacklisting is moot — treat as not blacklisted and clean up.
        if (it->second < nowSeconds()) {
            blacklisted_.erase(it);
            return false;
        }
        return true;
    }

private:
    TokenBlacklist() = default;

    static int64_t nowSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    mutable std::mutex mutex_;
    // jti -> expiration timestamp (unix seconds)
    mutable std::unordered_map<std::string, int64_t> blacklisted_;
};

}  // namespace taskflow::common::util
