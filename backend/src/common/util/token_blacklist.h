#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace taskflow::common::util {

// TokenBlacklist: 基于 PostgreSQL 的 JWT 黑名单，支持多实例共享。
//
// 设计：
// - 写入（add/tryAddIfNotBlacklisted）：直接写 DB，同时更新本地缓存。
// - 读取（isBlacklisted）：先查本地缓存，命中则直接返回；未命中则查 DB。
//   - 正向缓存（已拉黑）：缓存到 token 过期，避免重复查 DB。
//   - 负向缓存（未拉黑）：缓存 5 秒，既减少 DB 压力，又保证其他实例
//     新拉黑后最多 5 秒内本实例也能感知。
// - DB 层有 token_blacklist 表，jti 为主键，INSERT ON CONFLICT DO NOTHING
//   保证原子性，多实例并发写入不会重复。
//
// 与旧实现的区别：旧实现用 unordered_map 存内存，重启丢失且多实例不共享。
// 新实现以 DB 为权威源，内存仅做缓存，重启和 HA 均不丢失黑名单。
class TokenBlacklist {
public:
    static TokenBlacklist& instance() {
        static TokenBlacklist inst;
        return inst;
    }

    // 将 jti 加入黑名单。exp_timestamp 为 token 的过期时间（unix 秒）。
    // Fix #329: 返回是否成功持久化到 DB（写库失败返回 false，调用方应把
    // 登出视为未完成而非假成功）。本地缓存无论成败都置位（本实例立即生效）。
    bool add(const std::string& jti, int64_t exp_timestamp = 0);

    // 检查 jti 是否在黑名单中。
    // Fix #329: DB 查询异常时 fail-closed（返回 true 视为已拉黑），
    // 不再放行——DB 故障时系统本就不可用，安全控制不随故障降级失效。
    bool isBlacklisted(const std::string& jti) const;

    // 原子检查并加入：若 jti 不在黑名单中则加入并返回 true，
    // 若已在黑名单中则返回 false。用于 refresh token 轮换防重放。
    // DB 异常时同样返回 false（fail-closed，拒绝刷新）。
    bool tryAddIfNotBlacklisted(const std::string& jti, int64_t exp_timestamp = 0);

private:
    TokenBlacklist() = default;

    static int64_t nowSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // 负向缓存 TTL（秒）：未拉黑的 jti 缓存 5 秒后重新查 DB
    static constexpr int64_t NEGATIVE_CACHE_TTL = 5;

    struct CacheEntry {
        bool is_blacklisted;     // 是否在黑名单中
        int64_t expires_at;      // token 过期时间（正向上限）
        int64_t cached_until;    // 缓存有效期（负向缓存的过期时间）
    };

    // 查询 DB 判断 jti 是否在黑名单中。
    // 返回 1 已拉黑 / 0 未拉黑 / -1 查询失败（Fix #329）
    int checkDb(const std::string& jti) const;

    // 写入 DB（ON CONFLICT DO NOTHING），失败时重试一次；返回是否成功
    bool addToDb(const std::string& jti, int64_t exp_timestamp);

    // 清理内存缓存中已过期的条目
    void purgeExpiredCache();

    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, CacheEntry> cache_;
};

}  // namespace taskflow::common::util
