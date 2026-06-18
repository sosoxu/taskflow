#pragma once

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <spdlog/spdlog.h>
#include "common/config/scheduler_config.h"

namespace taskflow::common::util {

// Fix #126: Create a gRPC channel to a worker with optional TLS.
// When tls.enabled is true, reads the cert/key/ca files and creates an SSL
// channel; otherwise creates an insecure channel. This centralizes the
// scheduler→worker channel creation so DispatchTask, CancelTask, and GetTaskLog
// all respect the same TLS configuration.
inline std::shared_ptr<grpc::Channel> createWorkerChannel(
    const std::string& address,
    const taskflow::common::config::TlsConfig& tls) {
    if (!tls.enabled) {
        return ::grpc::CreateChannel(address, ::grpc::InsecureChannelCredentials());
    }

    grpc::SslCredentialsOptions ssl_opts;

    if (!tls.cert_path.empty()) {
        std::ifstream cert_file(tls.cert_path);
        if (cert_file.is_open()) {
            ssl_opts.pem_cert_chain = std::string(
                std::istreambuf_iterator<char>(cert_file),
                std::istreambuf_iterator<char>());
        } else {
            spdlog::warn("createWorkerChannel: failed to open cert file: {}", tls.cert_path);
        }
    }
    if (!tls.key_path.empty()) {
        std::ifstream key_file(tls.key_path);
        if (key_file.is_open()) {
            ssl_opts.pem_private_key = std::string(
                std::istreambuf_iterator<char>(key_file),
                std::istreambuf_iterator<char>());
        } else {
            spdlog::warn("createWorkerChannel: failed to open key file: {}", tls.key_path);
        }
    }
    if (!tls.ca_path.empty()) {
        std::ifstream ca_file(tls.ca_path);
        if (ca_file.is_open()) {
            ssl_opts.pem_root_certs = std::string(
                std::istreambuf_iterator<char>(ca_file),
                std::istreambuf_iterator<char>());
        } else {
            spdlog::warn("createWorkerChannel: failed to open CA file: {}", tls.ca_path);
        }
    }

    return ::grpc::CreateChannel(address, grpc::SslCredentials(ssl_opts));
}

// Fix #127: Retry a scheduler→worker gRPC call with exponential backoff.
// `rpc_call` is a callable that creates a fresh ClientContext, sets a deadline,
// and invokes the RPC, returning grpc::Status. Retries on transient failures
// (UNAVAILABLE, DEADLINE_EXCEEDED) up to max_retries times with exponential
// backoff (100ms, 200ms, 400ms, ...). Non-transient errors are returned
// immediately without retry.
template <typename RpcCall>
::grpc::Status retryWorkerRpc(RpcCall rpc_call, int max_retries = 3) {
    ::grpc::Status status = rpc_call();
    if (status.ok()) {
        return status;
    }

    for (int i = 0; i < max_retries; ++i) {
        // Only retry transient errors
        auto code = status.error_code();
        if (code != ::grpc::StatusCode::UNAVAILABLE &&
            code != ::grpc::StatusCode::DEADLINE_EXCEEDED) {
            return status;
        }

        int delay_ms = 100 * (1 << i);  // 100ms, 200ms, 400ms
        spdlog::warn("retryWorkerRpc: transient gRPC failure (attempt {}/{}), retrying in {}ms: {}",
                     i + 1, max_retries, delay_ms, status.error_message());
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        status = rpc_call();
        if (status.ok()) {
            return status;
        }
    }
    return status;
}

}  // namespace taskflow::common::util
