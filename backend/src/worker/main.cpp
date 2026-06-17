#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>

#include "common/config/worker_config.h"
#include "worker/grpc/worker_client.h"
#include "worker/executor/task_executor.h"
#include "worker/util/resource_collector.h"
#include "taskflow.grpc.pb.h"

using taskflow::v1::WorkerService;
using taskflow::v1::TaskDispatchRequest;
using taskflow::v1::TaskDispatchResponse;
using taskflow::v1::TaskCancelRequest;
using taskflow::v1::TaskCancelResponse;
using taskflow::v1::TaskLogRequest;
using taskflow::v1::LogChunk;

static void initLogger(const taskflow::common::config::WorkerLogConfig& log_config) {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_config.file_path, 0, 0, false, 30);

        spdlog::init_thread_pool(8192, 1);
        auto logger = std::make_shared<spdlog::async_logger>(
            "taskflow_worker",
            spdlog::sinks_init_list{console_sink, file_sink},
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );

        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        logger->set_level(spdlog::level::from_str(log_config.level));
        spdlog::set_default_logger(logger);

        spdlog::info("Worker 日志系统初始化完成, 级别: {}", log_config.level);
    } catch (const spdlog::spdlog_ex& e) {
        std::cerr << "日志初始化失败: " << e.what() << std::endl;
        exit(1);
    }
}

// WorkerService 实现 - 集成 TaskExecutor
class WorkerServiceImpl final : public WorkerService::Service {
public:
    WorkerServiceImpl(taskflow::worker::executor::TaskExecutor& executor,
                      taskflow::worker::grpc::WorkerClient& client,
                      const std::string& log_dir)
        : executor_(executor), client_(client), log_dir_(log_dir) {}

    grpc::Status DispatchTask(grpc::ServerContext* /*context*/,
                              const TaskDispatchRequest* request,
                              TaskDispatchResponse* response) override {
        spdlog::info("收到任务下发请求: task_instance_id={}, type={}",
                     request->task_instance_id(), request->task_type());

        try {
            auto config = nlohmann::json::parse(request->config_json());
        } catch (const std::exception& e) {
            response->set_accepted(false);
            response->set_error_message(std::string("配置 JSON 解析失败: ") + e.what());
            return grpc::Status::OK;
        }

        nlohmann::json config;
        try {
            config = nlohmann::json::parse(request->config_json());
        } catch (const std::exception& e) {
            response->set_accepted(false);
            response->set_error_message(std::string("配置 JSON 解析失败: ") + e.what());
            return grpc::Status::OK;
        }

        std::string task_instance_id = request->task_instance_id();
        std::string task_type = request->task_type();
        std::string workflow_instance_id = request->workflow_instance_id();
        int timeout = request->timeout();
        std::string log_dir = log_dir_;

        if (!workflow_instance_id.empty()) {
            log_dir += "/" + workflow_instance_id;
        }
        std::filesystem::create_directories(log_dir);

        auto result = executor_.submit(
            task_instance_id, task_type, config, timeout, log_dir,
            [this, task_instance_id](const taskflow::worker::executor::TaskResult& task_result) {
                spdlog::info("任务完成: task_instance_id={}, status={}, exit_code={}",
                             task_instance_id, task_result.status, task_result.exit_code);

                auto report_result = client_.reportTaskResult(
                    task_instance_id, task_result.status,
                    task_result.exit_code, task_result.error_message);

                if (!report_result.ok()) {
                    spdlog::error("上报任务结果失败: {}", report_result.error());
                }
            });

        if (!result.ok()) {
            response->set_accepted(false);
            response->set_error_message(result.error());
        } else {
            response->set_accepted(true);
        }

        return grpc::Status::OK;
    }

    grpc::Status CancelTask(grpc::ServerContext* /*context*/,
                            const TaskCancelRequest* request,
                            TaskCancelResponse* response) override {
        spdlog::info("收到任务取消请求: task_instance_id={}", request->task_instance_id());

        auto result = executor_.cancel(request->task_instance_id());
        if (result.ok()) {
            response->set_cancelled(true);
        } else {
            response->set_cancelled(false);
            response->set_error_message(result.error());
        }

        return grpc::Status::OK;
    }

    grpc::Status GetTaskLog(grpc::ServerContext* /*context*/,
                            const TaskLogRequest* request,
                            grpc::ServerWriter<LogChunk>* writer) override {
        spdlog::info("收到日志请求: task_instance_id={}", request->task_instance_id());

        std::string log_path;
        std::string target_filename = request->task_instance_id() + ".log";

        // Search in workflow_instance_id subdirectories for the log file
        for (const auto& entry : std::filesystem::directory_iterator(log_dir_)) {
            if (entry.is_directory()) {
                auto candidate = entry.path() / target_filename;
                if (std::filesystem::exists(candidate)) {
                    log_path = candidate.string();
                    break;
                }
            }
        }

        // Fallback to flat log directory for backward compatibility
        if (log_path.empty()) {
            log_path = log_dir_ + "/" + target_filename;
        }

        std::ifstream ifs(log_path, std::ios::binary);

        if (!ifs.is_open()) {
            LogChunk chunk;
            chunk.set_task_instance_id(request->task_instance_id());
            chunk.set_data("日志文件不存在\n");
            chunk.set_eof(true);
            writer->Write(chunk);
            return grpc::Status::OK;
        }

        constexpr size_t chunk_size = 4096;
        std::vector<char> buffer(chunk_size);

        while (ifs.read(buffer.data(), chunk_size) || ifs.gcount() > 0) {
            LogChunk chunk;
            chunk.set_task_instance_id(request->task_instance_id());
            chunk.set_data(buffer.data(), static_cast<size_t>(ifs.gcount()));
            chunk.set_eof(false);
            writer->Write(chunk);
        }

        LogChunk eof_chunk;
        eof_chunk.set_task_instance_id(request->task_instance_id());
        eof_chunk.set_eof(true);
        writer->Write(eof_chunk);

        return grpc::Status::OK;
    }

private:
    taskflow::worker::executor::TaskExecutor& executor_;
    taskflow::worker::grpc::WorkerClient& client_;
    std::string log_dir_;
};

int main(int argc, char* argv[]) {
    std::string config_path = "worker.yaml";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // 加载配置
    taskflow::common::config::WorkerConfig config;
    try {
        config = taskflow::common::config::WorkerConfig::load(config_path);
    } catch (const std::exception& e) {
        std::cerr << "配置加载失败: " << e.what() << std::endl;
        return 1;
    }

    // 初始化日志
    initLogger(config.log);

    spdlog::info("TaskFlow Worker 启动中...");
    spdlog::info("gRPC 端口: {}", config.server.grpc_port);
    spdlog::info("Scheduler 地址: {}", config.scheduler.address);

    // 创建任务日志目录
    std::filesystem::create_directories(config.task_log.dir);

    // 创建 TaskExecutor
    taskflow::worker::executor::TaskExecutor executor(config.worker.max_tasks);

    // 创建 gRPC 客户端连接 Scheduler
    std::shared_ptr<grpc::Channel> channel;
    if (config.scheduler.tls.enabled) {
        grpc::SslCredentialsOptions ssl_opts;
        std::ifstream cert_file(config.scheduler.tls.cert_path);
        std::ifstream key_file(config.scheduler.tls.key_path);
        std::string cert_str((std::istreambuf_iterator<char>(cert_file)),
                              std::istreambuf_iterator<char>());
        std::string key_str((std::istreambuf_iterator<char>(key_file)),
                             std::istreambuf_iterator<char>());
        ssl_opts.pem_private_key = key_str;
        ssl_opts.pem_cert_chain = cert_str;
        if (!config.scheduler.tls.ca_path.empty()) {
            std::ifstream ca_file(config.scheduler.tls.ca_path);
            std::string ca_str((std::istreambuf_iterator<char>(ca_file)),
                                std::istreambuf_iterator<char>());
            ssl_opts.pem_root_certs = ca_str;
        }
        channel = ::grpc::CreateChannel(config.scheduler.address, grpc::SslCredentials(ssl_opts));
    } else {
        channel = ::grpc::CreateChannel(config.scheduler.address, ::grpc::InsecureChannelCredentials());
    }
    taskflow::worker::grpc::WorkerClient scheduler_client(channel);

    // 向 Scheduler 注册（带重试）
    std::string worker_name = config.worker.name;
    if (worker_name.empty()) {
        worker_name = "worker-" + std::to_string(config.server.grpc_port);
    }

    std::string worker_address = "localhost:" + std::to_string(config.server.grpc_port);

    std::string worker_id;
    int register_retries = 0;
    const int max_register_retries = 10;
    while (register_retries < max_register_retries) {
        auto register_result = scheduler_client.registerWorker(
            worker_name, worker_address,
            config.worker.max_tasks, config.worker.resource_tags);

        if (register_result.ok()) {
            worker_id = register_result.value();
            spdlog::info("Worker 注册成功, worker_id: {}", worker_id);
            break;
        }
        register_retries++;
        int delay = 5 * register_retries;  // 线性退避: 5s, 10s, 15s...
        spdlog::warn("Register failed (attempt {}/{}), retrying in {}s: {}",
                     register_retries, max_register_retries, delay, register_result.error());
        std::this_thread::sleep_for(std::chrono::seconds(delay));
    }
    if (register_retries >= max_register_retries) {
        spdlog::error("Failed to register after {} attempts, exiting", max_register_retries);
        return 1;
    }

    // 启动心跳线程
    std::atomic<bool> running{true};
    std::thread heartbeat_thread([&scheduler_client, &worker_id, &running, &executor]() {
        while (running.load()) {
            auto resources = taskflow::worker::util::ResourceCollector::collect();
            auto result = scheduler_client.sendHeartbeat(
                worker_id, resources.cpu_usage, resources.memory_usage, executor.runningCount());
            if (!result.ok()) {
                spdlog::warn("心跳发送失败: {}", result.error());
            }
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    });

    // 启动日志清理线程
    std::string task_log_dir = config.task_log.dir;
    int log_retention_days = config.task_log.retention_days;
    std::thread log_cleanup_thread([&running, task_log_dir, log_retention_days]() {
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::hours(1));
            if (!running.load()) {
                break;
            }

            try {
                if (!std::filesystem::exists(task_log_dir)) {
                    continue;
                }

                auto now = std::filesystem::file_time_type::clock::now();
                auto retention = std::chrono::hours(24 * log_retention_days);

                for (const auto& entry : std::filesystem::directory_iterator(task_log_dir)) {
                    if (!entry.is_directory()) {
                        continue;
                    }
                    auto last_modified = std::filesystem::last_write_time(entry);
                    if (now - last_modified > retention) {
                        std::filesystem::remove_all(entry.path());
                        spdlog::info("日志清理: 删除过期日志目录 {}", entry.path().string());
                    }
                }
            } catch (const std::filesystem::filesystem_error& e) {
                spdlog::warn("日志清理失败: {}", e.what());
            }
        }
    });

    // 启动 gRPC 服务
    std::string server_address = "0.0.0.0:" + std::to_string(config.server.grpc_port);
    WorkerServiceImpl service(executor, scheduler_client, config.task_log.dir);

    ::grpc::ServerBuilder builder;
    if (config.server.tls.enabled) {
        grpc::SslServerCredentialsOptions ssl_opts;
        std::ifstream cert_file(config.server.tls.cert_path);
        std::ifstream key_file(config.server.tls.key_path);
        std::string cert_str((std::istreambuf_iterator<char>(cert_file)),
                              std::istreambuf_iterator<char>());
        std::string key_str((std::istreambuf_iterator<char>(key_file)),
                             std::istreambuf_iterator<char>());
        grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert;
        key_cert.private_key = key_str;
        key_cert.cert_chain = cert_str;
        ssl_opts.pem_key_cert_pairs.push_back(key_cert);
        if (!config.server.tls.ca_path.empty()) {
            std::ifstream ca_file(config.server.tls.ca_path);
            std::string ca_str((std::istreambuf_iterator<char>(ca_file)),
                                std::istreambuf_iterator<char>());
            ssl_opts.pem_root_certs = ca_str;
        }
        builder.AddListeningPort(server_address, grpc::SslServerCredentials(ssl_opts));
    } else {
        builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
    }
    builder.RegisterService(&service);

    std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        spdlog::error("gRPC 服务启动失败");
        running.store(false);
        heartbeat_thread.join();
        log_cleanup_thread.join();
        return 1;
    }

    spdlog::info("TaskFlow Worker 启动完成, 监听: {}", server_address);
    server->Wait();

    // 清理
    running.store(false);
    heartbeat_thread.join();
    log_cleanup_thread.join();

    return 0;
}
