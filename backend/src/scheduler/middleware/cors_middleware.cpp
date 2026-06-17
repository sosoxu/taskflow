#include "scheduler/middleware/cors_middleware.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace taskflow::scheduler::middleware {

void CorsFilter::doFilter(
    const drogon::HttpRequestPtr& req,
    drogon::FilterCallback&& fcb,
    drogon::FilterChainCallback&& fccb) {

    // Handle OPTIONS preflight request
    if (req->method() == drogon::Options) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        resp->addHeader("Access-Control-Max-Age", "86400");
        fcb(resp);
        return;
    }

    fccb();
}

}  // namespace taskflow::scheduler::middleware
