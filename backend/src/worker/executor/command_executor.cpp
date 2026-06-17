#include "worker/executor/command_executor.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace taskflow::worker::executor {

TaskResult CommandExecutor::execute(const std::string& task_instance_id,
                                    const nlohmann::json& config,
                                    int timeout,
                                    const std::string& log_dir) {
    TaskResult result;

    if (!config.contains("command") || !config["command"].is_string()) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Missing or invalid 'command' in config";
        return result;
    }

    std::string command = config["command"].get<std::string>();
    std::string log_path = log_dir + "/" + task_instance_id + ".log";

    // Split command into argv array for execvp (avoid shell injection)
    std::vector<std::string> args;
    std::istringstream iss(command);
    std::string token;
    while (iss >> token) {
        args.push_back(token);
    }

    if (args.empty()) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Empty command";
        return result;
    }

    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "fork() failed";
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

        execvp(argv[0], argv.data());
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
