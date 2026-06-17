#include "common/config/worker_config.h"

#include <stdexcept>

namespace taskflow::common::config {

WorkerConfig WorkerConfig::load(const std::string& config_path) {
    WorkerConfig config;
    YAML::Node root;

    try {
        root = YAML::LoadFile(config_path);
    } catch (const YAML::BadFile& e) {
        throw std::runtime_error("无法读取配置文件: " + config_path);
    } catch (const YAML::ParserException& e) {
        throw std::runtime_error("配置文件格式错误: " + std::string(e.what()));
    }

    // server
    if (root["server"]) {
        auto s = root["server"];
        if (s["grpc_port"]) config.server.grpc_port = s["grpc_port"].as<int>();
    }

    // server.tls
    if (root["server"] && root["server"]["tls"]) {
        auto tls = root["server"]["tls"];
        if (tls["enabled"]) config.server.tls.enabled = tls["enabled"].as<bool>();
        if (tls["cert_path"]) config.server.tls.cert_path = tls["cert_path"].as<std::string>();
        if (tls["key_path"]) config.server.tls.key_path = tls["key_path"].as<std::string>();
        if (tls["ca_path"]) config.server.tls.ca_path = tls["ca_path"].as<std::string>();
    }

    // scheduler
    if (root["scheduler"]) {
        auto sc = root["scheduler"];
        if (sc["address"]) config.scheduler.address = sc["address"].as<std::string>();
    }

    // scheduler.tls
    if (root["scheduler"] && root["scheduler"]["tls"]) {
        auto tls = root["scheduler"]["tls"];
        if (tls["enabled"]) config.scheduler.tls.enabled = tls["enabled"].as<bool>();
        if (tls["cert_path"]) config.scheduler.tls.cert_path = tls["cert_path"].as<std::string>();
        if (tls["key_path"]) config.scheduler.tls.key_path = tls["key_path"].as<std::string>();
        if (tls["ca_path"]) config.scheduler.tls.ca_path = tls["ca_path"].as<std::string>();
    }

    // worker
    if (root["worker"]) {
        auto w = root["worker"];
        if (w["name"]) config.worker.name = w["name"].as<std::string>();
        if (w["max_tasks"]) config.worker.max_tasks = w["max_tasks"].as<int>();
        if (w["resource_tags"]) {
            config.worker.resource_tags = w["resource_tags"].as<std::vector<std::string>>();
        }
    }

    // log
    if (root["log"]) {
        auto l = root["log"];
        if (l["level"]) config.log.level = l["level"].as<std::string>();
        if (l["file_path"]) config.log.file_path = l["file_path"].as<std::string>();
    }

    // task_log
    if (root["task_log"]) {
        auto tl = root["task_log"];
        if (tl["dir"]) config.task_log.dir = tl["dir"].as<std::string>();
        if (tl["retention_days"]) config.task_log.retention_days = tl["retention_days"].as<int>();
        if (tl["sink_type"]) config.task_log.sink_type = tl["sink_type"].as<std::string>();
        if (tl["es_url"]) config.task_log.es_url = tl["es_url"].as<std::string>();
        if (tl["es_index"]) config.task_log.es_index = tl["es_index"].as<std::string>();
    }

    config.validate();
    return config;
}

void WorkerConfig::validate() const {
    if (server.grpc_port <= 0 || server.grpc_port > 65535) {
        throw std::runtime_error("配置错误: server.grpc_port 无效");
    }
    if (scheduler.address.empty()) {
        throw std::runtime_error("配置错误: scheduler.address 不能为空");
    }
    if (worker.max_tasks <= 0) {
        throw std::runtime_error("配置错误: worker.max_tasks 必须大于 0");
    }
}

}  // namespace taskflow::common::config
