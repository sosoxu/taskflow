#pragma once
#include <string>

#include <grpcpp/grpcpp.h>

namespace taskflow::common::util {

// Fix #326: scheduler ↔ worker gRPC 内部认证。
// 双方配置相同的 auth_token；客户端将其写入 metadata，服务端逐一校验。
// token 为空表示未启用认证（兼容旧部署/本地开发，由调用方打启动警告）。
class GrpcAuthUtil {
public:
    static constexpr const char* kMetadataKey = "x-internal-auth";

    // 客户端：将内部 token 写入请求 metadata（token 为空时跳过）
    static void applyAuth(::grpc::ClientContext& context, const std::string& token) {
        if (!token.empty()) {
            context.AddMetadata(kMetadataKey, token);
        }
    }

    // 服务端：校验请求 metadata。expected 为空表示未启用认证，直接放行。
    static ::grpc::Status checkAuth(::grpc::ServerContextBase* context,
                                    const std::string& expected) {
        if (expected.empty()) {
            return ::grpc::Status::OK;
        }
        const auto& metadata = context->client_metadata();
        auto it = metadata.find(kMetadataKey);
        if (it == metadata.end()) {
            return ::grpc::Status(::grpc::StatusCode::UNAUTHENTICATED,
                                  "missing internal auth token");
        }
        if (std::string(it->second.data(), it->second.size()) != expected) {
            return ::grpc::Status(::grpc::StatusCode::UNAUTHENTICATED,
                                  "invalid internal auth token");
        }
        return ::grpc::Status::OK;
    }
};

}  // namespace taskflow::common::util
