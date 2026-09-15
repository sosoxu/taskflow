#include "scheduler/api/worker_controller.h"

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>
#include "scheduler/api/response_util.h"

namespace taskflow::scheduler::api {

namespace {

}  // namespace

WorkerController::WorkerController(
    std::shared_ptr<service::WorkerService> worker_service,
    std::shared_ptr<service::WorkerDeployService> worker_deploy_service)
    : worker_service_(std::move(worker_service)),
      worker_deploy_service_(std::move(worker_deploy_service)) {}

void WorkerController::listWorkers(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto result = worker_service_->listWorkers();

    if (!result.ok()) {
        sendError(std::move(callback), 400, 50001, result.error());
        return;
    }

    const auto& workers = result.value();
    nlohmann::json items = nlohmann::json::array();
    for (const auto& worker : workers) {
        items.push_back(worker.toJson());
    }

    nlohmann::json response = {
        {"items", items},
        {"total", static_cast<int>(workers.size())}
    };

    sendSuccess(std::move(callback), response);
}

void WorkerController::deployWorker(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto json = req->getJsonObject();
    if (!json || !(*json).isObject()) {
        sendError(std::move(callback), 400, 40001, "Request body must be a JSON object");
        return;
    }

    service::WorkerDeployRequest deployReq;
    deployReq.name = (*json).get("name", "").asString();
    deployReq.host = (*json).get("host", "").asString();
    // 默认 0 = 自动分配端口（见 worker_deploy_service.h）
    deployReq.grpc_port = (*json).get("grpc_port", 0).asInt();
    deployReq.ssh_port = (*json).get("ssh_port", 22).asInt();
    deployReq.ssh_username = (*json).get("ssh_username", "").asString();
    deployReq.ssh_password = (*json).get("ssh_password", "").asString();
    deployReq.max_tasks = (*json).get("max_tasks", 10).asInt();
    deployReq.worker_binary_path = (*json).get("worker_binary_path", "/opt/taskflow/bin/worker").asString();
    deployReq.remote_dir = (*json).get("remote_dir", "/opt/taskflow/worker").asString();
    deployReq.scheduler_address = (*json).get("scheduler_address", "").asString();
    deployReq.log_level = (*json).get("log_level", "info").asString();

    if ((*json).isMember("resource_tags") && (*json)["resource_tags"].isArray()) {
        for (const auto& tag : (*json)["resource_tags"]) {
            deployReq.resource_tags.push_back(tag.asString());
        }
    }

    auto result = worker_deploy_service_->deploy(deployReq);

    if (!result.ok()) {
        sendError(std::move(callback), 400, 50002, result.error());
        return;
    }

    const auto& deployResult = result.value();
    // Use 200 even when individual steps failed: the deploy call itself
    // completed and returns structured step-by-step diagnostics the frontend
    // can render.
    sendSuccess(std::move(callback), deployResult.toJson());
}

}  // namespace taskflow::scheduler::api
