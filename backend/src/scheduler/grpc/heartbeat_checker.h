#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "scheduler/dao/worker_dao.h"
#include "scheduler/dao/task_instance_dao.h"

namespace taskflow::scheduler::grpc {

class HeartbeatChecker {
public:
    HeartbeatChecker(int check_interval, int timeout);

    void start();
    void stop();

private:
    void checkLoop();
    int check_interval_;  // seconds
    int timeout_;         // seconds
    std::atomic<bool> running_{false};
    std::thread thread_;
    taskflow::scheduler::dao::WorkerDao worker_dao_;
    taskflow::scheduler::dao::TaskInstanceDao task_instance_dao_;
};

}  // namespace taskflow::scheduler::grpc
