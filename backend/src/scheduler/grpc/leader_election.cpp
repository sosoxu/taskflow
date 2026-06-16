#include "scheduler/grpc/leader_election.h"

#include <chrono>
#include <string>

#include <pqxx/pqxx>

#include "common/database/database_manager.h"

namespace taskflow::scheduler::grpc {

LeaderElection::LeaderElection(int lease_interval, int lock_id)
    : lease_interval_(lease_interval), lock_id_(lock_id) {}

void LeaderElection::start() {
    running_ = true;
    thread_ = std::thread(&LeaderElection::electionLoop, this);
    spdlog::info("LeaderElection started with lock_id={}", lock_id_);
}

void LeaderElection::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }

    // Release the lock if we are the leader
    if (leader_) {
        try {
            auto conn = common::database::DatabaseManager::instance().getConnection();
            if (conn) {
                pqxx::nontransaction txn(*conn);
                txn.exec_params("SELECT pg_advisory_unlock($1)", lock_id_);
                common::database::DatabaseManager::instance().returnConnection(
                    std::move(conn));
            }
        } catch (const std::exception& e) {
            spdlog::warn("LeaderElection: failed to release lock on stop: {}",
                         e.what());
        }
        leader_ = false;
    }

    spdlog::info("LeaderElection stopped");
}

bool LeaderElection::isLeader() const {
    return leader_.load();
}

void LeaderElection::electionLoop() {
    while (running_) {
        if (leader_) {
            // Renew: unlock then re-lock
            try {
                auto conn =
                    common::database::DatabaseManager::instance().getConnection();
                if (conn) {
                    pqxx::nontransaction txn(*conn);
                    txn.exec_params("SELECT pg_advisory_unlock($1)", lock_id_);
                    auto res = txn.exec_params(
                        "SELECT pg_try_advisory_lock($1)", lock_id_);
                    bool acquired = res[0][0].as<bool>();
                    common::database::DatabaseManager::instance().returnConnection(
                        std::move(conn));

                    if (!acquired) {
                        leader_ = false;
                        spdlog::warn(
                            "LeaderElection: failed to renew advisory lock, "
                            "lost leadership");
                    }
                } else {
                    leader_ = false;
                    spdlog::warn(
                        "LeaderElection: no DB connection, lost leadership");
                }
            } catch (const std::exception& e) {
                leader_ = false;
                spdlog::error(
                    "LeaderElection: exception during lock renewal: {}",
                    e.what());
            }
        } else {
            // Try to acquire
            if (tryAcquireLock()) {
                leader_ = true;
                spdlog::info(
                    "LeaderElection: acquired advisory lock, now leader");
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(lease_interval_));
    }
}

bool LeaderElection::tryAcquireLock() {
    try {
        auto conn = common::database::DatabaseManager::instance().getConnection();
        if (!conn) {
            return false;
        }

        pqxx::nontransaction txn(*conn);
        auto res =
            txn.exec_params("SELECT pg_try_advisory_lock($1)", lock_id_);
        bool acquired = res[0][0].as<bool>();

        common::database::DatabaseManager::instance().returnConnection(
            std::move(conn));

        return acquired;
    } catch (const std::exception& e) {
        spdlog::error("LeaderElection: exception trying to acquire lock: {}",
                      e.what());
        return false;
    }
}

}  // namespace taskflow::scheduler::grpc
