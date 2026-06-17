#pragma once

#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace taskflow::common::config {

struct WorkerTlsConfig {
    bool enabled = false;
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
};

struct WorkerServerConfig {
    int grpc_port = 50052;
    WorkerTlsConfig tls;
};

struct SchedulerAddressConfig {
    std::string address = "localhost:50051";
    WorkerTlsConfig tls;
};

struct WorkerNodeConfig {
    std::string name;                // 留空则自动生成
    int max_tasks = 10;
    std::vector<std::string> resource_tags;
};

struct WorkerLogConfig {
    std::string level = "info";
    std::string file_path = "logs/worker.log";
};

struct TaskLogConfig {
    std::string dir = "logs/tasks";  // 任务日志目录
    int retention_days = 30;
    std::string sink_type = "file";  // 日志存储后端类型: file, elasticsearch
    std::string es_url;           // Elasticsearch URL (for elasticsearch sink)
    std::string es_index = "taskflow-logs";  // Elasticsearch index
};

class WorkerConfig {
public:
    WorkerServerConfig server;
    SchedulerAddressConfig scheduler;
    WorkerNodeConfig worker;
    WorkerLogConfig log;
    TaskLogConfig task_log;

    static WorkerConfig load(const std::string& config_path);

    void validate() const;
};

}  // namespace taskflow::common::config
