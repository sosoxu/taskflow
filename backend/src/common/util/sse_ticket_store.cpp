#include "common/util/sse_ticket_store.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <openssl/rand.h>
#include <spdlog/spdlog.h>

#include "common/database/database_manager.h"

namespace taskflow::common::util {

namespace {

// 32 字节 CSPRNG 随机数转 64 位十六进制串。
// 与 uuid.h 同样的理由：票据等价于短期凭据，必须不可猜测。
std::string generateTicket() {
    unsigned char bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("RAND_bytes failed: cannot generate secure SSE ticket");
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : bytes) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

}  // namespace

std::string SseTicketStore::issue(const std::string& user_id,
                                  const std::string& username,
                                  const std::string& role,
                                  const std::string& instance_id,
                                  const std::string& task_instance_id,
                                  int ttl_seconds) {
    if (user_id.empty() || instance_id.empty() || task_instance_id.empty()) {
        return "";
    }
    if (ttl_seconds <= 0) {
        ttl_seconds = kDefaultTtlSeconds;
    }

    std::string ticket;
    try {
        ticket = generateTicket();
    } catch (const std::exception& e) {
        spdlog::error("SseTicketStore: {}", e.what());
        return "";
    }

    try {
        auto result = common::database::DatabaseManager::instance().withTransaction<bool>(
            [&](pqxx::work& txn) -> bool {
                // 顺带清理过期票据，避免表无限增长（expires_at 上有索引）
                txn.exec("DELETE FROM sse_tickets WHERE expires_at < NOW()");
                txn.exec_params(
                    "INSERT INTO sse_tickets "
                    "(ticket, user_id, username, role, instance_id, task_instance_id, expires_at) "
                    "VALUES ($1, $2, $3, $4, $5, $6, NOW() + make_interval(secs => $7))",
                    ticket, user_id, username, role, instance_id, task_instance_id,
                    static_cast<double>(ttl_seconds));
                return true;
            });
        if (!result.ok()) {
            spdlog::error("SseTicketStore: issue failed: {}", result.error());
            return "";
        }
    } catch (const std::exception& e) {
        spdlog::error("SseTicketStore: issue DB error: {}", e.what());
        return "";
    }

    return ticket;
}

std::optional<SseTicket> SseTicketStore::consume(const std::string& ticket) {
    if (ticket.empty()) {
        return std::nullopt;
    }

    std::optional<SseTicket> consumed;
    try {
        // 单条 DELETE ... RETURNING：并发/重放只有一个请求能删到行，天然单次有效。
        // 已过期（expires_at <= NOW()）或已消费都返回空结果。
        auto result = common::database::DatabaseManager::instance().withTransaction<bool>(
            [&](pqxx::work& txn) -> bool {
                auto res = txn.exec_params(
                    "DELETE FROM sse_tickets "
                    "WHERE ticket = $1 AND expires_at > NOW() "
                    "RETURNING user_id, username, role, instance_id, task_instance_id",
                    ticket);
                if (res.empty()) {
                    return true;  // 票据无效/已用过：正常业务结果，保持空
                }
                const auto& row = res[0];
                SseTicket found;
                found.user_id = row["user_id"].as<std::string>();
                found.username = row["username"].as<std::string>();
                found.role = row["role"].as<std::string>();
                found.instance_id = row["instance_id"].as<std::string>();
                found.task_instance_id = row["task_instance_id"].as<std::string>();
                consumed = std::move(found);
                return true;
            });
        if (!result.ok()) {
            // Fix #343: fail-closed——DB 异常时拒绝，不放行未授权访问
            spdlog::error("SseTicketStore: consume failed: {}", result.error());
            return std::nullopt;
        }
    } catch (const std::exception& e) {
        spdlog::error("SseTicketStore: consume DB error: {}", e.what());
        return std::nullopt;
    }
    return consumed;
}

}  // namespace taskflow::common::util
