#include "worker/executor/sql_executor.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <spdlog/spdlog.h>
#include <pqxx/pqxx>

namespace taskflow::worker::executor {

// Fix #288: 验证实例 ID 不含路径分隔符或 ".."，防止路径穿越攻击
static bool isValidInstanceId(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id) {
        if (c == '/' || c == '\\' || c == '\0') return false;
    }
    if (id.find("..") != std::string::npos) return false;
    if (id == ".") return false;
    return true;
}

// Fix #288: 转义 libpq 连接串中的特殊字符（空格、单引号、反斜杠）
// 与 DatabaseConfig::connectionString() 的转义逻辑保持一致（Fix #281）
static std::string escapeConnValue(const std::string& val) {
    if (val.empty()) return "''";
    bool need_quote = false;
    for (char c : val) {
        if (c == ' ' || c == '\'' || c == '\\' || c == '\t' || c == '\n') {
            need_quote = true;
            break;
        }
    }
    if (!need_quote) return val;
    std::string result = "'";
    for (char c : val) {
        if (c == '\\' || c == '\'') result += '\\';
        result += c;
    }
    result += "'";
    return result;
}

// Runs in the child process after fork(). Connects to PostgreSQL, executes
// the statement, writes the result to the log file, and exits with 0 on
// success or non-zero on failure. The child must NOT use spdlog or any
// parent state — only async-signal-safe / child-safe operations.
static int runSqlChild(const std::string& conn_str,
                       const std::string& sql_statement,
                       const std::string& log_path) {
    std::ofstream ofs(log_path, std::ios::trunc);
    if (!ofs) {
        return 126;
    }

    try {
        pqxx::connection conn(conn_str);

        // Determine if this is a SELECT or a DML/DDL statement
        std::string trimmed = sql_statement;
        size_t start = trimmed.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) {
            ofs << "Empty SQL statement\n";
            return 1;
        }
        trimmed = trimmed.substr(start);

        bool is_select = (trimmed.size() >= 6 &&
                          (trimmed.substr(0, 6) == "SELECT" ||
                           trimmed.substr(0, 6) == "select" ||
                           trimmed.substr(0, 6) == "Select"));

        if (is_select) {
            pqxx::nontransaction txn(conn);
            pqxx::result res = txn.exec(sql_statement);

            // Fix #188: Cap the number of rows written to the log to prevent
            // disk exhaustion / OOM from unbounded SELECT results.
            const pqxx::result::size_type max_rows = 10000;
            pqxx::result::size_type row_count = 0;
            for (const auto& row : res) {
                if (row_count >= max_rows) {
                    break;
                }
                for (int i = 0; i < static_cast<int>(row.size()); ++i) {
                    if (i > 0) ofs << "\t";
                    ofs << (row[i].is_null() ? "" : row[i].c_str());
                }
                ofs << "\n";
                row_count++;
            }
            if (res.size() > max_rows) {
                ofs << "... (truncated at " << max_rows
                    << " rows, total=" << res.size() << " rows)\n";
            }
        } else {
            pqxx::work txn(conn);
            pqxx::result res = txn.exec(sql_statement);
            txn.commit();
            ofs << "Affected rows: " << res.affected_rows() << "\n";
        }
        return 0;
    } catch (const pqxx::broken_connection& e) {
        ofs << "Connection failed: " << e.what() << "\n";
        return 1;
    } catch (const pqxx::sql_error& e) {
        ofs << "SQL error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        ofs << "Execution error: " << e.what() << "\n";
        return 1;
    }
}

TaskResult SqlExecutor::execute(const std::string& task_instance_id,
                                const nlohmann::json& config,
                                int timeout,
                                const std::string& log_dir,
                                std::function<void(pid_t)> pid_callback,
                                LogSink* /*log_sink*/) {
    TaskResult result;

    // Fix #288: 校验 task_instance_id 防止路径穿越
    if (!isValidInstanceId(task_instance_id)) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "Invalid task_instance_id contains path separators or traversal";
        return result;
    }

    // Validate required config fields
    auto get_string = [&config, &result](const std::string& key)
        -> std::string {
        if (!config.contains(key)) {
            result.status = "FAILED";
            result.error_message = "Missing or invalid '" + key + "' in config";
            return "";
        }
        if (config[key].is_string()) {
            return config[key].get<std::string>();
        }
        // Allow numeric types (e.g., db_port as integer)
        if (config[key].is_number()) {
            return std::to_string(config[key].get<int>());
        }
        result.status = "FAILED";
        result.error_message = "Missing or invalid '" + key + "' in config";
        return "";
    };

    std::string db_host = get_string("db_host");
    if (result.status == "FAILED") return result;
    std::string db_port = get_string("db_port");
    if (result.status == "FAILED") return result;
    std::string db_name = get_string("db_name");
    if (result.status == "FAILED") return result;
    std::string db_user = get_string("db_user");
    if (result.status == "FAILED") return result;
    std::string db_password = get_string("db_password");
    if (result.status == "FAILED") return result;
    std::string sql_statement = get_string("sql_statement");
    if (result.status == "FAILED") return result;

    std::string log_path = log_dir + "/" + task_instance_id + ".log";

    // Build connection string
    // Fix #288: 对含特殊字符的值用单引号包裹并转义，防止连接串注入
    std::string conn_str = "host=" + escapeConnValue(db_host) +
                           " port=" + db_port +
                           " dbname=" + escapeConnValue(db_name) +
                           " user=" + escapeConnValue(db_user) +
                           " password=" + escapeConnValue(db_password);

    // Fix #147: Fork a child process to run the SQL so the task can be
    // cancelled via SIGTERM (the same mechanism used by CommandExecutor and
    // ScriptExecutor). Previously SqlExecutor ran the query inline in the
    // executor thread with no PID registered, so TaskExecutor::cancel()
    // could never reach it.
    pid_t pid = fork();
    if (pid < 0) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = "fork() failed";
        return result;
    }

    if (pid == 0) {
        // Child process
        // Fix #206: Create a new process group so timeout/cancel can kill the
        // whole group, cleaning up any subprocesses pqxx/libpq may spawn.
        // Fix #294: 检查 setpgid 返回值，失败时退出子进程，防止 kill(-pid) 误杀 Worker
        if (setpgid(0, 0) != 0) {
            _exit(127);
        }
        int rc = runSqlChild(conn_str, sql_statement, log_path);
        _exit(rc);
    }

    // Parent process - report PID so cancel() can signal the child
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
            // Fix #206: Kill the whole process group (negative pid).
            // Fix #302: 检查 kill 返回值，失败时回退到 kill(pid)
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
            // Read the error message written by the child
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
                    ? "SQL execution failed with exit code " + std::to_string(result.exit_code)
                    : last_lines;
            } else {
                result.error_message = "SQL execution failed with exit code " + std::to_string(result.exit_code);
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
