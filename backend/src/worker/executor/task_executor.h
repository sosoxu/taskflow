#pragma once

#include <string>
#include <memory>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>

#include <nlohmann/json.hpp>
#include "common/result/result.h"

namespace taskflow::worker::executor {

struct TaskResult {
    std::string status;  // SUCCESS, FAILED, TIMEOUT
    int exit_code = 0;
    std::string error_message;
};

class TaskExecutorBase {
public:
    virtual ~TaskExecutorBase() = default;
    virtual TaskResult execute(const std::string& task_instance_id,
                               const nlohmann::json& config,
                               int timeout,
                               const std::string& log_dir) = 0;
};

class TaskExecutor {
public:
    explicit TaskExecutor(int max_tasks);

    // Execute a task, returns immediately if accepted, runs in background thread
    common::result::Result<void> submit(
        const std::string& task_instance_id,
        const std::string& task_type,
        const nlohmann::json& config,
        int timeout,
        const std::string& log_dir,
        std::function<void(const TaskResult&)> callback);

    // Cancel a running task
    common::result::Result<void> cancel(const std::string& task_instance_id);

    int runningCount() const;

private:
    int max_tasks_;
    std::atomic<int> running_count_{0};
    std::map<std::string, pid_t> running_processes_;
    std::mutex mutex_;

    std::unique_ptr<TaskExecutorBase> createExecutor(const std::string& task_type);
};

}  // namespace taskflow::worker::executor
