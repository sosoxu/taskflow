#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <csignal>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>

#include "common/config/worker_config.h"
#include "worker/grpc/worker_client.h"
#include "common/util/grpc_auth_util.h"
#include "common/util/instance_id.h"
#include "worker/executor/task_executor.h"
#include "worker/util/resource_collector.h"
#include "taskflow.grpc.pb.h"

// Fix #342: 公共校验函数，唯一定义在 common/util/instance_id.h
// 覆盖 Fix #298 的路径穿越校验要求
using taskflow::common::util::isValidInstanceId;
using taskflow::v1::WorkerService;
using taskflow::v1::TaskDispatchRequest;
using taskflow::v1::TaskDispatchResponse;
using taskflow::v1::TaskCancelRequest;
using taskflow::v1::TaskCancelResponse;
using taskflow::v1::TaskLogRequest;
using taskflow::v1::LogChunk;

// Fix #124: Global shutdown flag for signal handler → main loop communication.
// std::signal handlers cannot capture state, so we use a global atomic.
// This is file-scoped (anonymous namespace would also work).
std::atomic<bool> g_shutdown_flag{false};

static void initLogger(const taskflow::common::config::WorkerLogConfig& log_config) {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        // Fix #221: Use rotating_file_sink_mt to cap file size (same as scheduler).
        constexpr size_t kMaxLogFileSize = 100 * 1024 * 1024;  // 100 MB per file
        constexpr size_t kMaxLogFiles = 10;
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_config.file_path, kMaxLogFileSize, kMaxLogFiles);

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

static std::string localHostname() {
    char hostname[256] = {};
    if (::gethostname(hostname, sizeof(hostname) - 1) != 0) {
        return "unknown";
    }
    return hostname;
}

// Docker's service DNS name load-balances across replicas, which is unsuitable
// for scheduler-to-worker callbacks. Register this instance's own interface IP.
static std::string localRoutableIpv4() {
    ifaddrs* interfaces = nullptr;
    if (::getifaddrs(&interfaces) != 0) {
        return {};
    }

    std::string address;
    for (auto* interface = interfaces; interface != nullptr; interface = interface->ifa_next) {
        if (interface->ifa_addr == nullptr || interface->ifa_addr->sa_family != AF_INET ||
            (interface->ifa_flags & IFF_UP) == 0 ||
            (interface->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        char buffer[INET_ADDRSTRLEN] = {};
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(interface->ifa_addr);
        if (::inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer)) != nullptr) {
            address = buffer;
            break;
        }
    }

    ::freeifaddrs(interfaces);
    return address;
}

// WorkerService 实现 - 集成 TaskExecutor

class WorkerServiceImpl final : public WorkerService::Service {
public:
    // Fix #326: auth_token 非空时，DispatchTask/CancelTask/GetTaskLog 必须携带
    // 匹配的内部认证 token，否则拒绝。此前任何能连上 worker 的客户端都可以
    // 直接下发任意 command 任务（未授权 RCE）。
    WorkerServiceImpl(taskflow::worker::executor::TaskExecutor& executor,
                      taskflow::worker::grpc::WorkerClient& client,
                      const std::string& log_dir,
                      const std::string& auth_token)
        : executor_(executor), client_(client), log_dir_(log_dir), auth_token_(auth_token) {}

    grpc::Status DispatchTask(grpc::ServerContext* context,
                              const TaskDispatchRequest* request,
                              TaskDispatchResponse* response) override {
        auto auth = taskflow::common::util::GrpcAuthUtil::checkAuth(context, auth_token_);
        if (!auth.ok()) {
            spdlog::warn("DispatchTask rejected: {} (task_instance_id={})",
                         auth.error_message(), request->task_instance_id());
            return auth;
        }
        spdlog::info("收到任务下发请求: task_instance_id={}, type={}",
                     request->task_instance_id(), request->task_type());

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

        // Fix #298: 校验 workflow_instance_id 防止路径穿越
        // task_instance_id 在 executor 内部校验，但 workflow_instance_id 用于构造 log_dir
        // 必须在此处提前校验，否则 create_directories 会逃逸 log_dir_
        if (!workflow_instance_id.empty() && !isValidInstanceId(workflow_instance_id)) {
            response->set_accepted(false);
            response->set_error_message("Invalid workflow_instance_id contains path separators or traversal");
            return grpc::Status::OK;
        }

        std::string log_dir = log_dir_;

        if (!workflow_instance_id.empty()) {
            log_dir += "/" + workflow_instance_id;
        }
        std::filesystem::create_directories(log_dir);

        auto result = executor_.submit(
            task_instance_id, task_type, config, timeout, log_dir,
            workflow_instance_id,
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

    grpc::Status CancelTask(grpc::ServerContext* context,
                            const TaskCancelRequest* request,
                            TaskCancelResponse* response) override {
        auto auth = taskflow::common::util::GrpcAuthUtil::checkAuth(context, auth_token_);
        if (!auth.ok()) {
            return auth;
        }
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

    grpc::Status GetTaskLog(grpc::ServerContext* context,
                            const TaskLogRequest* request,
                            grpc::ServerWriter<LogChunk>* writer) override {
        auto auth = taskflow::common::util::GrpcAuthUtil::checkAuth(context, auth_token_);
        if (!auth.ok()) {
            return auth;
        }
        spdlog::info("收到日志请求: task_instance_id={}, follow={}",
                     request->task_instance_id(), request->follow());

        // Fix #298: 校验 task_instance_id 防止路径穿越
        // 若含 ../，可逃逸日志目录读取任意文件
        if (!isValidInstanceId(request->task_instance_id())) {
            LogChunk chunk;
            chunk.set_task_instance_id(request->task_instance_id());
            chunk.set_data("Invalid task_instance_id contains path separators or traversal\n");
            chunk.set_eof(true);
            writer->Write(chunk);
            return grpc::Status::OK;
        }

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

        // Fix #318: Implement follow mode (tail -f) for real-time log streaming.
        // When follow=true, keep reading new content until the task completes
        // (detected via executor_.isRunning() returning false) or the gRPC
        // context is cancelled.
        if (request->follow()) {
            return streamFollowLog(context, request, writer, log_path);
        }

        // Non-follow mode: read existing content once
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
    // Fix #318: Stream log file with follow mode (tail -f behavior).
    // Reads existing content, then polls for new content until the task
    // completes or the client disconnects.
    grpc::Status streamFollowLog(grpc::ServerContext* context,
                                 const TaskLogRequest* request,
                                 grpc::ServerWriter<LogChunk>* writer,
                                 const std::string& log_path) {
        constexpr size_t chunk_size = 4096;
        std::vector<char> buffer(chunk_size);

        // Wait for the log file to appear (task may not have started yet)
        for (int i = 0; i < 30 && !std::filesystem::exists(log_path); ++i) {
            if (context->IsCancelled()) {
                return grpc::Status(grpc::StatusCode::CANCELLED, "Client cancelled");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

        // Phase 1: Read existing content
        while (ifs.read(buffer.data(), chunk_size) || ifs.gcount() > 0) {
            LogChunk chunk;
            chunk.set_task_instance_id(request->task_instance_id());
            chunk.set_data(buffer.data(), static_cast<size_t>(ifs.gcount()));
            chunk.set_eof(false);
            if (!writer->Write(chunk)) {
                return grpc::Status::OK;  // Client disconnected
            }
        }

        // Phase 2: Follow new content until task completes
        // Check if the task is still running via the executor
        while (!context->IsCancelled()) {
            // Check if task is still running
            bool task_running = executor_.isRunning(request->task_instance_id());

            // Try to read new content
            ifs.clear();
            while (ifs.read(buffer.data(), chunk_size) || ifs.gcount() > 0) {
                LogChunk chunk;
                chunk.set_task_instance_id(request->task_instance_id());
                chunk.set_data(buffer.data(), static_cast<size_t>(ifs.gcount()));
                chunk.set_eof(false);
                if (!writer->Write(chunk)) {
                    return grpc::Status::OK;  // Client disconnected
                }
            }

            if (!task_running) {
                // Task finished; do one final read to catch any remaining output
                ifs.clear();
                while (ifs.read(buffer.data(), chunk_size) || ifs.gcount() > 0) {
                    LogChunk chunk;
                    chunk.set_task_instance_id(request->task_instance_id());
                    chunk.set_data(buffer.data(), static_cast<size_t>(ifs.gcount()));
                    chunk.set_eof(false);
                    writer->Write(chunk);
                }
                break;
            }

            // Wait before next poll
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        // Send EOF marker
        LogChunk eof_chunk;
        eof_chunk.set_task_instance_id(request->task_instance_id());
        eof_chunk.set_eof(true);
        writer->Write(eof_chunk);

        return grpc::Status::OK;
    }
    taskflow::worker::executor::TaskExecutor& executor_;
    taskflow::worker::grpc::WorkerClient& client_;
    std::string log_dir_;
    std::string auth_token_;
};

int main(int argc, char* argv[]) {
    std::string config_path = "worker.yaml";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg.find("--config=") == 0) {
            config_path = arg.substr(9);
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
    spdlog::info("gRPC 端口: {}",
                 config.server.grpc_port > 0 ? std::to_string(config.server.grpc_port)
                                             : std::string("auto（由内核分配）"));
    spdlog::info("Scheduler 地址: {}", config.scheduler.address);

    // 创建任务日志目录
    std::filesystem::create_directories(config.task_log.dir);

    // 创建 TaskExecutor
    taskflow::worker::executor::TaskExecutor executor(config.worker.max_tasks);

    // 创建 LogSink 并配置到 TaskExecutor
    auto log_sink = taskflow::worker::executor::createLogSink(
        config.task_log.sink_type, config.task_log.dir,
        config.task_log.es_url, config.task_log.es_index);
    // Fix #122: Keep a shared_ptr for the cleanup thread to use the LogSink interface.
    std::shared_ptr<taskflow::worker::executor::LogSink> log_sink_ptr(std::move(log_sink));
    executor.setLogSink(log_sink_ptr);
    spdlog::info("LogSink 配置完成, sink_type={}", config.task_log.sink_type);

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
    taskflow::worker::grpc::WorkerClient scheduler_client(channel, config.server.grpc_auth_token);

    // Start accepting callbacks before registering the worker. Registration
    // makes the worker immediately eligible for scheduling, so doing it first
    // exposes a window where the scheduler can select an unreachable worker.
    // 端口 0（默认）= 自动分配：由内核挑一个空闲端口，因此同一环境里再起一个
    // worker 不会复用端口。selected_port 拿回实际端口，后续注册地址必须用它。
    std::string server_address = "0.0.0.0:" + std::to_string(config.server.grpc_port);
    int selected_port = 0;
    WorkerServiceImpl service(executor, scheduler_client, config.task_log.dir,
                              config.server.grpc_auth_token);

    ::grpc::ServerBuilder builder;
    // 关闭 SO_REUSEPORT：gRPC 默认允许两个进程绑定同一端口（都"启动成功"，
    // 连接被内核轮询分配）。端口自动分配后正常不会撞车；若运维显式指定了固定
    // 端口且已被占用，这里让启动直接失败，而不是静默共存。
    builder.AddChannelArgument("grpc.so_reuseport", 0);
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
        builder.AddListeningPort(server_address, grpc::SslServerCredentials(ssl_opts), &selected_port);
    } else {
        builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials(), &selected_port);
    }
    builder.RegisterService(&service);

    std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        spdlog::error("gRPC 服务启动失败");
        return 1;
    }
    // 自动分配时以 gRPC 回填的实际端口为准
    const int effective_grpc_port =
        config.server.grpc_port > 0 ? config.server.grpc_port : selected_port;
    if (effective_grpc_port <= 0) {
        spdlog::error("无法确定 gRPC 监听端口 (配置 {} / 实际 {})",
                      config.server.grpc_port, selected_port);
        server->Shutdown();
        return 1;
    }
    spdlog::info("TaskFlow Worker 已监听, 等待向 Scheduler 注册: 0.0.0.0:{} (端口{})",
                 effective_grpc_port,
                 config.server.grpc_port > 0 ? "固定" : "自动分配");

    // 向 Scheduler 注册（带重试）
    std::string worker_name = config.worker.name;
    if (worker_name.empty()) {
        worker_name = "worker-" + localHostname() + "-" +
                      std::to_string(effective_grpc_port);
    }

    std::string worker_address = config.server.advertise_address;
    if (worker_address == "auto") {
        auto interface_address = localRoutableIpv4();
        if (interface_address.empty()) {
            spdlog::error("无法自动检测可路由的 IPv4 地址; 请设置 server.advertise_address");
            server->Shutdown();
            return 1;
        }
        worker_address = interface_address + ":" + std::to_string(effective_grpc_port);
    } else if (worker_address.empty()) {
        // Fix #146: Fall back to localhost:<port> for single-host dev setups.
        // Operators must set server.advertise_address when running in Docker
        // or on a remote host so the scheduler can dial back this worker.
        worker_address = "localhost:" + std::to_string(effective_grpc_port);
    }

    std::string worker_id;
    // Fix #333: 注册失败不再退出。此前重试 10 次（约 5 分钟）后进程直接退出，
    // scheduler 滚动重启/部署窗口超过 5 分钟时裸机部署的 worker 会永久离线。
    // 改为无限重试，线性退避封顶 30s，scheduler 恢复后自动完成注册。
    int register_retries = 0;
    while (true) {
        auto register_result = scheduler_client.registerWorker(
            worker_name, worker_address,
            config.worker.max_tasks, config.worker.resource_tags);

        if (register_result.ok()) {
            worker_id = register_result.value();
            spdlog::info("Worker 注册成功, worker_id: {} (重试 {} 次后)", worker_id, register_retries);
            break;
        }
        register_retries++;
        int delay = std::min(5 * register_retries, 30);  // 线性退避 5s,10s,15s... 封顶 30s
        spdlog::warn("Register failed (attempt {}), retrying in {}s: {}",
                     register_retries, delay, register_result.error());
        std::this_thread::sleep_for(std::chrono::seconds(delay));
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
    // Fix #122: Use the LogSink interface for cleanup instead of duplicating
    // filesystem logic, so that switching to ElasticLogSink will also work.
    // Fix #145: Use short sleep slices (1s) so the thread responds to
    // `running` being cleared within ~1s instead of blocking for up to 1h.
    // Run cleanup once at startup so a long-stopped worker doesn't keep
    // stale logs around until the first hourly tick.
    int log_retention_days = config.task_log.retention_days;
    std::thread log_cleanup_thread([&running, log_sink_ptr, log_retention_days]() {
        auto run_cleanup = [&]() {
            try {
                if (log_sink_ptr) {
                    log_sink_ptr->cleanup(log_retention_days);
                    spdlog::info("日志清理完成 (retention={}d)", log_retention_days);
                }
            } catch (const std::exception& e) {
                spdlog::warn("日志清理失败: {}", e.what());
            }
        };

        // Initial cleanup pass at startup.
        run_cleanup();

        constexpr int kSleepSeconds = 1;
        constexpr int kLoopSeconds = 3600;  // 1 hour between cleanups
        int elapsed = 0;
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(kSleepSeconds));
            if (!running.load()) {
                break;
            }
            elapsed += kSleepSeconds;
            if (elapsed >= kLoopSeconds) {
                elapsed = 0;
                run_cleanup();
            }
        }
    });

    spdlog::info("TaskFlow Worker 启动完成, 监听: {}", server_address);

    // Fix #124: Graceful shutdown on SIGTERM/SIGINT.
    // Order: stop gRPC server (no new tasks) → wait for running tasks →
    // deregister from scheduler → stop background threads.
    // Fix #145: The signal handler must be async-signal-safe — only
    // async-signal-safe functions (write(), _exit(), etc.) are allowed.
    // spdlog (malloc/locks/iostream) is NOT safe, so we only set the flag
    // here and log from the main loop below.
    auto signal_handler = [](int sig) {
        (void)sig;
        // std::signal handlers cannot capture state, so we set a global flag
        // and let the main loop perform the actual shutdown sequence.
        g_shutdown_flag.store(true);
    };
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    // Wait for shutdown signal (instead of blocking forever in server->Wait())
    while (!g_shutdown_flag.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spdlog::info("Received shutdown signal, initiating graceful shutdown...");

    spdlog::info("Stopping gRPC server (no new tasks will be accepted)...");
    server->Shutdown();

    // Wait for running tasks to finish (up to 30s), then cancel if needed.
    spdlog::info("Draining {} running task(s)...", executor.runningCount());
    executor.shutdown(30);

    // Deregister from scheduler so it marks this worker offline immediately.
    if (!worker_id.empty()) {
        spdlog::info("Deregistering worker {} from scheduler...", worker_id);
        auto dereg_result = scheduler_client.deregisterWorker(worker_id);
        if (!dereg_result.ok()) {
            spdlog::warn("Deregister failed: {} (scheduler will mark offline via heartbeat timeout)",
                         dereg_result.error());
        }
    }

    // Stop background threads
    running.store(false);
    heartbeat_thread.join();
    log_cleanup_thread.join();

    spdlog::info("TaskFlow Worker shutdown complete");
    return 0;
}
