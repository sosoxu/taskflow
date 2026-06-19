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
    REQUIRE(wi.workflow_version == 0);
    REQUIRE(wi.id.empty());
    REQUIRE(wi.workflow_id.empty());
    REQUIRE(wi.creator_id.empty());
    REQUIRE(wi.started_at.empty());
    REQUIRE(wi.finished_at.empty());
    REQUIRE(wi.created_at.empty());
}

TEST_CASE("WorkflowInstance: toJson contains all fields", "[model_workflow_instance]") {
    // Fix #242: 原测试设置了 created_at 但未验证 toJson 输出，且未设置/验证
    // workflow_version 字段。toJson() 输出包含 workflow_version 和 created_at。
    WorkflowInstance wi;
    wi.id = "wi-001";
    wi.workflow_id = "wf-001";
    wi.workflow_version = 3;
    wi.status = "SUCCESS";
    wi.trigger_type = "manual";
    wi.creator_id = "user-001";
    wi.param_overrides = nlohmann::json::object({{"key", "val"}});
    wi.started_at = "2025-01-01T00:00:01Z";
    wi.finished_at = "2025-01-01T00:00:10Z";
    wi.created_at = "2025-01-01T00:00:00Z";

    auto j = wi.toJson();
    REQUIRE(j["id"] == "wi-001");
    REQUIRE(j["workflow_id"] == "wf-001");
    REQUIRE(j["workflow_version"] == 3);
    REQUIRE(j["status"] == "SUCCESS");
    REQUIRE(j["trigger_type"] == "manual");
    REQUIRE(j["creator_id"] == "user-001");
    REQUIRE(j["param_overrides"]["key"] == "val");
    REQUIRE(j["started_at"] == "2025-01-01T00:00:01Z");
    REQUIRE(j["finished_at"] == "2025-01-01T00:00:10Z");
    REQUIRE(j["created_at"] == "2025-01-01T00:00:00Z");
}

TEST_CASE("WorkflowInstance: toJson with default param_overrides", "[model_workflow_instance]") {
    // Fix #242: 验证 param_overrides 默认值（空 JSON）正确序列化
    WorkflowInstance wi;
    wi.id = "wi-002";
    wi.workflow_id = "wf-002";
    wi.status = "PENDING";
    wi.trigger_type = "cron";

    auto j = wi.toJson();
    REQUIRE(j["id"] == "wi-002");
    REQUIRE(j["workflow_version"] == 0);
    REQUIRE(j["status"] == "PENDING");
    REQUIRE(j["trigger_type"] == "cron");
    // param_overrides 默认构造为 null（nlohmann::json 默认），toJson 直接输出
    REQUIRE(j.contains("param_overrides"));
}

// ============================================================================
// Fix #256: WorkerInfo/User/TaskInstance toJson 完整字段验证
// 验收指标：
//   1. WorkerInfo toJson 包含 cpu_usage/memory_usage/resource_tags/last_heartbeat/registered_at
//   2. User toJson 包含 password_hash
//   3. User toSafeJson 不含 password_hash
//   4. TaskInstance toJson 包含 started_at/finished_at/error_message/created_at
// ============================================================================

TEST_CASE("WorkerInfo: toJson includes all 11 fields", "[model_worker_full]") {
    // Fix #256: 验证 WorkerInfo toJson 输出全部 11 个字段
    WorkerInfo w;
    w.id = "worker-full";
    w.name = "worker-full-name";
    w.address = "10.0.0.1:50052";
    w.status = "online";
    w.cpu_usage = 75.5;
    w.memory_usage = 82.3;
    w.running_tasks = 5;
    w.max_tasks = 20;
    w.resource_tags = {{"gpu", "A100"}, {"zone", "us-east-1a"}};
    w.last_heartbeat = "2025-06-20T10:00:00Z";
    w.registered_at = "2025-01-01T00:00:00Z";

    auto j = w.toJson();

    // 验证全部 11 个字段
    REQUIRE(j["id"] == "worker-full");
    REQUIRE(j["name"] == "worker-full-name");
    REQUIRE(j["address"] == "10.0.0.1:50052");
    REQUIRE(j["status"] == "online");
    REQUIRE(j["cpu_usage"] == 75.5);
    REQUIRE(j["memory_usage"] == 82.3);
    REQUIRE(j["running_tasks"] == 5);
    REQUIRE(j["max_tasks"] == 20);
    REQUIRE(j["resource_tags"]["gpu"] == "A100");
    REQUIRE(j["resource_tags"]["zone"] == "us-east-1a");
    REQUIRE(j["last_heartbeat"] == "2025-06-20T10:00:00Z");
    REQUIRE(j["registered_at"] == "2025-01-01T00:00:00Z");

    // 确保恰好 11 个字段
    REQUIRE(j.size() == 11);
}

TEST_CASE("WorkerInfo: toJson with default resource_tags", "[model_worker_full]") {
    // Fix #256: 验证 resource_tags 默认值（null）正确输出
    WorkerInfo w;
    w.id = "worker-default-tags";
    w.name = "worker-default";
    w.address = "localhost:50052";
    w.status = "online";

    auto j = w.toJson();
    REQUIRE(j.contains("resource_tags"));
    REQUIRE(j["cpu_usage"] == 0.0);
    REQUIRE(j["memory_usage"] == 0.0);
    REQUIRE(j["last_heartbeat"] == "");
    REQUIRE(j["registered_at"] == "");
}

TEST_CASE("User: toJson includes password_hash", "[model_user_full]") {
    // Fix #256: 验证 toJson 包含 password_hash 字段
    User u;
    u.id = "user-full";
    u.username = "admin_user";
    u.password_hash = "$2b$12$somebcrypt_hash_value_here";
    u.role = "admin";
    u.created_at = "2025-01-01T00:00:00Z";
    u.updated_at = "2025-06-20T10:00:00Z";

    auto j = u.toJson();
    REQUIRE(j["id"] == "user-full");
    REQUIRE(j["username"] == "admin_user");
    REQUIRE(j["password_hash"] == "$2b$12$somebcrypt_hash_value_here");
    REQUIRE(j["role"] == "admin");
    REQUIRE(j["created_at"] == "2025-01-01T00:00:00Z");
    REQUIRE(j["updated_at"] == "2025-06-20T10:00:00Z");

    // toJson 应包含 6 个字段（含 password_hash）
    REQUIRE(j.size() == 6);
    REQUIRE(j.contains("password_hash"));
}

TEST_CASE("User: toSafeJson excludes password_hash", "[model_user_safe]") {
    // Fix #256: 验证 toSafeJson 不包含 password_hash 字段
    User u;
    u.id = "user-safe";
    u.username = "safe_user";
    u.password_hash = "$2b$12$secret_hash_should_not_leak";
    u.role = "viewer";
    u.created_at = "2025-01-01T00:00:00Z";
    u.updated_at = "2025-06-20T10:00:00Z";

    auto j = u.toSafeJson();
    REQUIRE(j["id"] == "user-safe");
    REQUIRE(j["username"] == "safe_user");
    REQUIRE(j["role"] == "viewer");
    REQUIRE(j["created_at"] == "2025-01-01T00:00:00Z");
    REQUIRE(j["updated_at"] == "2025-06-20T10:00:00Z");

    // toSafeJson 应不包含 password_hash
    REQUIRE_FALSE(j.contains("password_hash"));

    // toSafeJson 应包含 5 个字段（不含 password_hash）
    REQUIRE(j.size() == 5);
}

TEST_CASE("User: toJson vs toSafeJson difference", "[model_user_safe]") {
    // Fix #256: 对比 toJson 和 toSafeJson，唯一区别是 password_hash
    User u;
    u.id = "user-diff";
    u.username = "diff_user";
    u.password_hash = "$pbkdf2-sha256$i=10000$abc$def";
    u.role = "operator";
    u.created_at = "2025-01-01T00:00:00Z";
    u.updated_at = "2025-06-20T10:00:00Z";

    auto full = u.toJson();
    auto safe = u.toSafeJson();

    // safe 比 full 少一个字段
    REQUIRE(full.size() == safe.size() + 1);
    // full 有 password_hash，safe 没有
    REQUIRE(full.contains("password_hash"));
    REQUIRE_FALSE(safe.contains("password_hash"));
    // 其他字段相同
    REQUIRE(full["id"] == safe["id"]);
    REQUIRE(full["username"] == safe["username"]);
    REQUIRE(full["role"] == safe["role"]);
    REQUIRE(full["created_at"] == safe["created_at"]);
    REQUIRE(full["updated_at"] == safe["updated_at"]);
}

TEST_CASE("TaskInstance: toJson includes all 14 fields", "[model_task_instance_full]") {
    // Fix #256: 验证 TaskInstance toJson 输出全部 14 个字段
    TaskInstance ti;
    ti.id = "ti-full";
    ti.workflow_instance_id = "wi-full";
    ti.task_id = "task-full";
    ti.node_id = "node-1";
    ti.task_version = 5;
    ti.task_name = "full-task-name";
    ti.status = "FAILED";
    ti.worker_id = "worker-001";
    ti.retry_count = 3;
    ti.started_at = "2025-06-20T10:00:00Z";
    ti.finished_at = "2025-06-20T10:05:30Z";
    ti.exit_code = 1;
    ti.error_message = "Task failed due to timeout";
    ti.created_at = "2025-06-20T09:59:00Z";

    auto j = ti.toJson();

    // 验证全部 14 个字段
    REQUIRE(j["id"] == "ti-full");
    REQUIRE(j["workflow_instance_id"] == "wi-full");
    REQUIRE(j["task_id"] == "task-full");
    REQUIRE(j["node_id"] == "node-1");
    REQUIRE(j["task_version"] == 5);
    REQUIRE(j["task_name"] == "full-task-name");
    REQUIRE(j["status"] == "FAILED");
    REQUIRE(j["worker_id"] == "worker-001");
    REQUIRE(j["retry_count"] == 3);
    REQUIRE(j["started_at"] == "2025-06-20T10:00:00Z");
    REQUIRE(j["finished_at"] == "2025-06-20T10:05:30Z");
    REQUIRE(j["exit_code"] == 1);
    REQUIRE(j["error_message"] == "Task failed due to timeout");
    REQUIRE(j["created_at"] == "2025-06-20T09:59:00Z");

    // 确保恰好 14 个字段
    REQUIRE(j.size() == 14);
}

TEST_CASE("TaskInstance: toJson with empty time fields", "[model_task_instance_full]") {
    // Fix #256: 验证 started_at/finished_at/error_message/created_at 默认空值正确输出
    TaskInstance ti;
    ti.id = "ti-empty-times";
    ti.status = "PENDING";

    auto j = ti.toJson();
    REQUIRE(j["started_at"] == "");
    REQUIRE(j["finished_at"] == "");
    REQUIRE(j["error_message"] == "");
    REQUIRE(j["created_at"] == "");
    REQUIRE(j["worker_id"] == "");
    REQUIRE(j["node_id"] == "");
    REQUIRE(j["exit_code"] == 0);
    REQUIRE(j["retry_count"] == 0);
    REQUIRE(j["task_version"] == 0);
}
