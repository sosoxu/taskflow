#pragma once

#include <string>
#include <drogon/HttpFilter.h>
#include "common/util/jwt_util.h"

namespace taskflow::scheduler::middleware {

class AuthFilter : public drogon::HttpFilter<AuthFilter, false> {
public:
    explicit AuthFilter(const std::string& jwt_secret);

    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;

private:
    std::string jwt_secret_;
};

}  // namespace taskflow::scheduler::middleware
