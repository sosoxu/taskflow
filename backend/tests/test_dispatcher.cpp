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
