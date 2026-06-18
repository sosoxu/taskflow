#pragma once

#include <string>
#include <vector>
#include <memory>

namespace taskflow::worker::executor {

// Abstract log sink interface
class LogSink {
public:
    virtual ~LogSink() = default;

    // Write log data for a task instance
    virtual bool write(const std::string& workflow_instance_id,
                       const std::string& task_instance_id,
                       const std::string& data) = 0;

    // Read log content for a task instance
    virtual std::string read(const std::string& workflow_instance_id,
                             const std::string& task_instance_id) = 0;

    // Check if log exists
    virtual bool exists(const std::string& workflow_instance_id,
                        const std::string& task_instance_id) = 0;

    // Cleanup logs older than retention_days
    virtual void cleanup(int retention_days) = 0;

    // Get the log file path (for gRPC streaming)
    virtual std::string getLogPath(const std::string& workflow_instance_id,
                                   const std::string& task_instance_id) = 0;
};

// File-based log sink (default implementation)
class FileLogSink : public LogSink {
public:
    explicit FileLogSink(const std::string& base_dir);

    bool write(const std::string& workflow_instance_id,
               const std::string& task_instance_id,
               const std::string& data) override;

    std::string read(const std::string& workflow_instance_id,
                     const std::string& task_instance_id) override;

    bool exists(const std::string& workflow_instance_id,
                const std::string& task_instance_id) override;

    void cleanup(int retention_days) override;

    std::string getLogPath(const std::string& workflow_instance_id,
                           const std::string& task_instance_id) override;

private:
    std::string base_dir_;
    std::string makeLogDir(const std::string& workflow_instance_id);
    std::string makeLogPath(const std::string& workflow_instance_id,
                            const std::string& task_instance_id);
};

// Elasticsearch log sink (reserved for future ELK integration)
class ElasticLogSink : public LogSink {
public:
    explicit ElasticLogSink(const std::string& base_dir,
                             const std::string& es_url = "",
                             const std::string& es_index = "taskflow-logs");

    bool write(const std::string& workflow_instance_id,
               const std::string& task_instance_id,
               const std::string& data) override;

    std::string read(const std::string& workflow_instance_id,
                     const std::string& task_instance_id) override;

    bool exists(const std::string& workflow_instance_id,
                const std::string& task_instance_id) override;

    void cleanup(int retention_days) override;

    std::string getLogPath(const std::string& workflow_instance_id,
                           const std::string& task_instance_id) override;

private:
    FileLogSink file_sink_;  // Fallback to file storage
    std::string es_url_;
    std::string es_index_;
    // Future: add Elasticsearch HTTP client
};

// Factory function to create log sink based on configuration
std::unique_ptr<LogSink> createLogSink(const std::string& sink_type,
                                        const std::string& base_dir,
                                        const std::string& es_url = "",
                                        const std::string& es_index = "taskflow-logs");

}  // namespace taskflow::worker::executor
