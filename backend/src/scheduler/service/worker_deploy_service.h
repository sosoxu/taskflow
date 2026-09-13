#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/result/result.h"

namespace taskflow::scheduler::service {

// Parameters for deploying (create config + start) a worker on a remote node
// via SSH username/password authentication.
struct WorkerDeployRequest {
    std::string name;                 // worker name
    std::string host;                 // remote machine IP (SSH host + advertise host)
    int grpc_port = 50052;            // worker gRPC port (server.grpc_port)
    int ssh_port = 22;                // SSH port
    std::string ssh_username;
    std::string ssh_password;

    int max_tasks = 10;
    std::vector<std::string> resource_tags;

    // Path to the worker binary already installed on the remote machine.
    std::string worker_binary_path = "/opt/taskflow/bin/worker";
    // Working directory on the remote host (logs/, worker.yaml live here).
    std::string remote_dir = "/opt/taskflow/worker";
    // Address the worker uses to reach the scheduler (host:port).
    std::string scheduler_address;
    std::string log_level = "info";
};

// Step-by-step deployment log.
struct DeployStepLog {
    std::string step;
    bool success = false;
    std::string message;
};

struct WorkerDeployResult {
    bool success = false;
    std::string worker_name;
    std::string address;            // host:grpc_port (worker advertise_address)
    std::string config_path;        // remote config file path
    std::vector<DeployStepLog> steps;
    std::string message;

    nlohmann::json toJson() const;
};

class WorkerDeployService {
public:
    // Fix #357: 注入 scheduler 自身的 gRPC 内部认证 token，写入生成的
    // worker 配置。开启认证（server.grpc_auth_token 非空）后，不带 token
    // 的 SSH 部署 worker 注册会被拒（UNAUTHENTICATED），部署链路失效。
    // token 由 scheduler 侧注入而非请求携带，避免凭据经 API 传输。
    explicit WorkerDeployService(const std::string& grpc_auth_token = {})
        : grpc_auth_token_(grpc_auth_token) {}

    // Validate inputs, generate worker.yaml, SSH into the node, write the
    // config file and start the worker. The worker registers itself with the
    // scheduler over gRPC on startup, so it will appear in the worker list
    // automatically once it boots.
    common::result::Result<WorkerDeployResult> deploy(const WorkerDeployRequest& req);

private:
    static common::result::Result<void> validate(const WorkerDeployRequest& req);
    std::string generateWorkerConfig(const WorkerDeployRequest& req) const;
    std::string grpc_auth_token_;
};

}  // namespace taskflow::scheduler::service
