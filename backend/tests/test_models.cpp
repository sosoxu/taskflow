#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "common/models/task.h"
#include "common/models/workflow.h"
#include "common/models/task_instance.h"
#include "common/models/worker_info.h"
#include "common/models/user.h"
#include "common/models/workflow_instance.h"

using namespace taskflow::common::models;

// ============================================================================
// Task 模型序列化测试
// 验收指标：
//   1. toJson 包含所有字段
//   2. 默认值正确
//   3. 自定义值正确序列化
// ============================================================================

TEST_CASE("Task: default values", "[model_task]") {
    Task task;
    REQUIRE(task.timeout == 3600);
    REQUIRE(task.max_retries == 0);
    REQUIRE(task.retry_interval == 60);
    REQUIRE(task.version == 1);
    REQUIRE(task.deleted == false);
    REQUIRE(task.type.empty());
    REQUIRE(task.name.empty());
}

TEST_CASE("Task: toJson contains all fields", "[model_task]") {
    Task task;
    task.id = "task-001";
    task.name = "test-task";
    task.type = "command";
    task.config_json = {{"command", "echo hello"}};
    task.description = "A test task";
    task.timeout = 120;
    task.max_retries = 3;
    task.retry_interval = 30;
    task.resource_tags = {{"gpu", "true"}};
    task.parameters_json = {{"key1", "val1"}};
    task.creator_id = "user-001";
    task.version = 2;
    task.deleted = false;
    task.created_at = "2025-01-01T00:00:00Z";
    task.updated_at = "2025-01-02T00:00:00Z";

    auto j = task.toJson();
    REQUIRE(j["id"] == "task-001");
    REQUIRE(j["name"] == "test-task");
    REQUIRE(j["type"] == "command");
    REQUIRE(j["config_json"]["command"] == "echo hello");
    REQUIRE(j["description"] == "A test task");
    REQUIRE(j["timeout"] == 120);
    REQUIRE(j["max_retries"] == 3);
    REQUIRE(j["retry_interval"] == 30);
    REQUIRE(j["resource_tags"]["gpu"] == "true");
    REQUIRE(j["parameters_json"]["key1"] == "val1");
    REQUIRE(j["creator_id"] == "user-001");
    REQUIRE(j["version"] == 2);
    REQUIRE(j["deleted"] == false);
    REQUIRE(j["created_at"] == "2025-01-01T00:00:00Z");
    REQUIRE(j["updated_at"] == "2025-01-02T00:00:00Z");
}

TEST_CASE("Task: toJson with empty config", "[model_task]") {
    Task task;
    task.id = "task-002";
    task.name = "empty-config";
    task.type = "sql";
    task.config_json = nlohmann::json::object();

    auto j = task.toJson();
    REQUIRE(j["config_json"].is_object());
    REQUIRE(j["type"] == "sql");
}

// ============================================================================
// Workflow 模型序列化测试
// ============================================================================

TEST_CASE("Workflow: default values", "[model_workflow]") {
    Workflow wf;
    REQUIRE(wf.cron_enabled == false);
    REQUIRE(wf.version == 1);
    REQUIRE(wf.deleted == false);
    REQUIRE(wf.schedule_strategy.empty());
    REQUIRE(wf.target_worker_id.empty());
    REQUIRE(wf.cron_expression.empty());
}

TEST_CASE("Workflow: toJson contains all fields", "[model_workflow]") {
    Workflow wf;
    wf.id = "wf-001";
    wf.name = "test-workflow";
    wf.description = "A test workflow";
    wf.dag_json = {
        {"nodes", nlohmann::json::array({{{"id", "n1"}, {"task_id", "t1"}}})},
        {"edges", nlohmann::json::array()}
    };
    wf.schedule_strategy = "random";
    wf.target_worker_id = "";
    wf.cron_expression = "0 */5 * * * *";
    wf.cron_enabled = true;
    wf.creator_id = "user-001";
    wf.version = 3;
    wf.deleted = false;
    wf.created_at = "2025-01-01T00:00:00Z";
    wf.updated_at = "2025-01-02T00:00:00Z";

    auto j = wf.toJson();
    REQUIRE(j["id"] == "wf-001");
    REQUIRE(j["name"] == "test-workflow");
    REQUIRE(j["description"] == "A test workflow");
    REQUIRE(j["dag_json"]["nodes"].size() == 1);
    REQUIRE(j["schedule_strategy"] == "random");
    REQUIRE(j["cron_expression"] == "0 */5 * * * *");
    REQUIRE(j["cron_enabled"] == true);
    REQUIRE(j["creator_id"] == "user-001");
    REQUIRE(j["version"] == 3);
    REQUIRE(j["deleted"] == false);
}

TEST_CASE("Workflow: toJson with specified strategy", "[model_workflow]") {
    Workflow wf;
    wf.id = "wf-002";
    wf.name = "specified-wf";
    wf.schedule_strategy = "specified";
    wf.target_worker_id = "worker-001";

    auto j = wf.toJson();
    REQUIRE(j["schedule_strategy"] == "specified");
    REQUIRE(j["target_worker_id"] == "worker-001");
}

// ============================================================================
// TaskInstance 模型序列化测试
// ============================================================================

TEST_CASE("TaskInstance: default values", "[model_task_instance]") {
    TaskInstance ti;
    REQUIRE(ti.task_version == 0);
    REQUIRE(ti.retry_count == 0);
    REQUIRE(ti.exit_code == 0);
    REQUIRE(ti.status.empty());
    REQUIRE(ti.worker_id.empty());
    REQUIRE(ti.error_message.empty());
}

TEST_CASE("TaskInstance: toJson contains all fields", "[model_task_instance]") {
    TaskInstance ti;
    ti.id = "ti-001";
    ti.workflow_instance_id = "wi-001";
    ti.task_id = "task-001";
    ti.node_id = "n1";
    ti.task_version = 2;
    ti.task_name = "my-task";
    ti.status = "SUCCESS";
    ti.worker_id = "worker-001";
    ti.retry_count = 1;
    ti.started_at = "2025-01-01T00:00:01Z";
    ti.finished_at = "2025-01-01T00:00:05Z";
    ti.exit_code = 0;
    ti.error_message = "";
    ti.created_at = "2025-01-01T00:00:00Z";

    auto j = ti.toJson();
    REQUIRE(j["id"] == "ti-001");
    REQUIRE(j["workflow_instance_id"] == "wi-001");
    REQUIRE(j["task_id"] == "task-001");
    REQUIRE(j["node_id"] == "n1");
    REQUIRE(j["task_version"] == 2);
    REQUIRE(j["task_name"] == "my-task");
    REQUIRE(j["status"] == "SUCCESS");
    REQUIRE(j["worker_id"] == "worker-001");
    REQUIRE(j["retry_count"] == 1);
    REQUIRE(j["exit_code"] == 0);
}

TEST_CASE("TaskInstance: toJson with FAILED status", "[model_task_instance]") {
    TaskInstance ti;
    ti.id = "ti-002";
    ti.status = "FAILED";
    ti.exit_code = 1;
    ti.error_message = "Command exited with code 1";

    auto j = ti.toJson();
    REQUIRE(j["status"] == "FAILED");
    REQUIRE(j["exit_code"] == 1);
    REQUIRE(j["error_message"] == "Command exited with code 1");
}

TEST_CASE("TaskInstance: toJson with UPSTREAM_FAILED status", "[model_task_instance]") {
    TaskInstance ti;
    ti.id = "ti-003";
    ti.status = "UPSTREAM_FAILED";
    ti.exit_code = -1;
    ti.error_message = "Upstream task failed";

    auto j = ti.toJson();
    REQUIRE(j["status"] == "UPSTREAM_FAILED");
    REQUIRE(j["exit_code"] == -1);
}

// ============================================================================
// WorkerInfo 模型序列化测试
// ============================================================================

TEST_CASE("WorkerInfo: default values", "[model_worker]") {
    WorkerInfo w;
    REQUIRE(w.running_tasks == 0);
    REQUIRE(w.max_tasks == 10);
    REQUIRE(w.status.empty());
    REQUIRE(w.name.empty());
}

TEST_CASE("WorkerInfo: toJson contains all fields", "[model_worker]") {
    WorkerInfo w;
    w.id = "worker-001";
    w.name = "worker-1";
    w.address = "localhost:50052";
    w.status = "online";
    w.running_tasks = 3;
    w.max_tasks = 10;
    w.cpu_usage = 45.5;
    w.memory_usage = 60.2;
    w.last_heartbeat = "2025-01-01T00:00:00Z";

    auto j = w.toJson();
    REQUIRE(j["id"] == "worker-001");
    REQUIRE(j["name"] == "worker-1");
    REQUIRE(j["address"] == "localhost:50052");
    REQUIRE(j["status"] == "online");
    REQUIRE(j["running_tasks"] == 3);
    REQUIRE(j["max_tasks"] == 10);
}

// ============================================================================
// User 模型序列化测试
// ============================================================================

TEST_CASE("User: default values", "[model_user]") {
    User u;
    REQUIRE(u.role.empty());
    REQUIRE(u.username.empty());
}

TEST_CASE("User: toJson contains all fields", "[model_user]") {
    User u;
    u.id = "user-001";
    u.username = "testadmin";
    u.role = "admin";
    u.created_at = "2025-01-01T00:00:00Z";
    u.updated_at = "2025-01-02T00:00:00Z";

    auto j = u.toJson();
    REQUIRE(j["id"] == "user-001");
    REQUIRE(j["username"] == "testadmin");
    REQUIRE(j["role"] == "admin");
}

// ============================================================================
// WorkflowInstance 模型序列化测试
// ============================================================================

TEST_CASE("WorkflowInstance: default values", "[model_workflow_instance]") {
    WorkflowInstance wi;
    REQUIRE(wi.status.empty());
    REQUIRE(wi.trigger_type.empty());
}

TEST_CASE("WorkflowInstance: toJson contains all fields", "[model_workflow_instance]") {
    WorkflowInstance wi;
    wi.id = "wi-001";
    wi.workflow_id = "wf-001";
    wi.status = "SUCCESS";
    wi.trigger_type = "manual";
    wi.creator_id = "user-001";
    wi.param_overrides = nlohmann::json::object();
    wi.started_at = "2025-01-01T00:00:01Z";
    wi.finished_at = "2025-01-01T00:00:10Z";
    wi.created_at = "2025-01-01T00:00:00Z";

    auto j = wi.toJson();
    REQUIRE(j["id"] == "wi-001");
    REQUIRE(j["workflow_id"] == "wf-001");
    REQUIRE(j["status"] == "SUCCESS");
    REQUIRE(j["trigger_type"] == "manual");
    REQUIRE(j["creator_id"] == "user-001");
}
