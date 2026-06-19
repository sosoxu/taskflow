#include "worker/executor/task_executor.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>

#include "worker/executor/command_executor.h"
#include "worker/executor/script_executor.h"
#include "worker/executor/sql_executor.h"

namespace taskflow::worker::executor {

TaskExecutor::TaskExecutor(int max_tasks) : max_tasks_(max_tasks) {}

TaskExecutor::~TaskExecutor() {
    // Mark as shutting down so submit() will reject new tasks
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutting_down_ = true;
    }

    // Cancel all running tasks to terminate child processes promptly
    std::vector<std::string> task_ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, pid] : running_processes_) {
            task_ids.push_back(id);
        }
    }
    for (const auto& id : task_ids) {
        cancel(id);
    }

    // Wait for all detached threads to finish so they don't access destroyed
    // member state (mutex_, running_processes_, running_count_).
    // We wait on active_threads_ (not running_count_) because the callback
    // runs after running_count_ is decremented.
    while (active_threads_.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

common::result::Result<void> TaskExecutor::submit(
    const std::string& task_instance_id,
    const std::string& task_type,
    const nlohmann::json& config,
    int timeout,
    const std::string& log_dir,
    const std::string& workflow_instance_id,
    std::function<void(const TaskResult&)> callback) {
    // Fix #116: Atomically check and increment running_count_ to prevent
    // the race condition where multiple threads pass the check before any
    // increments, causing max_tasks_ to be exceeded.
    int current = running_count_.load();
    while (current < max_tasks_) {
        if (running_count_.compare_exchange_weak(current, current + 1)) {
            break;  // Successfully reserved a slot
        }
        // current was updated by compare_exchange_weak; retry
    }
    if (current >= max_tasks_) {
        return common::result::Result<void>::failure(
            "Too many running tasks, max=" + std::to_string(max_tasks_));
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutting_down_) {
            running_count_.fetch_sub(1);
            return common::result::Result<void>::failure(
                "TaskExecutor is shutting down");
        }
        // Register a placeholder entry (pid=0) so cancel() can find the task
        // even before the executor thread reports the actual PID.
        running_processes_[task_instance_id] = 0;
    }

    auto executor = createExecutor(task_type);
    if (!executor) {
        std::lock_guard<std::mutex> lock(mutex_);
        running_processes_.erase(task_instance_id);
        running_count_.fetch_sub(1);
        return common::result::Result<void>::failure(
            "Unknown task type: " + task_type);
    }

    active_threads_.fetch_add(1);

    // Create a PID callback for the executor thread
    auto pid_callback = [this, task_instance_id](pid_t pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        running_processes_[task_instance_id] = pid;
    };

    // Capture log_sink as raw pointer (shared_ptr keeps it alive)
    auto* raw_log_sink = log_sink_.get();
    auto log_sink_ref = log_sink_;  // Keep a reference to prevent destruction

    std::thread([this, task_instance_id, executor = std::move(executor),
                 config, timeout, log_dir, workflow_instance_id,
                 callback = std::move(callback),
                 pid_callback = std::move(pid_callback),
                 raw_log_sink, log_sink_ref = std::move(log_sink_ref)]() mutable {
        TaskResult result = executor->execute(task_instance_id, config,
                                              timeout, log_dir,
                                              std::move(pid_callback),
                                              raw_log_sink);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_processes_.erase(task_instance_id);
        }

        // Decrement running_count_ BEFORE the callback so that
        // runningCount() reflects the actual task state when the callback
        // is invoked (tests check runningCount()==0 after done==true).
        running_count_.fetch_sub(1);
        cv_.notify_all();

        if (callback) {
            callback(result);
        }

        // Decrement active_threads_ LAST so the destructor doesn't return
        // until the callback has finished.
        active_threads_.fetch_sub(1);
    }).detach();

    return common::result::Result<void>();
}

common::result::Result<void> TaskExecutor::cancel(
    const std::string& task_instance_id) {
    pid_t pid = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = running_processes_.find(task_instance_id);
        if (it == running_processes_.end()) {
            return common::result::Result<void>::failure(
                "Task not found: " + task_instance_id);
        }
        pid = it->second;
    }

    // If the PID hasn't been registered yet (pid == 0), wait briefly for the
    // executor thread to report it. This handles the race between submit()
    // returning and the fork/exec happening in the worker thread.
    if (pid == 0) {
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = running_processes_.find(task_instance_id);
            if (it == running_processes_.end()) {
                // Task already finished
                return common::result::Result<void>::failure(
                    "Task already finished: " + task_instance_id);
            }
            pid = it->second;
            if (pid != 0) break;
        }
        if (pid == 0) {
            return common::result::Result<void>::failure(
                "Task PID not available: " + task_instance_id);
        }
    }

    // Send SIGTERM first for graceful termination.
    // Fix #206: Use negative pid to signal the whole process group, so that
    // grandchildren spawned by the shell / pqxx are also terminated.
    if (kill(-pid, SIGTERM) != 0) {
        // Process may have already exited; check if it's still alive
        if (kill(pid, 0) != 0) {
            return common::result::Result<void>();
        }
        return common::result::Result<void>::failure(
            "Failed to send SIGTERM to process " + std::to_string(pid));
    }

    // Wait up to 5 seconds for the process to terminate.
    // IMPORTANT: do NOT call waitpid() here — the executor thread owns the
    // child and must reap it to report the correct signal-terminated status.
    // Use kill(pid, 0) to poll for existence instead.
    for (int i = 0; i < 50; ++i) {
        if (kill(pid, 0) != 0) {
            // Process no longer exists (reaped by executor thread or gone)
            return common::result::Result<void>();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Still running after grace period, send SIGKILL to the whole group
    if (kill(-pid, SIGKILL) != 0 && kill(pid, 0) != 0) {
        return common::result::Result<void>::failure(
            "Failed to send SIGKILL to process " + std::to_string(pid));
    }

    return common::result::Result<void>();
}

int TaskExecutor::runningCount() const {
    return running_count_.load();
}

void TaskExecutor::shutdown(int timeout_seconds) {
    // Fix #124: Reject new submissions so running tasks can drain.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutting_down_ = true;
    }
    spdlog::info("TaskExecutor shutdown: waiting for {} running task(s) up to {}s",
                 running_count_.load(), timeout_seconds);

    // Wait for running tasks to finish naturally.
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(timeout_seconds);
    while (running_count_.load() > 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            spdlog::warn("TaskExecutor shutdown: {} task(s) still running after {}s, cancelling",
                         running_count_.load(), timeout_seconds);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Force-cancel any tasks that didn't finish in time.
    if (running_count_.load() > 0) {
        std::vector<std::string> task_ids;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, pid] : running_processes_) {
                task_ids.push_back(id);
            }
        }
        for (const auto& id : task_ids) {
            cancel(id);
        }
    }

    // Wait for all detached threads to finish so they don't access destroyed
    // member state during the destructor.
    while (active_threads_.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    spdlog::info("TaskExecutor shutdown complete");
}

std::unique_ptr<TaskExecutorBase> TaskExecutor::createExecutor(
    const std::string& task_type) {
    // Check registered factories first
    auto it = executor_factories_.find(task_type);
    if (it != executor_factories_.end()) {
        return it->second();
    }
    // Built-in types
    if (task_type == "command") {
        return std::make_unique<CommandExecutor>();
    } else if (task_type == "script") {
        return std::make_unique<ScriptExecutor>();
    } else if (task_type == "sql") {
        return std::make_unique<SqlExecutor>();
    }
    return nullptr;
}

void TaskExecutor::registerExecutor(
    const std::string& task_type,
    std::function<std::unique_ptr<TaskExecutorBase>()> factory) {
    executor_factories_[task_type] = std::move(factory);
    spdlog::info("TaskExecutor: registered custom executor for task type: {}", task_type);
}

void TaskExecutor::setLogSink(std::shared_ptr<LogSink> log_sink) {
    log_sink_ = std::move(log_sink);
    spdlog::info("TaskExecutor: log sink configured");
}

}  // namespace taskflow::worker::executor
