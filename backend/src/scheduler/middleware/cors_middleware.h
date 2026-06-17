#pragma once

#include <drogon/HttpFilter.h>

namespace taskflow::scheduler::middleware {

class CorsFilter : public drogon::HttpFilter<CorsFilter, false> {
public:
    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;
};

}  // namespace taskflow::scheduler::middleware
