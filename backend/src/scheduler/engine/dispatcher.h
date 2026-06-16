#pragma once

#include <random>
#include <string>
#include <vector>

#include "common/models/worker_info.h"
#include "common/result/result.h"

namespace taskflow::scheduler::engine {

class Dispatcher {
public:
    virtual ~Dispatcher() = default;
    virtual common::result::Result<common::models::WorkerInfo> selectWorker(
        const std::vector<common::models::WorkerInfo>& online_workers) = 0;
};

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
