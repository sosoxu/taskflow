#pragma once

#include <string>
#include <memory>
#include <map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>
#include "common/result/result.h"
#include "worker/executor/log_sink.h"

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
                               const std::string& log_dir,
                               std::function<void(pid_t)> pid_callback = nullptr,
                               LogSink* log_sink = nullptr) = 0;
};

class TaskExecutor {
public:
    explicit TaskExecutor(int max_tasks);

    // Destructor: cancels all running tasks and waits for threads to finish
    // to avoid use-after-free from detached threads accessing destroyed state.
    ~TaskExecutor();

    // Execute a task, returns immediately if accepted, runs in background thread
    common::result::Result<void> submit(
        const std::string& task_instance_id,
        const std::string& task_type,
        const nlohmann::json& config,
        int timeout,
        const std::string& log_dir,
        const std::string& workflow_instance_id,
        std::function<void(const TaskResult&)> callback);

    // Cancel a running task (sends SIGTERM, then SIGKILL after grace period)
    common::result::Result<void> cancel(const std::string& task_instance_id);

    int runningCount() const;

    // Register a custom task type executor
    void registerExecutor(const std::string& task_type,
                          std::function<std::unique_ptr<TaskExecutorBase>()> factory);

    // Set the log sink for task execution
    void setLogSink(std::shared_ptr<LogSink> log_sink);

private:
    int max_tasks_;
    std::atomic<int> running_count_{0};
    // Tracks detached threads that are still alive (including callback execution).
    // The destructor waits for this to reach 0 to avoid use-after-free.
    std::atomic<int> active_threads_{0};
    // Map of task_instance_id -> pid. pid=0 means task submitted but PID not yet known.
    std::map<std::string, pid_t> running_processes_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutting_down_{false};
    std::map<std::string, std::function<std::unique_ptr<TaskExecutorBase>()>> executor_factories_;
    std::shared_ptr<LogSink> log_sink_;

    std::unique_ptr<TaskExecutorBase> createExecutor(const std::string& task_type);
};

}  // namespace taskflow::worker::executor
