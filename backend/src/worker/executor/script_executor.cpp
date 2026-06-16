#include "worker/executor/script_executor.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <fstream>
#include <spdlog/spdlog.h>

namespace taskflow::worker::executor {

TaskResult ScriptExecutor::execute(const std::string& task_instance_id,
                                   const nlohmann::json& config,
                                   int timeout,
                                   const std::string& log_dir) {
    TaskResult result;

    if (!config.contains("script_content") ||
        !config["script_content"].is_string()) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Missing or invalid 'script_content' in config";
        return result;
    }

    std::string script_content = config["script_content"].get<std::string>();
    std::string script_path =
        "/tmp/taskflow_script_" + task_instance_id + ".sh";
    std::string log_path = log_dir + "/" + task_instance_id + ".log";

    // Write script to temp file
    {
        std::ofstream ofs(script_path);
        if (!ofs) {
            result.status = "FAILED";
            result.exit_code = 1;
            result.error_message = "Failed to write script file: " + script_path;
            return result;
        }
        ofs << script_content;
        ofs.close();
    }

    // Make executable
    if (chmod(script_path.c_str(), 0755) != 0) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Failed to chmod script file: " + script_path;
        std::remove(script_path.c_str());
        return result;
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
        int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            _exit(127);
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        execl("/bin/bash", "bash", script_path.c_str(), nullptr);
        _exit(127);
    }

    // Parent process - wait with timeout
    auto start = std::chrono::steady_clock::now();
    int status = 0;

    while (true) {
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            break;
        }
        if (ret < 0) {
            result.status = "FAILED";
            result.exit_code = 1;
            result.error_message = "waitpid() failed";
            std::remove(script_path.c_str());
            return result;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (timeout > 0 && elapsed >= timeout) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
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
