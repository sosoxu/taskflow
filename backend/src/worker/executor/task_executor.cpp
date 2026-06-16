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

common::result::Result<void> TaskExecutor::submit(
    const std::string& task_instance_id,
    const std::string& task_type,
    const nlohmann::json& config,
    int timeout,
    const std::string& log_dir,
    std::function<void(const TaskResult&)> callback) {
    if (running_count_.load() >= max_tasks_) {
        return common::result::Result<void>::failure(
            "Too many running tasks, max=" + std::to_string(max_tasks_));
    }

    auto executor = createExecutor(task_type);
    if (!executor) {
        return common::result::Result<void>::failure(
            "Unknown task type: " + task_type);
    }

    running_count_.fetch_add(1);

    std::thread([this, task_instance_id, executor = std::move(executor),
                 config, timeout, log_dir,
                 callback = std::move(callback)]() mutable {
        TaskResult result = executor->execute(task_instance_id, config,
                                              timeout, log_dir);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_processes_.erase(task_instance_id);
        }

        running_count_.fetch_sub(1);

        if (callback) {
            callback(result);
        }
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

    if (kill(pid, SIGTERM) != 0) {
        return common::result::Result<void>::failure(
            "Failed to send SIGTERM to process " + std::to_string(pid));
    }

    // Wait up to 5 seconds for the process to terminate
    for (int i = 0; i < 50; ++i) {
        int status = 0;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            return common::result::Result<void>();
        }
        if (ret < 0) {
            // Process already gone
            return common::result::Result<void>();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Process still running, send SIGKILL
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);

    return common::result::Result<void>();
}

int TaskExecutor::runningCount() const {
    return running_count_.load();
}

std::unique_ptr<TaskExecutorBase> TaskExecutor::createExecutor(
    const std::string& task_type) {
    if (task_type == "command") {
        return std::make_unique<CommandExecutor>();
    } else if (task_type == "script") {
        return std::make_unique<ScriptExecutor>();
    } else if (task_type == "sql") {
        return std::make_unique<SqlExecutor>();
    }
    return nullptr;
}

}  // namespace taskflow::worker::executor
