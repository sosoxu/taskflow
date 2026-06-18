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
    if (leader_ && lock_conn_ && lock_conn_->is_open()) {
        try {
            pqxx::nontransaction txn(*lock_conn_);
            txn.exec_params("SELECT pg_advisory_unlock($1)", lock_id_);
        } catch (const std::exception& e) {
            spdlog::warn("LeaderElection: failed to release lock on stop: {}",
                         e.what());
        }
        leader_ = false;
    }

    // Close the dedicated connection
    if (lock_conn_) {
        try {
            lock_conn_->close();
        } catch (...) {
        }
        lock_conn_.reset();
    }

    spdlog::info("LeaderElection stopped");
}

bool LeaderElection::isLeader() const {
    return leader_.load();
}

void LeaderElection::ensureConnection() {
    if (!lock_conn_ || !lock_conn_->is_open()) {
        auto conn = common::database::DatabaseManager::instance().getConnection();
        // Keep this connection dedicated for advisory lock - do NOT return to pool
        lock_conn_ = std::move(conn);
    }
}

bool LeaderElection::tryAcquireLock() {
    try {
        ensureConnection();
        if (!lock_conn_ || !lock_conn_->is_open()) {
            return false;
        }

        pqxx::nontransaction txn(*lock_conn_);
        auto res =
            txn.exec_params("SELECT pg_try_advisory_lock($1)", lock_id_);
        bool acquired = res[0][0].as<bool>();
        return acquired;
    } catch (const std::exception& e) {
        spdlog::error("LeaderElection: exception trying to acquire lock: {}",
                      e.what());
        // Reset connection on error
        if (lock_conn_) {
            try { lock_conn_->close(); } catch (...) {}
            lock_conn_.reset();
        }
        return false;
    }
}

bool LeaderElection::renewLock() {
    // Advisory locks are session-level: as long as the connection stays open,
    // the lock is held. Fix #115: If the connection was lost and ensureConnection()
    // opened a NEW connection, that new connection does NOT hold the advisory lock.
    // We must detect this and re-acquire the lock rather than assuming it's held.
    try {
        bool was_connection_lost = (!lock_conn_ || !lock_conn_->is_open());
        ensureConnection();
        if (!lock_conn_ || !lock_conn_->is_open()) {
            return false;
        }

        if (was_connection_lost) {
            // Connection was lost and a new one was opened by ensureConnection().
            // The new connection does NOT hold the advisory lock — re-acquire it.
            spdlog::warn("LeaderElection: connection was lost, re-acquiring advisory lock");
            pqxx::nontransaction txn(*lock_conn_);
            auto res = txn.exec_params("SELECT pg_try_advisory_lock($1)", lock_id_);
            bool acquired = res[0][0].as<bool>();
            if (!acquired) {
                // Another node holds the lock; we are no longer the leader.
                spdlog::info("LeaderElection: failed to re-acquire lock, another node is leader");
                return false;
            }
            return true;
        }

        // Connection is the same one that acquired the lock — verify it's alive.
        pqxx::nontransaction txn(*lock_conn_);
        auto res = txn.exec("SELECT 1");
        (void)res;
        return true;
    } catch (const std::exception& e) {
        spdlog::error("LeaderElection: connection lost during renewal: {}",
                      e.what());
        // Connection lost means lock is also lost
        if (lock_conn_) {
            try { lock_conn_->close(); } catch (...) {}
            lock_conn_.reset();
        }
        return false;
    }
}

void LeaderElection::electionLoop() {
    while (running_) {
        if (leader_) {
            // Renew: just check the dedicated connection is still alive
            if (!renewLock()) {
                leader_ = false;
                spdlog::warn(
                    "LeaderElection: lost advisory lock (connection lost)");
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

}  // namespace taskflow::scheduler::grpc
