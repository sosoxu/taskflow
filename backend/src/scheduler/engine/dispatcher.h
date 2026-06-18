#pragma once

#include <random>
#include <string>
#include <vector>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "common/models/worker_info.h"
#include "common/result/result.h"

namespace taskflow::scheduler::engine {

class Dispatcher {
public:
    virtual ~Dispatcher() = default;
    virtual common::result::Result<common::models::WorkerInfo> selectWorker(
        const std::vector<common::models::WorkerInfo>& online_workers) = 0;
};

// Fix #125: Filter online workers by resource tags.
// A task's resource_tags (JSON array of strings, e.g. ["gpu","highmem"]) lists
// the tags a worker MUST have to be eligible. A worker is eligible if its
// resource_tags set is a superset of the task's required tags.
// If required_tags is empty, all workers are eligible (backward compatible).
inline std::vector<common::models::WorkerInfo> filterByResourceTags(
    const std::vector<common::models::WorkerInfo>& workers,
    const nlohmann::json& required_tags) {
    if (required_tags.empty() || !required_tags.is_array() || required_tags.size() == 0) {
        return workers;
    }
    std::unordered_set<std::string> required;
    for (const auto& tag : required_tags) {
        if (tag.is_string()) {
            required.insert(tag.get<std::string>());
        }
    }
    if (required.empty()) {
        return workers;
    }
    std::vector<common::models::WorkerInfo> filtered;
    for (const auto& worker : workers) {
        std::unordered_set<std::string> worker_tags;
        if (worker.resource_tags.is_array()) {
            for (const auto& tag : worker.resource_tags) {
                if (tag.is_string()) {
                    worker_tags.insert(tag.get<std::string>());
                }
            }
        }
        bool eligible = true;
        for (const auto& req : required) {
            if (worker_tags.find(req) == worker_tags.end()) {
                eligible = false;
                break;
            }
        }
        if (eligible) {
            filtered.push_back(worker);
        }
    }
    return filtered;
}

class RandomDispatcher : public Dispatcher {
public:
    common::result::Result<common::models::WorkerInfo> selectWorker(
        const std::vector<common::models::WorkerInfo>& online_workers) override {
        if (online_workers.empty()) {
            return common::result::Result<common::models::WorkerInfo>::failure(
                "没有在线 Worker");
        }
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dist(0, online_workers.size() - 1);
        return online_workers[dist(gen)];
    }
};

class LoadBalanceDispatcher : public Dispatcher {
public:
    common::result::Result<common::models::WorkerInfo> selectWorker(
        const std::vector<common::models::WorkerInfo>& online_workers) override {
        if (online_workers.empty()) {
            return common::result::Result<common::models::WorkerInfo>::failure(
                "没有在线 Worker");
        }
        size_t min_idx = 0;
        for (size_t i = 1; i < online_workers.size(); ++i) {
            if (online_workers[i].running_tasks < online_workers[min_idx].running_tasks) {
                min_idx = i;
            }
        }
        return online_workers[min_idx];
    }
};

class SpecifiedDispatcher : public Dispatcher {
public:
    explicit SpecifiedDispatcher(const std::string& worker_id) : worker_id_(worker_id) {}

    common::result::Result<common::models::WorkerInfo> selectWorker(
        const std::vector<common::models::WorkerInfo>& online_workers) override {
        for (const auto& worker : online_workers) {
            if (worker.id == worker_id_) {
                return worker;
            }
        }
        return common::result::Result<common::models::WorkerInfo>::failure(
            "指定的 Worker 不在线: " + worker_id_);
    }

private:
    std::string worker_id_;
};

}  // namespace taskflow::scheduler::engine
