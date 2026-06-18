#include "worker/executor/sql_executor.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <spdlog/spdlog.h>
#include <pqxx/pqxx>

namespace taskflow::worker::executor {

TaskResult SqlExecutor::execute(const std::string& task_instance_id,
                                const nlohmann::json& config,
                                int /*timeout*/,
                                const std::string& log_dir,
                                std::function<void(pid_t)> /*pid_callback*/,
                                LogSink* /*log_sink*/) {
    TaskResult result;

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
    std::string conn_str = "host=" + db_host +
                           " port=" + db_port +
                           " dbname=" + db_name +
                           " user=" + db_user +
                           " password=" + db_password;

    // Connect
    std::unique_ptr<pqxx::connection> conn;
    try {
        conn = std::make_unique<pqxx::connection>(conn_str);
    } catch (const pqxx::broken_connection& e) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = std::string("Connection failed: ") + e.what();
        return result;
    } catch (const std::exception& e) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = std::string("Connection failed: ") + e.what();
        return result;
    }

    // Execute SQL
    std::string output;
    try {
        // Determine if this is a SELECT or a DML/DDL statement
        std::string trimmed = sql_statement;
        // Trim leading whitespace
        size_t start = trimmed.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) {
            result.status = "FAILED";
            result.exit_code = 1;
            result.error_message = "Empty SQL statement";
            return result;
        }
        trimmed = trimmed.substr(start);

        bool is_select = (trimmed.size() >= 6 &&
                          (trimmed.substr(0, 6) == "SELECT" ||
                           trimmed.substr(0, 6) == "select" ||
                           trimmed.substr(0, 6) == "Select"));

        if (is_select) {
            // Use nontransaction for SELECT
            pqxx::nontransaction txn(*conn);
            pqxx::result res = txn.exec(sql_statement);

            std::ostringstream oss;
            for (const auto& row : res) {
                for (int i = 0; i < static_cast<int>(row.size()); ++i) {
                    if (i > 0) oss << "\t";
                    oss << row[i].c_str();
                }
                oss << "\n";
            }
            output = oss.str();
        } else {
            // Use work for DML/DDL
            pqxx::work txn(*conn);
            pqxx::result res = txn.exec(sql_statement);
            txn.commit();

            std::ostringstream oss;
            oss << "Affected rows: " << res.affected_rows() << "\n";
            output = oss.str();
        }
    } catch (const pqxx::sql_error& e) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = std::string("SQL error: ") + e.what();
        return result;
    } catch (const std::exception& e) {
        result.status = "FAILED";
        result.exit_code = 1;
        result.error_message = std::string("Execution error: ") + e.what();
        return result;
    }

    // Write results to log file
    {
        std::ofstream ofs(log_path, std::ios::app);
        if (ofs) {
            ofs << output;
        }
    }

    result.status = "SUCCESS";
    result.exit_code = 0;
    return result;
}

}  // namespace taskflow::worker::executor
