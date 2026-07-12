#include "common/util/token_blacklist.h"

#include <spdlog/spdlog.h>

#include "common/database/database_manager.h"
#include "common/result/result.h"

namespace taskflow::common::util {

void TokenBlacklist::add(const std::string& jti, int64_t exp_timestamp) {
    if (jti.empty()) return;

    int64_t now = nowSeconds();
    if (exp_timestamp == 0) {
        exp_timestamp = now + 86400;  // 默认 24h
    }

    // 写入 DB
    addToDb(jti, exp_timestamp);

    // 更新本地缓存
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpiredCache();
    cache_[jti] = {true, exp_timestamp, 0};
}

bool TokenBlacklist::isBlacklisted(const std::string& jti) const {
    if (jti.empty()) return false;

    int64_t now = nowSeconds();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(jti);
        if (it != cache_.end()) {
            if (it->second.is_blacklisted) {
                // 正向缓存：token 过期后清除
                if (it->second.expires_at >= now) {
                    return true;
                }
                // token 已自然过期，移除缓存
                cache_.erase(it);
            } else {
                // 负向缓存：5 秒内直接返回 false
                if (it->second.cached_until > now) {
                    return false;
                }
                // 缓存过期，需重新查 DB
                cache_.erase(it);
            }
        }
    }

    // 查 DB
    bool in_db = checkDb(jti);

    // 更新缓存
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (in_db) {
            // 正向缓存：不知道确切过期时间，用 24h 兜底
            cache_[jti] = {true, now + 86400, 0};
        } else {
            // 负向缓存：5 秒后重新查 DB
            cache_[jti] = {false, 0, now + NEGATIVE_CACHE_TTL};
        }
    }

    return in_db;
}

bool TokenBlacklist::tryAddIfNotBlacklisted(const std::string& jti, int64_t exp_timestamp) {
    if (jti.empty()) return true;

    int64_t now = nowSeconds();
    if (exp_timestamp == 0) {
        exp_timestamp = now + 86400;
    }

    // 原子写入 DB：ON CONFLICT DO NOTHING，affected_rows > 0 表示首次写入
    bool inserted = false;
    try {
        auto result = common::database::DatabaseManager::instance().withTransaction<bool>(
            [&](pqxx::work& txn) -> bool {
                auto res = txn.exec_params(
                    "INSERT INTO token_blacklist (jti, expires_at) "
                    "VALUES ($1, to_timestamp($2)) "
                    "ON CONFLICT (jti) DO NOTHING",
                    jti, exp_timestamp);
                return res.affected_rows() > 0;
            });
        inserted = result.ok() && result.value();
    } catch (const std::exception& e) {
        spdlog::error("TokenBlacklist: tryAddIfNotBlacklisted DB error for jti {}: {}",
                      jti, e.what());
        return false;
    }

    // 更新本地缓存
    {
        std::lock_guard<std::mutex> lock(mutex_);
        purgeExpiredCache();
        if (inserted) {
            cache_[jti] = {true, exp_timestamp, 0};
        } else {
            // 已在黑名单中，更新缓存为正向
            cache_[jti] = {true, exp_timestamp, 0};
        }
    }

    return inserted;
}

bool TokenBlacklist::checkDb(const std::string& jti) const {
    try {
        auto result = common::database::DatabaseManager::instance().withReadTransaction<bool>(
            [&](pqxx::nontransaction& txn) -> bool {
                auto res = txn.exec_params(
                    "SELECT 1 FROM token_blacklist WHERE jti = $1 "
                    "AND expires_at > NOW()",
                    jti);
                return !res.empty();
            });
        return result.ok() && result.value();
    } catch (const std::exception& e) {
        spdlog::error("TokenBlacklist: checkDb error for jti {}: {}", jti, e.what());
        return false;
    }
}

void TokenBlacklist::addToDb(const std::string& jti, int64_t exp_timestamp) {
    try {
        common::database::DatabaseManager::instance().withTransaction<void>(
            [&](pqxx::work& txn) -> common::result::Result<void> {
                txn.exec_params(
                    "INSERT INTO token_blacklist (jti, expires_at) "
                    "VALUES ($1, to_timestamp($2)) "
                    "ON CONFLICT (jti) DO NOTHING",
                    jti, exp_timestamp);
                return common::result::Result<void>();
            });
    } catch (const std::exception& e) {
        spdlog::error("TokenBlacklist: addToDb error for jti {}: {}", jti, e.what());
    }
}

void TokenBlacklist::purgeExpiredCache() {
    int64_t now = nowSeconds();
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->second.is_blacklisted) {
            // 正向缓存：token 过期后清除
            if (it->second.expires_at < now) {
                it = cache_.erase(it);
                continue;
            }
        } else {
            // 负向缓存：过期后清除
            if (it->second.cached_until <= now) {
                it = cache_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

}  // namespace taskflow::common::util
