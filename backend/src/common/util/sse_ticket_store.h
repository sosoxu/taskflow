#pragma once

#include <optional>
#include <string>

namespace taskflow::common::util {

// 一次性 SSE 票据的内容：签发时的身份 + 绑定的资源。
// 消费成功时返回，过滤器和控制器据此完成认证与绑定核对。
struct SseTicket {
    std::string user_id;
    std::string username;
    std::string role;
    std::string instance_id;
    std::string task_instance_id;
};

// SseTicketStore: 基于 PostgreSQL 的一次性 SSE 票据，支持多实例共享。
//
// 背景（Fix #343）：EventSource 不支持自定义请求头，此前 SSE 只能把
// access_token 放进 URL query，token 会落进 nginx/代理访问日志与浏览器历史。
// 改为：先用正常带 Authorization 头的请求换发 ticket，再用 ticket 建立 SSE。
//
// 设计：
// - 与 TokenBlacklist 一致以 DB 为权威源，多实例（HA）下不依赖粘性会话；
//   内存缓存不适用，因为票据是单次消费，缓存反而会破坏「一次有效」。
// - 票据绑定 (user_id, instance_id, task_instance_id)，且带过期时间。
// - 消费用单条 DELETE ... RETURNING 完成，天然原子：并发/重放只有一方成功。
// - 任何 DB 异常都 fail-closed（返回空 ticket / nullopt），不放行。
class SseTicketStore {
public:
    // 票据有效期（秒）。够前端建立连接即可，越短泄露窗口越小。
    static constexpr int kDefaultTtlSeconds = 30;

    static SseTicketStore& instance() {
        static SseTicketStore inst;
        return inst;
    }

    // 签发票据；失败（DB 异常）返回空字符串，调用方应视为 500/503。
    std::string issue(const std::string& user_id,
                      const std::string& username,
                      const std::string& role,
                      const std::string& instance_id,
                      const std::string& task_instance_id,
                      int ttl_seconds = kDefaultTtlSeconds);

    // 消费票据：单次有效。无效、已过期或已消费均返回 nullopt。
    // 注意：绑定核对（instance_id/task_instance_id 是否与请求资源一致）由调用方
    // 拿返回结果自己比对——过滤器里读不到路径参数，所以不在这里做。
    std::optional<SseTicket> consume(const std::string& ticket);

private:
    SseTicketStore() = default;
};

}  // namespace taskflow::common::util
