#pragma once

#include <string>
#include "common/result/result.h"

namespace taskflow::common::util {

// Result of a remote command execution.
struct SshCommandResult {
    int exit_code = -1;
    std::string output;  // combined stdout + stderr
    bool timed_out = false;
};

// SSHExecutor: executes commands and writes files on a remote host over SSH
// using the system `ssh` client with password authentication.
//
// Password authentication is implemented via the SSH_ASKPASS mechanism
// (SSH_ASKPASS_REQUIRE=force, available in OpenSSH >= 8.4): a temporary
// askpass helper script echoes the password when ssh requests it. This avoids
// a hard dependency on libssh2/libssh or sshpass, which may not be packaged in
// every deployment environment.
class SshExecutor {
public:
    SshExecutor(std::string host, int port, std::string username, std::string password);
    ~SshExecutor();

    SshExecutor(const SshExecutor&) = delete;
    SshExecutor& operator=(const SshExecutor&) = delete;

    // Test SSH connectivity by running a trivial remote command.
    common::result::Result<void> testConnection(int timeout_sec = 15);

    // Execute a remote command. Returns combined stdout+stderr and exit code.
    common::result::Result<SshCommandResult> execute(const std::string& remote_command,
                                                      int timeout_sec = 60);

    // Write `content` to `remote_path` on the remote host, optionally setting
    // file permissions (e.g. "644"). The content is streamed over SSH stdin to
    // a remote `cat`, so no scp binary is required.
    common::result::Result<void> writeFile(const std::string& remote_path,
                                           const std::string& content,
                                           const std::string& permissions = "644",
                                           int timeout_sec = 30);

private:
    std::string host_;
    int port_;
    std::string username_;
    std::string password_;
    std::string askpass_path_;

    // Create the temporary askpass helper script. Called lazily.
    common::result::Result<void> ensureAskpass();
    void cleanupAskpass();

    // Build the ssh invocation prefix (env + ssh options) up to user@host.
    std::string buildSshPrefix(int connect_timeout) const;
};

}  // namespace taskflow::common::util
