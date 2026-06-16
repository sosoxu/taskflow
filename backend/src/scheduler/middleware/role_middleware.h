#pragma once

#include <drogon/HttpFilter.h>

namespace taskflow::scheduler::middleware {

class RoleFilter : public drogon::HttpFilter<RoleFilter, false> {
public:
    RoleFilter() = default;

    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;
};

}  // namespace taskflow::scheduler::middleware
