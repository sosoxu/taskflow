#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "scheduler/engine/dispatcher.h"
#include "common/models/worker_info.h"

using namespace taskflow::scheduler::engine;
using namespace taskflow::common::models;

// Helper: build a WorkerInfo with common defaults
static WorkerInfo makeWorker(const std::string& id, const std::string& status,
                             int running, int max_tasks,
                             const nlohmann::json& tags = nlohmann::json::array()) {
    WorkerInfo w;
    w.id = id;
    w.status = status;
    w.running_tasks = running;
    w.max_tasks = max_tasks;
    w.resource_tags = tags;
    return w;
}

TEST_CASE("RandomDispatcher selects from online workers", "[dispatcher]") {
    RandomDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    WorkerInfo w1;
    w1.id = "w1"; w1.status = "online"; w1.running_tasks = 0; w1.max_tasks = 10;
    WorkerInfo w2;
    w2.id = "w2"; w2.status = "online"; w2.running_tasks = 0; w2.max_tasks = 10;
    workers.push_back(w1);
    workers.push_back(w2);

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE((result.value().id == "w1" || result.value().id == "w2"));
}

TEST_CASE("RandomDispatcher fails with empty worker list", "[dispatcher]") {
    // Fix #261: 修正误导性测试名 —— 实际传入空 vector，源码不按 status 过滤
    RandomDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("没有在线") != std::string::npos);
}

TEST_CASE("RandomDispatcher selects from workers regardless of status field", "[dispatcher]") {
    // Fix #261: 验证源码不按 status 字段过滤 —— 传入 status="offline" 的 worker 仍会被选中
    RandomDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "offline", 0, 10));  // status 非 online
    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "w1");
}

TEST_CASE("LoadBalanceDispatcher selects least loaded", "[dispatcher]") {
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    WorkerInfo w1;
    w1.id = "w1"; w1.status = "online"; w1.running_tasks = 5; w1.max_tasks = 10;
    WorkerInfo w2;
    w2.id = "w2"; w2.status = "online"; w2.running_tasks = 2; w2.max_tasks = 10;
    workers.push_back(w1);
    workers.push_back(w2);

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "w2");
}

// Fix #200: LoadBalanceDispatcher must compare load RATIO (running/max), not
// absolute running_tasks. A high-capacity worker with more absolute tasks but
// lower utilization should be preferred over a small-capacity worker that is
// more loaded relative to its limit.
TEST_CASE("LoadBalanceDispatcher uses load ratio not absolute count", "[dispatcher]") {
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    WorkerInfo big;
    big.id = "big"; big.status = "online"; big.running_tasks = 10; big.max_tasks = 100;  // 10% load
    WorkerInfo small;
    small.id = "small"; small.status = "online"; small.running_tasks = 3; small.max_tasks = 2;  // 150% load
    workers.push_back(big);
    workers.push_back(small);

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    // big has lower ratio (0.1 < 1.5) even though it has more absolute tasks
    REQUIRE(result.value().id == "big");
}

TEST_CASE("LoadBalanceDispatcher avoids zero-capacity workers", "[dispatcher]") {
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    WorkerInfo zero;
    zero.id = "zero"; zero.status = "online"; zero.running_tasks = 0; zero.max_tasks = 0;
    WorkerInfo normal;
    normal.id = "normal"; normal.status = "online"; normal.running_tasks = 5; normal.max_tasks = 10;
    workers.push_back(zero);
    workers.push_back(normal);

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "normal");
}

TEST_CASE("SpecifiedDispatcher selects target worker", "[dispatcher]") {
    SpecifiedDispatcher dispatcher("w2");

    std::vector<WorkerInfo> workers;
    WorkerInfo w1;
    w1.id = "w1"; w1.status = "online"; w1.running_tasks = 0; w1.max_tasks = 10;
    WorkerInfo w2;
    w2.id = "w2"; w2.status = "online"; w2.running_tasks = 0; w2.max_tasks = 10;
    workers.push_back(w1);
    workers.push_back(w2);

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "w2");
}

TEST_CASE("SpecifiedDispatcher fails when target not in list", "[dispatcher]") {
    SpecifiedDispatcher dispatcher("w3");

    std::vector<WorkerInfo> workers;
    WorkerInfo w1;
    w1.id = "w1"; w1.status = "online"; w1.running_tasks = 0; w1.max_tasks = 10;
    workers.push_back(w1);

    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
}

// ============================================================================
// Fix #238: filterByResourceTags 单元测试
// 原测试套件对该函数零覆盖。该函数按资源标签过滤 Worker，是调度前置步骤。
// ============================================================================

TEST_CASE("filterByResourceTags: empty required_tags returns all workers", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"gpu"})));
    workers.push_back(makeWorker("w2", "online", 0, 10, nlohmann::json::array({"highmem"})));

    // 空 JSON (null) 返回所有
    auto filtered = filterByResourceTags(workers, nlohmann::json());
    REQUIRE(filtered.size() == 2);

    // 空数组返回所有
    filtered = filterByResourceTags(workers, nlohmann::json::array());
    REQUIRE(filtered.size() == 2);
}

TEST_CASE("filterByResourceTags: non-array required_tags returns all workers", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"gpu"})));

    // 非数组类型 (object/string/number) 返回所有，向后兼容
    auto filtered = filterByResourceTags(workers, nlohmann::json::object());
    REQUIRE(filtered.size() == 1);
    filtered = filterByResourceTags(workers, nlohmann::json("gpu"));
    REQUIRE(filtered.size() == 1);
}

TEST_CASE("filterByResourceTags: worker with superset tags is eligible", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"gpu", "highmem", "ssd"})));
    workers.push_back(makeWorker("w2", "online", 0, 10, nlohmann::json::array({"gpu"})));

    // 要求 ["gpu", "highmem"]，w1 有超集，w2 缺 highmem
    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu", "highmem"}));
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].id == "w1");
}

TEST_CASE("filterByResourceTags: worker with no tags is filtered out when tags required", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10));  // 默认空 tags
    workers.push_back(makeWorker("w2", "online", 0, 10, nlohmann::json::array({"gpu"})));

    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu"}));
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].id == "w2");
}

TEST_CASE("filterByResourceTags: no worker matches returns empty", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"cpu"})));
    workers.push_back(makeWorker("w2", "online", 0, 10, nlohmann::json::array({"highmem"})));

    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu"}));
    REQUIRE(filtered.empty());
}

TEST_CASE("filterByResourceTags: worker must have ALL required tags", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"gpu", "highmem"})));  // 有全部
    workers.push_back(makeWorker("w2", "online", 0, 10, nlohmann::json::array({"gpu", "ssd"})));      // 缺 highmem
    workers.push_back(makeWorker("w3", "online", 0, 10, nlohmann::json::array({"highmem", "ssd"})));  // 缺 gpu

    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu", "highmem"}));
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].id == "w1");
}

TEST_CASE("filterByResourceTags: non-string elements in required_tags are ignored", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"gpu"})));

    // 数组含数字和字符串，只有字符串 "gpu" 被当作有效要求
    nlohmann::json req = nlohmann::json::array({123, "gpu", true});
    auto filtered = filterByResourceTags(workers, req);
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].id == "w1");
}

TEST_CASE("filterByResourceTags: required_tags with only non-string elements returns all", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"gpu"})));
    workers.push_back(makeWorker("w2", "online", 0, 10));

    // 全是非字符串元素 → required 集合为空 → 返回所有 (向后兼容)
    nlohmann::json req = nlohmann::json::array({123, true, 45.6});
    auto filtered = filterByResourceTags(workers, req);
    REQUIRE(filtered.size() == 2);
}

TEST_CASE("filterByResourceTags: empty workers list returns empty", "[dispatcher_filter]") {
    std::vector<WorkerInfo> workers;
    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu"}));
    REQUIRE(filtered.empty());
}

// ============================================================================
// Fix #239: Dispatcher 边界测试（满载/平局/单元素/空列表）
// 原测试只覆盖 status="online" 的正常场景，缺少边界情况。
// ============================================================================

TEST_CASE("LoadBalanceDispatcher: tie returns first worker", "[dispatcher]") {
    // Fix #239: 所有 Worker 负载比相同时，应返回第一个（index 0）
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 5, 10));  // 50%
    workers.push_back(makeWorker("w2", "online", 5, 10));  // 50%
    workers.push_back(makeWorker("w3", "online", 5, 10));  // 50%

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    // 实现使用严格 < 比较，平局时保留第一个最小值
    REQUIRE(result.value().id == "w1");
}

TEST_CASE("LoadBalanceDispatcher: single worker", "[dispatcher]") {
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("only", "online", 3, 10));

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "only");
}

TEST_CASE("LoadBalanceDispatcher: mixed load selects lowest ratio", "[dispatcher]") {
    // Fix #261: 修正误导性测试名 —— 原 "all workers at full capacity" 实际是混合负载
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 10, 10));  // 100%
    workers.push_back(makeWorker("w2", "online", 8, 10));   // 80%
    workers.push_back(makeWorker("w3", "online", 10, 10));  // 100%

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "w2");  // 80% 是最低
}

TEST_CASE("LoadBalanceDispatcher: all workers zero capacity returns first", "[dispatcher]") {
    // Fix #261: 全部 worker 为零容量（max_tasks=0），所有 ratio 均为 1e9，应返回第一个
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 0));  // ratio = 1e9
    workers.push_back(makeWorker("w2", "online", 0, 0));  // ratio = 1e9
    workers.push_back(makeWorker("w3", "online", 0, 0));  // ratio = 1e9

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "w1");  // 严格 < 比较，平局保留第一个
}

TEST_CASE("LoadBalanceDispatcher: negative max_tasks treated as zero capacity", "[dispatcher]") {
    // Fix #261: 负数 max_tasks 走 max_tasks <= 0 分支，ratio = 1e9
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, -1));   // ratio = 1e9
    workers.push_back(makeWorker("w2", "online", 5, 10));   // ratio = 0.5

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "w2");
}

TEST_CASE("LoadBalanceDispatcher: empty list fails", "[dispatcher]") {
    LoadBalanceDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("RandomDispatcher: single worker", "[dispatcher]") {
    RandomDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("only", "online", 0, 10));

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "only");
}

TEST_CASE("SpecifiedDispatcher: empty list fails", "[dispatcher]") {
    SpecifiedDispatcher dispatcher("w1");

    std::vector<WorkerInfo> workers;
    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("SpecifiedDispatcher: target is first in list", "[dispatcher]") {
    // Fix #239: 边界 —— 目标在列表首位
    SpecifiedDispatcher dispatcher("w1");

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10));
    workers.push_back(makeWorker("w2", "online", 0, 10));

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "w1");
}

TEST_CASE("SpecifiedDispatcher: target is last in list", "[dispatcher]") {
    // Fix #239: 边界 —— 目标在列表末尾
    SpecifiedDispatcher dispatcher("w3");

    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10));
    workers.push_back(makeWorker("w2", "online", 0, 10));
    workers.push_back(makeWorker("w3", "online", 0, 10));

    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "w3");
}

// ============================================================================
// Fix #261: 补充 error 消息断言、filterByResourceTags worker 端非数组分支、
// SpecifiedDispatcher 边界测试
// ============================================================================

TEST_CASE("LoadBalanceDispatcher: empty list error message", "[dispatcher]") {
    // Fix #261: 验证空列表的错误消息
    LoadBalanceDispatcher dispatcher;
    std::vector<WorkerInfo> workers;
    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("没有在线") != std::string::npos);
}

TEST_CASE("SpecifiedDispatcher: target not in list error message", "[dispatcher]") {
    // Fix #261: 验证目标不在列表的错误消息含 worker_id
    SpecifiedDispatcher dispatcher("w3");
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10));
    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("w3") != std::string::npos);
}

TEST_CASE("SpecifiedDispatcher: empty list error message", "[dispatcher]") {
    // Fix #261: 验证空列表的错误消息
    SpecifiedDispatcher dispatcher("w1");
    std::vector<WorkerInfo> workers;
    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().find("w1") != std::string::npos);
}

TEST_CASE("SpecifiedDispatcher: empty string worker_id not found", "[dispatcher]") {
    // Fix #261: 空字符串 worker_id 匹配 id=="" 的 worker（通常不存在），返回 failure
    SpecifiedDispatcher dispatcher("");
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10));
    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("SpecifiedDispatcher: duplicate worker IDs returns first match", "[dispatcher]") {
    // Fix #261: 列表中有两个相同 ID 的 worker，应返回第一个匹配
    SpecifiedDispatcher dispatcher("dup");
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("dup", "online", 1, 10));  // 第一个
    workers.push_back(makeWorker("dup", "online", 2, 10));  // 第二个（同 ID）
    auto result = dispatcher.selectWorker(workers);
    REQUIRE(result.ok());
    REQUIRE(result.value().id == "dup");
    REQUIRE(result.value().running_tasks == 1);  // 返回第一个
}

TEST_CASE("filterByResourceTags: worker with non-array resource_tags is filtered out", "[dispatcher_filter]") {
    // Fix #261: worker.resource_tags 为非数组类型（object/string）时被视为空标签集，要求任何标签时被过滤
    std::vector<WorkerInfo> workers;
    WorkerInfo w1;
    w1.id = "w1"; w1.status = "online"; w1.running_tasks = 0; w1.max_tasks = 10;
    w1.resource_tags = nlohmann::json::object();  // 非数组
    workers.push_back(w1);

    WorkerInfo w2;
    w2.id = "w2"; w2.status = "online"; w2.running_tasks = 0; w2.max_tasks = 10;
    w2.resource_tags = "gpu";  // 非数组（字符串）
    workers.push_back(w2);

    WorkerInfo w3;
    w3.id = "w3"; w3.status = "online"; w3.running_tasks = 0; w3.max_tasks = 10;
    w3.resource_tags = nlohmann::json::array({"gpu"});  // 正常数组
    workers.push_back(w3);

    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu"}));
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].id == "w3");  // 只有 w3 有正确的数组标签
}

TEST_CASE("filterByResourceTags: worker with mixed-type tags only counts strings", "[dispatcher_filter]") {
    // Fix #261: worker.resource_tags 数组含非字符串元素，只有字符串元素被计入
    std::vector<WorkerInfo> workers;
    WorkerInfo w1;
    w1.id = "w1"; w1.status = "online"; w1.running_tasks = 0; w1.max_tasks = 10;
    w1.resource_tags = nlohmann::json::array({123, "gpu", true});  // 混合类型
    workers.push_back(w1);

    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu"}));
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].id == "w1");
}

TEST_CASE("filterByResourceTags: duplicate required tags are deduplicated", "[dispatcher_filter]") {
    // Fix #261: required_tags 含重复标签，去重后行为与单个标签相同
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"gpu"})));

    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu", "gpu"}));
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].id == "w1");
}

TEST_CASE("filterByResourceTags: multiple matches preserve input order", "[dispatcher_filter]") {
    // Fix #261: 多个 worker 都匹配时，输出顺序与输入一致
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10, nlohmann::json::array({"gpu"})));
    workers.push_back(makeWorker("w2", "online", 0, 10, nlohmann::json::array({"gpu", "highmem"})));
    workers.push_back(makeWorker("w3", "online", 0, 10, nlohmann::json::array({"gpu"})));

    auto filtered = filterByResourceTags(workers, nlohmann::json::array({"gpu"}));
    REQUIRE(filtered.size() == 3);
    REQUIRE(filtered[0].id == "w1");
    REQUIRE(filtered[1].id == "w2");
    REQUIRE(filtered[2].id == "w3");
}

TEST_CASE("RandomDispatcher: multiple calls cover different workers", "[dispatcher]") {
    // Fix #261: 循环调用 100 次，验证两个 worker 都至少被选中一次
    RandomDispatcher dispatcher;
    std::vector<WorkerInfo> workers;
    workers.push_back(makeWorker("w1", "online", 0, 10));
    workers.push_back(makeWorker("w2", "online", 0, 10));

    bool saw_w1 = false, saw_w2 = false;
    for (int i = 0; i < 100; ++i) {
        auto result = dispatcher.selectWorker(workers);
        REQUIRE(result.ok());
        if (result.value().id == "w1") saw_w1 = true;
        if (result.value().id == "w2") saw_w2 = true;
    }
    // 统计上 100 次调用应覆盖两个 worker（概率极低不覆盖）
    REQUIRE(saw_w1);
    REQUIRE(saw_w2);
}
