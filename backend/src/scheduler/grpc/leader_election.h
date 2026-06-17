#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

#include "common/database/database_manager.h"

namespace taskflow::scheduler::grpc {

class LeaderElection {
public:
    LeaderElection(int lease_interval, int lock_id);

    void start();
    void stop();

    bool isLeader() const;

private:
    void electionLoop();
    bool tryAcquireLock();
    bool renewLock();
    void ensureConnection();

    int lease_interval_;  // seconds between lease renewals
    int lock_id_;         // advisory lock ID
    std::atomic<bool> leader_{false};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::unique_ptr<pqxx::connection> lock_conn_;  // Dedicated connection for advisory lock
};

}  // namespace taskflow::scheduler::grpc
