#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <functional>
#include <pqxx/pqxx>

#include "common/result/result.h"

namespace taskflow::common::database {

class DatabaseManager {
public:
    static DatabaseManager& instance();

    void init(const std::string& connection_string, int min_conn, int max_conn);

    // 获取连接
    std::unique_ptr<pqxx::connection> getConnection();

    // 归还连接
    void returnConnection(std::unique_ptr<pqxx::connection> conn);

    // 执行事务辅助
    template<typename T>
    common::result::Result<T> withTransaction(std::function<common::result::Result<T>(pqxx::work&)> fn);

    // 执行只读事务辅助
    template<typename T>
    common::result::Result<T> withReadTransaction(std::function<common::result::Result<T>(pqxx::nontransaction&)> fn);

    void shutdown();

private:
    DatabaseManager() = default;
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    std::string connectionString_;
    int minConn_ = 5;
    int maxConn_ = 20;
    std::queue<std::unique_ptr<pqxx::connection>> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;
    int activeCount_ = 0;
};

// 模板方法实现
template<typename T>
common::result::Result<T> DatabaseManager::withTransaction(
    std::function<common::result::Result<T>(pqxx::work&)> fn) {
    auto conn = getConnection();
    if (!conn) {
        return common::result::Result<T>::failure("无法获取数据库连接");
    }

    // Fix #215: Ensure txn is destroyed (sending ROLLBACK if not committed)
    // BEFORE the connection is returned to the pool. Previously,
    // returnConnection was called while txn was still alive — if the
    // callback returned a failure Result (no commit), txn's destructor
    // would send ROLLBACK AFTER the connection was already reusable by
    // another thread, potentially aborting that thread's transaction.
    common::result::Result<T> result =
        common::result::Result<T>::failure("internal error");
    try {
        pqxx::work txn(*conn);
        result = fn(txn);
        if (result.ok()) {
            txn.commit();
        }
        // txn destructs at end of try block (normal path)
    } catch (const std::exception& e) {
        // txn already destructed during stack unwinding
        returnConnection(std::move(conn));
        return common::result::Result<T>::failure(std::string("事务执行失败: ") + e.what());
    }
    // txn is now destroyed; safe to return the connection
    returnConnection(std::move(conn));
    return result;
}

template<typename T>
common::result::Result<T> DatabaseManager::withReadTransaction(
    std::function<common::result::Result<T>(pqxx::nontransaction&)> fn) {
    auto conn = getConnection();
    if (!conn) {
        return common::result::Result<T>::failure("无法获取数据库连接");
    }

    // Fix #215: Same pattern — destroy txn before returning connection.
    common::result::Result<T> result =
        common::result::Result<T>::failure("internal error");
    try {
        pqxx::nontransaction txn(*conn);
        result = fn(txn);
        // nontransaction has no ROLLBACK, but keep the pattern consistent
    } catch (const std::exception& e) {
        returnConnection(std::move(conn));
        return common::result::Result<T>::failure(std::string("查询执行失败: ") + e.what());
    }
    returnConnection(std::move(conn));
    return result;
}

}  // namespace taskflow::common::database
