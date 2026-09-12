#include "worker/executor/script_executor.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <time.h>
#endif
#include <chrono>
#include <fstream>
#include <spdlog/spdlog.h>

#include "common/util/uuid.h"

namespace taskflow::worker::executor {

// Fix #287: 验证实例 ID 不含路径分隔符或 ".."，防止路径穿越攻击
// 与 command_executor.cpp / log_sink.cpp 的 isValidInstanceId 保持一致
static bool isValidInstanceId(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id) {
        if (c == '/' || c == '\\' || c == '\0') return false;
    }
    if (id.find("..") != std::string::npos) return false;
    if (id == ".") return false;
    return true;
}

TaskResult ScriptExecutor::execute(const std::string& task_instance_id,
                                   const nlohmann::json& config,
                                   int timeout,
                                   const std::string& log_dir,
                                   std::function<void(pid_t)> pid_callback,
                                   LogSink* /*log_sink*/) {
    TaskResult result;

    // Fix #287: 校验 task_instance_id 防止路径穿越
    if (!isValidInstanceId(task_instance_id)) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Invalid task_instance_id contains path separators or traversal";
        return result;
    }

    if (!config.contains("script_content") ||
        !config["script_content"].is_string()) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Missing or invalid 'script_content' in config";
        return result;
    }

    std::string script_content = config["script_content"].get<std::string>();
    std::string interpreter = "bash";
    if (config.contains("interpreter") && config["interpreter"].is_string()) {
        interpreter = config["interpreter"].get<std::string>();
    }

    // Determine file extension based on interpreter
    std::string ext = ".sh";
    if (interpreter.find("python") != std::string::npos) {
        ext = ".py";
    } else if (interpreter == "node") {
        ext = ".js";
    } else if (interpreter == "perl") {
        ext = ".pl";
    } else if (interpreter == "ruby") {
        ext = ".rb";
    }

    // Fix #351: 文件名加入随机后缀并以 O_CREAT|O_EXCL 原子创建、权限 0700。
    // 此前为固定路径 /tmp/taskflow_script_<id>.ext + 0755：多用户共享主机上
    // 路径可预测，存在符号链接替换/抢注竞态，且残留脚本全局可执行。
    // 解释器按路径读取脚本内容，无需可执行位。
    const std::string script_uniq = common::util::generateUuid().substr(0, 8);
    std::string script_path =
        "/tmp/taskflow_script_" + task_instance_id + "_" + script_uniq + ext;
    std::string log_path = log_dir + "/" + task_instance_id + ".log";

    // Write script to temp file (O_EXCL: 已存在或遇符号链接即失败，无竞态窗口)
    {
        int fd = ::open(script_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0700);
        if (fd < 0) {
            result.status = "FAILED";
            result.exit_code = 1;
            result.error_message = "Failed to create script file (O_EXCL): " + script_path;
            return result;
        }
        bool write_ok = true;
        size_t offset = 0;
        while (offset < script_content.size()) {
            ssize_t n = ::write(fd, script_content.data() + offset,
                                script_content.size() - offset);
            if (n < 0) {
                if (errno == EINTR) continue;
                write_ok = false;
                break;
            }
            offset += static_cast<size_t>(n);
        }
        if (::close(fd) != 0) {
            write_ok = false;
        }
        if (!write_ok) {
            result.status = "FAILED";
            result.exit_code = 1;
            result.error_message = "Failed to write script file: " + script_path;
            std::remove(script_path.c_str());
            return result;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "fork() failed";
        std::remove(script_path.c_str());
        return result;
    }

    if (pid == 0) {
        // Child process
        // Fix #206: Create a new process group so timeout/cancel can kill the
        // whole group, cleaning up any subprocesses the interpreter may spawn.
        // Fix #294: 检查 setpgid 返回值，失败时退出子进程，防止 kill(-pid) 误杀 Worker
        if (setpgid(0, 0) != 0) {
            _exit(127);
        }

        // Fix #321: Ensure the whole process group is cleaned up when the
        // worker dies. PR_SET_PDEATHSIG kills only the direct child (this
        // process), not grandchildren. So we also fork a watchdog that
        // monitors this process: when it is reparented to init (parent died),
        // it kills the entire process group with SIGKILL.
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGKILL);

        pid_t watchdog_pid = fork();
        if (watchdog_pid < 0) {
            _exit(127);
        }
        if (watchdog_pid == 0) {
            while (getppid() != 1) {
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 100 * 1000 * 1000;  // 100ms
                nanosleep(&ts, nullptr);
            }
            kill(0, SIGKILL);
            _exit(0);
        }
#endif

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

        execlp(interpreter.c_str(), interpreter.c_str(), script_path.c_str(), nullptr);
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
            std::remove(script_path.c_str());
            return result;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (timeout > 0 && elapsed >= timeout) {
            // Fix #319: Send SIGTERM first for graceful shutdown, then SIGKILL
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
                    std::remove(script_path.c_str());
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
            std::remove(script_path.c_str());
            return result;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Clean up temp file
    std::remove(script_path.c_str());

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
                    ? "Script exited with code " + std::to_string(result.exit_code)
                    : last_lines;
            } else {
                result.error_message = "Script exited with code " + std::to_string(result.exit_code);
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
