#include "scheduler/grpc/heartbeat_checker.h"

#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/models/worker_info.h"

namespace taskflow::scheduler::grpc {

HeartbeatChecker::HeartbeatChecker(int check_interval, int timeout)
    : check_interval_(check_interval), timeout_(timeout) {}

void HeartbeatChecker::start() {
    running_ = true;
    thread_ = std::thread(&HeartbeatChecker::checkLoop, this);
}

void HeartbeatChecker::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HeartbeatChecker::checkLoop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::seconds(check_interval_));

        if (!running_) {
            break;
        }

        auto workers_result = worker_dao_.listAll();
        if (!workers_result.ok()) {
            spdlog::error("HeartbeatChecker: failed to list workers: {}",
                          workers_result.error());
            continue;
        }

        const auto& workers = workers_result.value();
        auto now = std::chrono::system_clock::now();

        for (const auto& worker : workers) {
            if (worker.status != "online") {
                continue;
            }

            // Parse last_heartbeat string "YYYY-MM-DD HH:MM:SS"
            std::tm tm = {};
            if (sscanf(worker.last_heartbeat.c_str(), "%d-%d-%d %d:%d:%d",
                       &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
                spdlog::warn(
                    "HeartbeatChecker: failed to parse heartbeat time for "
                    "worker {}: {}",
                    worker.id, worker.last_heartbeat);
                continue;
            }

            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            tm.tm_isdst = -1;

            auto last_hb = std::chrono::system_clock::from_time_t(
                std::mktime(&tm));

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               now - last_hb)
                               .count();

            if (elapsed > timeout_) {
                spdlog::info(
                    "HeartbeatChecker: worker {} heartbeat expired "
                    "(elapsed={}s, timeout={}s), marking offline",
                    worker.id, elapsed, timeout_);

                worker_dao_.updateStatus(worker.id, "offline");

                // Mark running task instances as NODE_OFFLINE
                auto instances_result =
                    task_instance_dao_.listByWorkflowInstance(
                        worker.id);
                if (instances_result.ok()) {
                    for (const auto& instance : instances_result.value()) {
                        if (instance.status == "RUNNING" ||
                            instance.status == "DISPATCHED") {
                            task_instance_dao_.markFinished(
                                instance.id, "NODE_OFFLINE",
                                -1, "Worker offline");
                        }
                    }
                }
            }
        }
    }
}

}  // namespace taskflow::scheduler::grpc
