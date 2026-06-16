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
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/async.h>
#include <grpcpp/grpcpp.h>

#include "common/config/worker_config.h"
#include "worker/grpc/worker_client.h"
#include "worker/executor/task_executor.h"
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
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_config.file_path, true);

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
        int timeout = request->timeout();
        std::string log_dir = log_dir_;

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

        std::string log_path = log_dir_ + "/" + request->task_instance_id() + ".log";
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
    auto channel = ::grpc::CreateChannel(
        config.scheduler.address, ::grpc::InsecureChannelCredentials());
    taskflow::worker::grpc::WorkerClient scheduler_client(channel);

    // 向 Scheduler 注册
    std::string worker_name = config.worker.name;
    if (worker_name.empty()) {
        worker_name = "worker-" + std::to_string(config.server.grpc_port);
    }

    std::string worker_address = "localhost:" + std::to_string(config.server.grpc_port);

    auto register_result = scheduler_client.registerWorker(
        worker_name, worker_address,
        config.worker.max_tasks, config.worker.resource_tags);

    std::string worker_id;
    if (register_result.ok()) {
        worker_id = register_result.value();
        spdlog::info("Worker 注册成功, worker_id: {}", worker_id);
    } else {
        spdlog::error("Worker 注册失败: {}", register_result.error());
        return 1;
    }

    // 启动心跳线程
    std::atomic<bool> running{true};
    std::thread heartbeat_thread([&scheduler_client, &worker_id, &running, &executor]() {
        while (running.load()) {
            auto result = scheduler_client.sendHeartbeat(
                worker_id, 0.0, 0.0, executor.runningCount());
            if (!result.ok()) {
                spdlog::warn("心跳发送失败: {}", result.error());
            }
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    });

    // 启动 gRPC 服务
    std::string server_address = "0.0.0.0:" + std::to_string(config.server.grpc_port);
    WorkerServiceImpl service(executor, scheduler_client, config.task_log.dir);

    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        spdlog::error("gRPC 服务启动失败");
        running.store(false);
        heartbeat_thread.join();
        return 1;
    }

    spdlog::info("TaskFlow Worker 启动完成, 监听: {}", server_address);
    server->Wait();

    // 清理
    running.store(false);
    heartbeat_thread.join();

    return 0;
}
