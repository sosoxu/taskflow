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

            // Parse last_heartbeat string "YYYY-MM-DD HH:MM:SS" (possibly with timezone offset)
            // Fix #118: Use timegm to interpret as UTC to avoid timezone mismatch.
            // PostgreSQL TIMESTAMPTZ returned by pqxx may include a timezone offset
            // (e.g. "2026-06-18 12:34:56.789012+08"). sscanf parses the first 6
            // numeric fields and ignores the offset. Using mktime (local time)
            // would introduce an 8-hour skew if the DB session timezone differs
            // from the process timezone. timegm treats the parsed tm as UTC.
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
            tm.tm_isdst = 0;  // UTC has no DST

            // timegm interprets tm as UTC (not affected by process timezone)
            time_t t = timegm(&tm);
            if (t == -1) {
                spdlog::warn("HeartbeatChecker: timegm failed for worker {}", worker.id);
                continue;
            }
            auto last_hb = std::chrono::system_clock::from_time_t(t);

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               now - last_hb)
                               .count();

            if (elapsed > timeout_) {
                spdlog::info(
                    "HeartbeatChecker: worker {} heartbeat expired "
                    "(elapsed={}s, timeout={}s), marking offline",
                    worker.id, elapsed, timeout_);

                worker_dao_.updateStatus(worker.id, "offline");

                // Fix #186: Reset running_tasks to 0 when the worker goes
                // offline. The worker can no longer report heartbeats (which
                // carry the running_tasks count), so without this reset the
                // counter stays at its last value forever, inflating the load
                // seen by LoadBalanceDispatcher and preventing new tasks from
                // being dispatched to other workers.
                worker_dao_.updateRunningTasks(worker.id, 0);

                // Mark running task instances as NODE_OFFLINE
                auto instances_result =
                    task_instance_dao_.listByWorkerId(
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
