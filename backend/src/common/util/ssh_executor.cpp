#include "common/util/ssh_executor.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace taskflow::common::util {

namespace {

// Escape a string so it can be safely embedded inside single quotes in a
// POSIX shell command. Each single quote becomes '\'' (close quote, escaped
// quote, reopen quote).
std::string shellSingleQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    return "'" + out + "'";
}

// Run a shell command via /bin/sh -c, optionally feeding stdin_data, capturing
// combined stdout+stderr, with a timeout. On timeout the child process group
// is killed.
SshCommandResult runShell(const std::string& command,
                          const std::string& stdin_data,
                          int timeout_sec) {
    int out_pipe[2];
    if (pipe(out_pipe) != 0) {
        return {-1, std::string("pipe() failed: ") + std::strerror(errno), false};
    }

    bool need_stdin = !stdin_data.empty();
    int in_pipe[2] = {-1, -1};
    if (need_stdin) {
        if (pipe(in_pipe) != 0) {
            close(out_pipe[0]);
            close(out_pipe[1]);
            return {-1, std::string("pipe() failed: ") + std::strerror(errno), false};
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (need_stdin) {
            close(in_pipe[0]);
            close(in_pipe[1]);
        }
        return {-1, std::string("fork() failed: ") + std::strerror(errno), false};
    }

    if (pid == 0) {
        // Child: put ourselves in a new process group so timeout kills all
        // descendants (ssh + remote processes).
        setpgid(0, 0);

        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);  // merge stderr into stdout
        close(out_pipe[0]);
        close(out_pipe[1]);

        if (need_stdin) {
            dup2(in_pipe[0], STDIN_FILENO);
            close(in_pipe[0]);
            close(in_pipe[1]);
        } else {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }

        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    // Parent
    close(out_pipe[1]);
    if (need_stdin) {
        close(in_pipe[0]);
    }

    if (need_stdin) {
        // Config files are small, so a blocking write is safe.
        const char* data = stdin_data.data();
        size_t remaining = stdin_data.size();
        while (remaining > 0) {
            ssize_t n = write(in_pipe[1], data, remaining);
            if (n <= 0) {
                if (errno == EINTR) continue;
                break;
            }
            data += n;
            remaining -= static_cast<size_t>(n);
        }
        close(in_pipe[1]);
    }

    std::string output;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    bool timed_out = false;
    char buf[4096];

    while (true) {
        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                .count());

        struct pollfd pfd;
        pfd.fd = out_pipe[0];
        pfd.events = POLLIN;
        pfd.revents = 0;

        int wait_ms = remaining_ms > 0 ? remaining_ms : 0;
        int pr = poll(&pfd, 1, wait_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0 && remaining_ms <= 0) {
            timed_out = true;
            break;
        }
        if (pfd.revents & POLLIN) {
            ssize_t n = read(out_pipe[0], buf, sizeof(buf));
            if (n > 0) {
                output.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
                break;  // EOF
            } else if (errno != EINTR) {
                break;
            }
        } else if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            // Drain any remaining bytes then exit.
            ssize_t n;
            while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
                output.append(buf, static_cast<size_t>(n));
            }
            break;
        }
    }

    close(out_pipe[0]);

    if (timed_out) {
        // Kill the whole process group.
        kill(-pid, SIGTERM);
        // Give it a moment, then SIGKILL if still alive.
        for (int i = 0; i < 10; ++i) {
            int status = 0;
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) {
                pid = -1;
                break;
            }
            usleep(100 * 1000);  // 100ms
        }
        if (pid != -1) {
            kill(-pid, SIGKILL);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return {124, output, true};
    }

    int status = 0;
    waitpid(pid, &status, 0);

    int exit_code = -1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    return {exit_code, output, false};
}

}  // namespace

SshExecutor::SshExecutor(std::string host, int port, std::string username, std::string password)
    : host_(std::move(host)),
      port_(port > 0 ? port : 22),
      username_(std::move(username)),
      password_(std::move(password)) {}

SshExecutor::~SshExecutor() {
    cleanupAskpass();
}

common::result::Result<void> SshExecutor::ensureAskpass() {
    if (!askpass_path_.empty()) {
        return common::result::Result<void>();
    }

    // Create a temp file for the askpass helper script.
    char tmpl[] = "/tmp/taskflow_askpass_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return common::result::Result<void>::failure(
            std::string("mkstemp failed: ") + std::strerror(errno));
    }

    // Build script body. Use printf '%s\n' so the password is treated as a
    // data argument (no format-string injection). Escape single quotes.
    std::string escaped;
    escaped.reserve(password_.size() + 8);
    for (char c : password_) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    std::string script = "#!/bin/sh\nprintf '%s\\n' '" + escaped + "'\n";

    ssize_t total = 0;
    while (static_cast<size_t>(total) < script.size()) {
        ssize_t n = write(fd, script.data() + total, script.size() - total);
        if (n <= 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmpl);
            return common::result::Result<void>::failure(
                std::string("write askpass failed: ") + std::strerror(errno));
        }
        total += n;
    }
    close(fd);

    if (chmod(tmpl, 0700) != 0) {
        unlink(tmpl);
        return common::result::Result<void>::failure(
            std::string("chmod askpass failed: ") + std::strerror(errno));
    }

    askpass_path_ = tmpl;
    return common::result::Result<void>();
}

void SshExecutor::cleanupAskpass() {
    if (!askpass_path_.empty()) {
        unlink(askpass_path_.c_str());
        askpass_path_.clear();
    }
}

std::string SshExecutor::buildSshPrefix(int connect_timeout) const {
    // Env vars are set inline (VAR=val ...) so they apply only to this
    // invocation, keeping the call thread-safe.
    std::string prefix =
        "SSH_ASKPASS=" + askpass_path_ +
        " SSH_ASKPASS_REQUIRE=force DISPLAY=:0"
        " setsid -w ssh"
        " -p " + std::to_string(port_) +
        " -o StrictHostKeyChecking=no"
        " -o UserKnownHostsFile=/dev/null"
        " -o ConnectTimeout=" + std::to_string(connect_timeout) +
        " -o LogLevel=ERROR"
        " -o PreferredAuthentications=password"
        " -o PubkeyAuthentication=no"
        " " + shellSingleQuote(username_ + "@" + host_);
    return prefix;
}

common::result::Result<void> SshExecutor::testConnection(int timeout_sec) {
    auto ask = ensureAskpass();
    if (!ask.ok()) {
        return ask;
    }

    std::string cmd = buildSshPrefix(timeout_sec) + " 'echo taskflow_ssh_ok' 2>&1";
    auto res = runShell(cmd, "", timeout_sec + 5);

    if (res.timed_out) {
        return common::result::Result<void>::failure(
            "SSH 连接超时: " + host_ + ":" + std::to_string(port_));
    }
    if (res.exit_code != 0) {
        return common::result::Result<void>::failure(
            "SSH 连接失败 (exit=" + std::to_string(res.exit_code) + "): " + res.output);
    }
    if (res.output.find("taskflow_ssh_ok") == std::string::npos) {
        return common::result::Result<void>::failure(
            "SSH 连接返回异常: " + res.output);
    }
    return common::result::Result<void>();
}

common::result::Result<SshCommandResult> SshExecutor::execute(
    const std::string& remote_command, int timeout_sec) {
    auto ask = ensureAskpass();
    if (!ask.ok()) {
        return common::result::Result<SshCommandResult>::failure(ask.error());
    }

    std::string cmd = buildSshPrefix(15) + " " + shellSingleQuote(remote_command) + " 2>&1";
    auto res = runShell(cmd, "", timeout_sec);

    if (res.timed_out) {
        res.output = "命令执行超时 (" + std::to_string(timeout_sec) + "s): " + res.output;
    }
    return res;
}

common::result::Result<void> SshExecutor::writeFile(
    const std::string& remote_path,
    const std::string& content,
    const std::string& permissions,
    int timeout_sec) {
    auto ask = ensureAskpass();
    if (!ask.ok()) {
        return ask;
    }

    // Feed content via SSH stdin to a remote `cat`, then chmod.
    std::string remote_cmd =
        "cat > " + shellSingleQuote(remote_path) +
        " && chmod " + permissions + " " + shellSingleQuote(remote_path);
    std::string cmd = buildSshPrefix(15) + " " + shellSingleQuote(remote_cmd) + " 2>&1";

    auto res = runShell(cmd, content, timeout_sec);

    if (res.timed_out) {
        return common::result::Result<void>::failure(
            "写入远程文件超时: " + remote_path);
    }
    if (res.exit_code != 0) {
        return common::result::Result<void>::failure(
            "写入远程文件失败 (exit=" + std::to_string(res.exit_code) + "): " + res.output);
    }
    return common::result::Result<void>();
}

}  // namespace taskflow::common::util
