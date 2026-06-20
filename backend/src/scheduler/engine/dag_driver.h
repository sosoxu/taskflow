#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "common/config/scheduler_config.h"
#include "scheduler/dao/task_dao.h"
#include "scheduler/dao/task_instance_dao.h"
#include "scheduler/dao/worker_dao.h"
#include "scheduler/dao/workflow_dao.h"
#include "scheduler/dao/workflow_instance_dao.h"
#include "scheduler/engine/dag_engine.h"
#include "scheduler/engine/dispatcher.h"
#include "scheduler/grpc/leader_election.h"

namespace taskflow::scheduler::engine {

class DagDriver {
public:
    DagDriver(int drive_interval, const std::string& aes_key,
              std::shared_ptr<grpc::LeaderElection> leader_election,
              common::config::TlsConfig worker_tls = {});

    void start();
    void stop();

    // Fix #275: 将 resolveString/resolvePlaceholders 改为 public inline 以便单元测试
    // 且无需编译 dag_driver.cpp（避免 gRPC/DAO 依赖）
    // Resolve ${var} placeholders in string using parameter values
    static std::string resolveString(const std::string& input, const nlohmann::json& params) {
        std::string result;
        size_t i = 0;
        while (i < input.size()) {
            if (i + 1 < input.size() && input[i] == '$' && input[i + 1] == '{') {
                size_t end = input.find('}', i + 2);
                if (end != std::string::npos) {
                    std::string var_name = input.substr(i + 2, end - i - 2);
                    if (params.contains(var_name)) {
                        if (params[var_name].is_string()) {
                            result += params[var_name].get<std::string>();
                        } else {
                            result += params[var_name].dump();
                        }
                    } else {
                        result += input.substr(i, end - i + 1);
                    }
                    i = end + 1;
                } else {
                    result += input[i];
                    i++;
                }
            } else {
                result += input[i];
                i++;
            }
        }
        return result;
    }

    // Resolve ${var} placeholders in JSON config using parameter values
    static void resolvePlaceholders(nlohmann::json& config, const nlohmann::json& params) {
        if (config.is_string()) {
            std::string str_val = config.get<std::string>();
            config = resolveString(str_val, params);
        } else if (config.is_object()) {
            for (auto& [key, value] : config.items()) {
                resolvePlaceholders(value, params);
            }
        } else if (config.is_array()) {
            for (auto& item : config) {
                resolvePlaceholders(item, params);
            }
        }
    }

private:
    void driveLoop();
    void driveInstance(const common::models::WorkflowInstance& instance);
    common::result::Result<void> dispatchTask(
        const common::models::TaskInstance& task_instance,
        const common::models::Workflow& workflow);

    int drive_interval_;
    std::string aes_key_;
    std::shared_ptr<grpc::LeaderElection> leader_election_;
    // Fix #126: TLS config for scheduler→worker gRPC channels.
    common::config::TlsConfig worker_tls_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    dao::WorkflowInstanceDao workflow_instance_dao_;
    dao::TaskInstanceDao task_instance_dao_;
    dao::WorkerDao worker_dao_;
    dao::TaskDao task_dao_;
    dao::WorkflowDao workflow_dao_;
};

}  // namespace taskflow::scheduler::engine
