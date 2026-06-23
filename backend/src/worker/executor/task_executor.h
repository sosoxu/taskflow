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

    // Fix #318: Check if a specific task is still running.
    // Used by GetTaskLog follow mode to know when to stop streaming.
    bool isRunning(const std::string& task_instance_id) const;

    // Fix #124: Graceful shutdown — reject new submissions and wait for
    // running tasks to finish (up to timeout_seconds). If tasks are still
    // running after the timeout, they are cancelled. This is distinct from
    // the destructor, which always cancels immediately.
    void shutdown(int timeout_seconds = 30);

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
    // Fix #278: executor_factories_ 和 log_sink_ 需要独立锁保护，
    // 因为 createExecutor 在 worker 线程中调用，而 registerExecutor/setLogSink 可能在主线程并发调用
    std::mutex config_mutex_;
    std::map<std::string, std::function<std::unique_ptr<TaskExecutorBase>()>> executor_factories_;
    std::shared_ptr<LogSink> log_sink_;

    std::unique_ptr<TaskExecutorBase> createExecutor(const std::string& task_type);
};

}  // namespace taskflow::worker::executor
