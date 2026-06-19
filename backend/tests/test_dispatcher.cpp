#include <catch2/catch_test_macros.hpp>
#include "scheduler/engine/dispatcher.h"
#include "common/models/worker_info.h"

using namespace taskflow::scheduler::engine;
using namespace taskflow::common::models;

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

TEST_CASE("RandomDispatcher fails with no online workers", "[dispatcher]") {
    RandomDispatcher dispatcher;

    std::vector<WorkerInfo> workers;
    auto result = dispatcher.selectWorker(workers);
    REQUIRE_FALSE(result.ok());
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
