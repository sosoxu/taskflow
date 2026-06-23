#include "worker/executor/command_executor.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace taskflow::worker::executor {

// Fix #277: 验证实例 ID 不含路径分隔符或 ".."，防止路径穿越攻击
// 与 log_sink.cpp 的 isValidInstanceId 保持一致
static bool isValidInstanceId(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id) {
        if (c == '/' || c == '\\' || c == '\0') return false;
    }
    if (id.find("..") != std::string::npos) return false;
    if (id == ".") return false;
    return true;
}

TaskResult CommandExecutor::execute(const std::string& task_instance_id,
                                    const nlohmann::json& config,
                                    int timeout,
                                    const std::string& log_dir,
                                    std::function<void(pid_t)> pid_callback,
                                    LogSink* /*log_sink*/) {
    TaskResult result;

    if (!config.contains("command") || !config["command"].is_string()) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Missing or invalid 'command' in config";
        return result;
    }

    // Fix #277: 校验 task_instance_id 防止路径穿越（与 log_sink Fix #271 一致）
    if (!isValidInstanceId(task_instance_id)) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Invalid task_instance_id contains path separators or traversal";
        return result;
    }

    std::string log_path = log_dir + "/" + task_instance_id + ".log";

    // Use /bin/sh -c to support shell operators (&&, ||, |, ;, etc.)
    std::string full_cmd = config["command"].get<std::string>();

    char* args[] = {
        const_cast<char*>("/bin/sh"),
        const_cast<char*>("-c"),
        const_cast<char*>(full_cmd.c_str()),
        nullptr
    };

    if (full_cmd.empty()) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Empty command";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "fork() failed";
        return result;
    }

    if (pid == 0) {
        // Child process
        // Fix #206: Create a new process group so that on timeout/cancel we can
        // kill the entire group (kill(-pgid)) and reap any grandchildren the
        // shell may have spawned (pipelines, background jobs, etc.). Without
        // this, kill(pid) only terminates the direct /bin/sh child, leaving
        // grandchildren orphaned and still running.
        // Fix #294: 检查 setpgid 返回值，失败时退出子进程，防止 kill(-pid) 误杀 Worker
        if (setpgid(0, 0) != 0) {
            _exit(127);
        }

        int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            _exit(127);
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        // Set working directory
        if (config.contains("working_dir") && config["working_dir"].is_string()) {
            std::string working_dir = config["working_dir"].get<std::string>();
            if (!working_dir.empty()) {
                if (chdir(working_dir.c_str()) != 0) {
                    _exit(126);
                }
            }
        }

        // Set environment variables
        if (config.contains("env_vars") && config["env_vars"].is_object()) {
            for (auto it = config["env_vars"].begin(); it != config["env_vars"].end(); ++it) {
                std::string key = it.key();
                std::string value = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
                setenv(key.c_str(), value.c_str(), 1);
            }
        }

        execvp("/bin/sh", args);
        _exit(127);
    }

    // Parent process - report PID and wait with timeout
    if (pid_callback) {
        pid_callback(pid);
    }
    auto start = std::chrono::steady_clock::now();
    int status = 0;

    while (true) {
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            break;
        }
        if (ret < 0) {
            // Fix #301: EINTR 是可恢复错误，应当重试而非失败
            if (errno == EINTR) {
                continue;
            }
            result.status = "FAILED";
            result.exit_code = 1;
            result.error_message = "waitpid() failed";
            return result;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (timeout > 0 && elapsed >= timeout) {
            // Fix #319: Send SIGTERM first for graceful shutdown, then SIGKILL
            // after 2 seconds if the process hasn't exited. This gives child
            // processes a chance to clean up temp files and flush buffers.
            if (kill(-pid, SIGTERM) < 0) {
                kill(pid, SIGTERM);
            }
            // Wait up to 2 seconds for graceful exit
            for (int i = 0; i < 20; ++i) {
                pid_t ret = waitpid(pid, &status, WNOHANG);
                if (ret == pid) {
                    result.status = "TIMEOUT";
                    result.exit_code = -1;
                    result.error_message = "Task timed out after " +
                                           std::to_string(timeout) + " seconds";
                    return result;
                }
                if (ret < 0 && errno != EINTR) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            // Force kill if still alive
            if (kill(-pid, SIGKILL) < 0) {
                kill(pid, SIGKILL);
            }
            // Fix #301: 循环重试 waitpid 直到成功，避免 EINTR 导致僵尸进程
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            result.status = "TIMEOUT";
            result.exit_code = -1;
            result.error_message = "Task timed out after " +
                                   std::to_string(timeout) + " seconds";
            return result;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        result.status = (result.exit_code == 0) ? "SUCCESS" : "FAILED";
        if (result.exit_code != 0) {
            // Read last few lines of log for error message
            std::ifstream log_file(log_path);
            if (log_file.is_open()) {
                std::string line;
                std::string last_lines;
                int line_count = 0;
                while (std::getline(log_file, line)) {
                    if (!last_lines.empty()) last_lines += "\n";
                    last_lines += line;
                    line_count++;
                    if (line_count > 10) {
                        size_t pos = last_lines.find('\n');
                        if (pos != std::string::npos) {
                            last_lines = last_lines.substr(pos + 1);
                        }
                        line_count--;
                    }
                }
                result.error_message = last_lines.empty()
                    ? "Command exited with code " + std::to_string(result.exit_code)
                    : last_lines;
            } else {
                result.error_message = "Command exited with code " + std::to_string(result.exit_code);
            }
        }
    } else if (WIFSIGNALED(status)) {
        result.exit_code = -WTERMSIG(status);
        result.status = "FAILED";
        result.error_message = "Process killed by signal " +
                               std::to_string(WTERMSIG(status));
    } else {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Process terminated abnormally";
    }

    return result;
}

}  // namespace taskflow::worker::executor
