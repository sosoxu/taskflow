#include "common/util/token_blacklist.h"

#include <spdlog/spdlog.h>

#include "common/database/database_manager.h"
#include "common/result/result.h"

namespace taskflow::common::util {

bool TokenBlacklist::add(const std::string& jti, int64_t exp_timestamp) {
    if (jti.empty()) return true;

    int64_t now = nowSeconds();
    if (exp_timestamp == 0) {
        exp_timestamp = now + 86400;  // 默认 24h
    }

    // 写入 DB（Fix #329: 失败重试一次后仍失败则向上传递，登出接口据此报错）
    bool persisted = addToDb(jti, exp_timestamp);

    // 更新本地缓存（本实例立即生效）
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpiredCache();
    cache_[jti] = {true, exp_timestamp, 0};
    return persisted;
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
    // Fix #329: -1 表示查询失败 → fail-closed 直接返回 true，且不写缓存，
    // 避免 DB 短暂故障期间把所有 token 缓存为黑名单、恢复后仍被锁。
    int db_state = checkDb(jti);
    if (db_state < 0) {
        return true;
    }
    bool in_db = db_state == 1;

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

int TokenBlacklist::checkDb(const std::string& jti) const {
    try {
        auto result = common::database::DatabaseManager::instance().withReadTransaction<bool>(
            [&](pqxx::nontransaction& txn) -> bool {
                auto res = txn.exec_params(
                    "SELECT 1 FROM token_blacklist WHERE jti = $1 "
                    "AND expires_at > NOW()",
                    jti);
                return !res.empty();
            });
        if (!result.ok()) {
            spdlog::error("TokenBlacklist: checkDb failed for jti {}: {}",
                          jti, result.error());
            return -1;
        }
        return result.value() ? 1 : 0;
    } catch (const std::exception& e) {
        spdlog::error("TokenBlacklist: checkDb error for jti {}: {}", jti, e.what());
        return -1;
    }
}

bool TokenBlacklist::addToDb(const std::string& jti, int64_t exp_timestamp) {
    // Fix #329: 失败重试一次；两次都失败才向上报告失败
    for (int attempt = 0; attempt < 2; ++attempt) {
        try {
            auto result = common::database::DatabaseManager::instance().withTransaction<void>(
                [&](pqxx::work& txn) -> common::result::Result<void> {
                    txn.exec_params(
                        "INSERT INTO token_blacklist (jti, expires_at) "
                        "VALUES ($1, to_timestamp($2)) "
                        "ON CONFLICT (jti) DO NOTHING",
                        jti, exp_timestamp);
                    return common::result::Result<void>();
                });
            if (result.ok()) {
                return true;
            }
            spdlog::error("TokenBlacklist: addToDb failed for jti {} (attempt {}): {}",
                          jti, attempt + 1, result.error());
        } catch (const std::exception& e) {
            spdlog::error("TokenBlacklist: addToDb error for jti {} (attempt {}): {}",
                          jti, attempt + 1, e.what());
        }
    }
    return false;
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
