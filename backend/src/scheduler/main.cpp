#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <fstream>
#include <csignal>
#include <atomic>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>
#include <drogon/drogon.h>
#include <drogon/HttpRequest.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>

// Specialize fromRequest for std::string to prevent Drogon LOG_ERROR
// when path parameters are missing (e.g., after filter rejects request)
namespace drogon {
template <>
inline std::string fromRequest<std::string>(const HttpRequest&) {
    return std::string();
}
}  // namespace drogon

#include "common/config/scheduler_config.h"
#include "common/database/database_manager.h"
#include "scheduler/api/health_controller.h"
#include "scheduler/api/auth_controller.h"
#include "scheduler/api/user_controller.h"
#include "scheduler/api/task_controller.h"
#include "scheduler/api/workflow_controller.h"
#include "scheduler/api/instance_controller.h"
#include "scheduler/api/worker_controller.h"
#include "scheduler/service/auth_service.h"
#include "scheduler/service/user_service.h"
#include "scheduler/service/task_service.h"
#include "scheduler/service/workflow_service.h"
#include "scheduler/service/instance_service.h"
#include "scheduler/service/worker_service.h"
#include "scheduler/dao/worker_dao.h"
#include "scheduler/middleware/auth_middleware.h"
#include "scheduler/middleware/role_middleware.h"
#include "scheduler/grpc/scheduler_service.h"
#include "scheduler/grpc/heartbeat_checker.h"
#include "scheduler/engine/dag_driver.h"
#include "scheduler/engine/cron_scheduler.h"
#include "scheduler/grpc/leader_election.h"
#include "taskflow.grpc.pb.h"

static void initLogger(const taskflow::common::config::LogConfig& log_config) {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_config.file_path, 0, 0, false, 30);

        spdlog::init_thread_pool(8192, 1);
        auto logger = std::make_shared<spdlog::async_logger>(
            "taskflow",
            spdlog::sinks_init_list{console_sink, file_sink},
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );

        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        logger->set_level(spdlog::level::from_str(log_config.level));
        spdlog::set_default_logger(logger);

        spdlog::info("日志系统初始化完成, 级别: {}", log_config.level);
    } catch (const spdlog::spdlog_ex& e) {
        std::cerr << "日志初始化失败: " << e.what() << std::endl;
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    std::string config_path = "scheduler.yaml";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // 加载配置
    taskflow::common::config::SchedulerConfig config;
    try {
        config = taskflow::common::config::SchedulerConfig::load(config_path);
    } catch (const std::exception& e) {
        std::cerr << "配置加载失败: " << e.what() << std::endl;
        return 1;
    }

    // 初始化日志
    initLogger(config.log);

    spdlog::info("TaskFlow Scheduler 启动中...");
    spdlog::info("HTTP 端口: {}, gRPC 端口: {}", config.server.http_port, config.server.grpc_port);

    // 初始化数据库连接池
    try {
        taskflow::common::database::DatabaseManager::instance().init(
            config.database.connectionString(),
            config.database.min_connections,
            config.database.max_connections
        );
        spdlog::info("数据库连接池初始化完成");
    } catch (const std::exception& e) {
        spdlog::error("数据库连接池初始化失败: {}", e.what());
        return 1;
    }

    // 启动 gRPC 服务
    taskflow::scheduler::grpc::SchedulerServiceImpl scheduler_service;
    std::string grpc_address = "0.0.0.0:" + std::to_string(config.server.grpc_port);
    ::grpc::ServerBuilder grpc_builder;

    // Configure TLS if enabled
    if (config.server.tls.enabled) {
        grpc::SslServerCredentialsOptions ssl_opts;
        // Read cert and key files
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
        grpc_builder.AddListeningPort(grpc_address, grpc::SslServerCredentials(ssl_opts));
    } else {
        grpc_builder.AddListeningPort(grpc_address, ::grpc::InsecureServerCredentials());
    }

    grpc_builder.RegisterService(&scheduler_service);
    auto grpc_server = grpc_builder.BuildAndStart();
    if (!grpc_server) {
        spdlog::error("gRPC 服务启动失败");
        return 1;
    }
    spdlog::info("gRPC 服务启动, 监听: {}", grpc_address);

    // 在后台线程运行 gRPC 服务
    std::thread grpc_thread([&grpc_server]() {
        grpc_server->Wait();
    });
    grpc_thread.detach();

    // 启动心跳检测
    taskflow::scheduler::grpc::HeartbeatChecker heartbeat_checker(
        config.schedule.heartbeat_check_interval,
        config.schedule.heartbeat_timeout
    );
    heartbeat_checker.start();
    spdlog::info("心跳检测已启动");

    // 启动选主（必须在 DagDriver 和 CronScheduler 之前）
    auto leader_election = std::make_shared<taskflow::scheduler::grpc::LeaderElection>(
        config.schedule.leader_lease_interval, 12345);
    leader_election->start();
    spdlog::info("选主机制已启动");

    // 启动 DAG 执行驱动
    taskflow::scheduler::engine::DagDriver dag_driver(
        config.schedule.dag_drive_interval,
        config.encryption.aes_key,
        leader_election);
    dag_driver.start();
    spdlog::info("DAG 执行驱动已启动");

    // 启动定时调度
    taskflow::scheduler::engine::CronScheduler cron_scheduler(leader_election);
    cron_scheduler.start();
    spdlog::info("定时调度已启动");

    // 配置 Drogon HTTP 服务
    drogon::app()
        .setLogPath("./logs")
        .addListener("0.0.0.0", config.server.http_port)
        .setThreadNum(4);

    // CORS: 在路由之前拦截 OPTIONS 预检请求，返回 204
    drogon::app().registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr& req,
           drogon::AdviceCallback&& acb,
           drogon::AdviceChainCallback&& accb) {
            if (req->method() == drogon::Options) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                resp->addHeader("Access-Control-Max-Age", "86400");
                acb(resp);
                return;
            }
            accb();
        });

    // 注册全局 Filter（认证 → 权限）
    auto auth_filter = std::make_shared<taskflow::scheduler::middleware::AuthFilter>(
        config.auth.jwt_secret);
    auto role_filter = std::make_shared<taskflow::scheduler::middleware::RoleFilter>();
    drogon::app().registerFilter(auth_filter);
    drogon::app().registerFilter(role_filter);

    // 为所有响应添加 CORS 头
    drogon::app().registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr&, const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        });

    // 注册 Controller
    auto healthCtrl = std::make_shared<taskflow::scheduler::api::HealthController>();
    drogon::app().registerController(healthCtrl);

    auto auth_service = std::make_shared<taskflow::scheduler::service::AuthService>(
        config.auth.jwt_secret,
        config.auth.access_token_ttl,
        config.auth.refresh_token_ttl
    );
    auto authCtrl = std::make_shared<taskflow::scheduler::api::AuthController>(auth_service);
    drogon::app().registerController(authCtrl);

    auto user_service = std::make_shared<taskflow::scheduler::service::UserService>();
    auto userCtrl = std::make_shared<taskflow::scheduler::api::UserController>(user_service);
    drogon::app().registerController(userCtrl);

    auto task_service = std::make_shared<taskflow::scheduler::service::TaskService>(
        config.encryption.aes_key);
    auto taskCtrl = std::make_shared<taskflow::scheduler::api::TaskController>(task_service);
    drogon::app().registerController(taskCtrl);

    auto workflow_service = std::make_shared<taskflow::scheduler::service::WorkflowService>();
    auto workflowCtrl = std::make_shared<taskflow::scheduler::api::WorkflowController>(workflow_service);
    drogon::app().registerController(workflowCtrl);

    auto instance_service = std::make_shared<taskflow::scheduler::service::InstanceService>();
    auto instanceCtrl = std::make_shared<taskflow::scheduler::api::InstanceController>(instance_service, config.auth.jwt_secret);
    drogon::app().registerController(instanceCtrl);

    auto worker_dao = std::make_shared<taskflow::scheduler::dao::WorkerDao>();
    auto worker_service = std::make_shared<taskflow::scheduler::service::WorkerService>(worker_dao);
    auto workerCtrl = std::make_shared<taskflow::scheduler::api::WorkerController>(worker_service);
    drogon::app().registerController(workerCtrl);

    spdlog::info("TaskFlow Scheduler 启动完成");

    // Install signal handlers for graceful shutdown
    std::atomic<bool> shutdown_requested{false};
    std::signal(SIGTERM, [](int) {
        spdlog::info("Received SIGTERM, initiating graceful shutdown...");
        drogon::app().quit();
    });
    std::signal(SIGINT, [](int) {
        spdlog::info("Received SIGINT, initiating graceful shutdown...");
        drogon::app().quit();
    });

    drogon::app().run();

    spdlog::info("Drogon 已退出，开始清理资源...");

    // 清理（按逆序停止后台线程）
    cron_scheduler.stop();
    spdlog::info("CronScheduler 已停止");
    dag_driver.stop();
    spdlog::info("DagDriver 已停止");
    heartbeat_checker.stop();
    spdlog::info("HeartbeatChecker 已停止");
    leader_election->stop();
    spdlog::info("LeaderElection 已停止");
    grpc_server->Shutdown();
    spdlog::info("gRPC 服务已停止");

    // 显式关闭数据库连接池（在静态析构之前）
    taskflow::common::database::DatabaseManager::instance().shutdown();
    spdlog::info("TaskFlow Scheduler 已关闭");

    return 0;
}
