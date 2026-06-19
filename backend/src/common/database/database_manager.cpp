#include "common/database/database_manager.h"
#include <spdlog/spdlog.h>

namespace taskflow::common::database {

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager mgr;
    return mgr;
}

DatabaseManager::~DatabaseManager() {
    // Do NOT call shutdown() here - it can deadlock during static destruction.
    // Call shutdown() explicitly before main() returns instead.
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        while (!pool_.empty()) {
            pool_.pop();
        }
        cv_.notify_all();
    } catch (...) {
        // Best effort during destruction
    }
}

void DatabaseManager::init(const std::string& connection_string, int min_conn, int max_conn) {
    connectionString_ = connection_string;
    minConn_ = min_conn;
    maxConn_ = max_conn;

    spdlog::info("初始化数据库连接池: min={}, max={}", minConn_, maxConn_);

    for (int i = 0; i < minConn_; ++i) {
        try {
            auto conn = std::make_unique<pqxx::connection>(connectionString_);
            // Fix #186: Force UTC session timezone so TIMESTAMPTZ values are
            // always returned as UTC strings. Without this, HeartbeatChecker's
            // sscanf-based time parsing (which ignores the timezone offset)
            // would skew on non-UTC database deployments, breaking timeout
            // detection.
            pqxx::nontransaction tz_txn(*conn);
            tz_txn.exec("SET TIME ZONE 'UTC'");
            pool_.push(std::move(conn));
        } catch (const std::exception& e) {
            spdlog::error("创建数据库连接失败: {}", e.what());
            throw std::runtime_error("数据库连接初始化失败: " + std::string(e.what()));
        }
    }

    spdlog::info("数据库连接池初始化完成, 活跃连接: {}", static_cast<int>(pool_.size()));
}

std::unique_ptr<pqxx::connection> DatabaseManager::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (pool_.empty() && activeCount_ >= maxConn_) {
        if (cv_.wait_for(lock, std::chrono::seconds(30)) == std::cv_status::timeout) {
            spdlog::error("获取数据库连接超时");
            return nullptr;
        }
    }

    if (shutdown_) {
        return nullptr;
    }

    if (!pool_.empty()) {
        auto conn = std::move(pool_.front());
        pool_.pop();
        if (conn->is_open()) {
            activeCount_++;
            return conn;
        }
        // 连接已断开，创建新连接
    }

    // 池中无可用连接但未达上限，创建新连接
    try {
        auto conn = std::make_unique<pqxx::connection>(connectionString_);
        // Fix #186: Force UTC session timezone (see init() for rationale).
        pqxx::nontransaction tz_txn(*conn);
        tz_txn.exec("SET TIME ZONE 'UTC'");
        activeCount_++;
        return conn;
    } catch (const std::exception& e) {
        spdlog::error("创建新数据库连接失败: {}", e.what());
        return nullptr;
    }
}

void DatabaseManager::returnConnection(std::unique_ptr<pqxx::connection> conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_ || !conn->is_open()) {
        activeCount_--;
        return;
    }

    pool_.push(std::move(conn));
    activeCount_--;
    cv_.notify_one();
}

void DatabaseManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return;  // Already shut down
    shutdown_ = true;

    while (!pool_.empty()) {
        pool_.pop();
    }
    activeCount_ = 0;
    cv_.notify_all();

    spdlog::info("数据库连接池已关闭");
}

}  // namespace taskflow::common::database
