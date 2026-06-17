#!/bin/bash
# TaskFlow 端到端自动化测试脚本
# 使用方式: ./tests/e2e_test.sh [BASE_URL]
# 默认 BASE_URL=http://localhost:8080

set -euo pipefail

BASE_URL="${1:-http://localhost:8080}"
PASS=0
FAIL=0
TOTAL=0

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); TOTAL=$((TOTAL+1)); }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1)); }
log_info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

# 辅助函数：HTTP 请求
api_get() {
    curl -s -w "\n%{http_code}" -H "Authorization: Bearer $TOKEN" "$BASE_URL$1"
}

api_post() {
    curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" -H "Authorization: Bearer $TOKEN" "$BASE_URL$1" -d "$2"
}

api_put() {
    curl -s -w "\n%{http_code}" -X PUT -H "Content-Type: application/json" -H "Authorization: Bearer $TOKEN" "$BASE_URL$1" -d "$2"
}

api_delete() {
    curl -s -w "\n%{http_code}" -X DELETE -H "Authorization: Bearer $TOKEN" "$BASE_URL$1"
}

# 提取响应体和状态码
split_response() {
    local resp="$1"
    BODY=$(echo "$resp" | sed '$d')
    STATUS=$(echo "$resp" | tail -1)
}

assert_status() {
    local expected="$1"
    local actual="$2"
    local msg="$3"
    if [ "$actual" = "$expected" ]; then
        log_pass "$msg (status=$actual)"
    else
        log_fail "$msg (expected=$expected, actual=$actual)"
    fi
}

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local msg="$3"
    if echo "$haystack" | grep -q "$needle"; then
        log_pass "$msg"
    else
        log_fail "$msg (expected to contain '$needle')"
    fi
}

# ==================== 测试开始 ====================

echo "========================================"
echo "TaskFlow 端到端测试"
echo "BASE_URL: $BASE_URL"
echo "========================================"

# ---------- 1. 健康检查 ----------
log_info "=== 1. 健康检查 ==="
resp=$(curl -s -w "\n%{http_code}" "$BASE_URL/api/v1/health")
split_response "$resp"
assert_status "200" "$STATUS" "Health check"

# ---------- 2. 用户注册 ----------
log_info "=== 2. 用户注册 ==="
resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" \
    "$BASE_URL/api/v1/auth/register" \
    -d '{"username":"e2e_test_user","password":"test123456"}')
split_response "$resp"
assert_status "200" "$STATUS" "Register user"

# ---------- 3. 用户登录 ----------
log_info "=== 3. 用户登录 ==="
resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" \
    "$BASE_URL/api/v1/auth/login" \
    -d '{"username":"e2e_test_user","password":"test123456"}')
split_response "$resp"
assert_status "200" "$STATUS" "Login"
TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['access_token'])" 2>/dev/null || echo "")
if [ -n "$TOKEN" ]; then
    log_pass "Got access token"
else
    log_fail "Failed to get access token"
fi

# ---------- 4. 创建 Command 任务 ----------
log_info "=== 4. 创建 Command 任务 ==="
resp=$(api_post "/api/v1/tasks" '{
    "name":"e2e_command_task",
    "type":"command",
    "config":{"command":"echo Hello TaskFlow"},
    "description":"E2E test command task",
    "timeout":60,
    "max_retries":0,
    "retry_interval":60
}')
split_response "$resp"
assert_status "200" "$STATUS" "Create command task"
CMD_TASK_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null || echo "")

# ---------- 5. 创建 Script 任务 ----------
log_info "=== 5. 创建 Script 任务 ==="
resp=$(api_post "/api/v1/tasks" '{
    "name":"e2e_script_task",
    "type":"script",
    "config":{"script_content":"#!/bin/bash\necho Script executed at $(date)"},
    "description":"E2E test script task",
    "timeout":60
}')
split_response "$resp"
assert_status "200" "$STATUS" "Create script task"
SCRIPT_TASK_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null || echo "")

# ---------- 6. 创建 SQL 任务 ----------
log_info "=== 6. 创建 SQL 任务 ==="
resp=$(api_post "/api/v1/tasks" '{
    "name":"e2e_sql_task",
    "type":"sql",
    "config":{"db_host":"localhost","db_port":5432,"db_name":"taskflow","db_user":"taskflow","db_password":"taskflow123","sql_statement":"SELECT 1"},
    "description":"E2E test SQL task",
    "timeout":60
}')
split_response "$resp"
assert_status "200" "$STATUS" "Create SQL task"
SQL_TASK_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null || echo "")

# ---------- 7. 查看任务列表 ----------
log_info "=== 7. 查看任务列表 ==="
resp=$(api_get "/api/v1/tasks?page=1&page_size=10")
split_response "$resp"
assert_status "200" "$STATUS" "List tasks"
assert_contains "$BODY" "e2e_command_task" "Task list contains command task"

# ---------- 8. 查看任务详情 ----------
log_info "=== 8. 查看任务详情 ==="
if [ -n "$CMD_TASK_ID" ]; then
    resp=$(api_get "/api/v1/tasks/$CMD_TASK_ID")
    split_response "$resp"
    assert_status "200" "$STATUS" "Get task detail"
    assert_contains "$BODY" "e2e_command_task" "Task detail contains name"
fi

# ---------- 9. 更新任务（版本自增） ----------
log_info "=== 9. 更新任务（版本自增） ==="
if [ -n "$CMD_TASK_ID" ]; then
    resp=$(api_put "/api/v1/tasks/$CMD_TASK_ID" '{
        "name":"e2e_command_task",
        "type":"command",
        "config":{"command":"echo Hello TaskFlow v2"},
        "description":"Updated E2E test command task",
        "timeout":120
    }')
    split_response "$resp"
    assert_status "200" "$STATUS" "Update task"
    VERSION=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['version'])" 2>/dev/null || echo "0")
    if [ "$VERSION" = "2" ]; then
        log_pass "Task version incremented to 2"
    else
        log_fail "Task version not incremented (got $VERSION)"
    fi
fi

# ---------- 10. 创建工作流（线性 DAG: A→B→C） ----------
log_info "=== 10. 创建工作流 ==="
if [ -n "$CMD_TASK_ID" ] && [ -n "$SCRIPT_TASK_ID" ]; then
    resp=$(api_post "/api/v1/workflows" "{
        \"name\":\"e2e_linear_workflow\",
        \"description\":\"E2E test linear workflow A->B->C\",
        \"dag\":{
            \"nodes\":[
                {\"id\":\"node_1\",\"task_id\":\"$CMD_TASK_ID\",\"task_name\":\"e2e_command_task\",\"task_type\":\"command\"},
                {\"id\":\"node_2\",\"task_id\":\"$SCRIPT_TASK_ID\",\"task_name\":\"e2e_script_task\",\"task_type\":\"script\"},
                {\"id\":\"node_3\",\"task_id\":\"$CMD_TASK_ID\",\"task_name\":\"e2e_command_task\",\"task_type\":\"command\"}
            ],
            \"edges\":[
                {\"source\":\"node_1\",\"target\":\"node_2\"},
                {\"source\":\"node_2\",\"target\":\"node_3\"}
            ]
        },
        \"schedule_strategy\":\"random\"
    }")
    split_response "$resp"
    assert_status "200" "$STATUS" "Create workflow"
    WORKFLOW_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null || echo "")
fi

# ---------- 11. 查看工作流详情 ----------
log_info "=== 11. 查看工作流详情 ==="
if [ -n "$WORKFLOW_ID" ]; then
    resp=$(api_get "/api/v1/workflows/$WORKFLOW_ID")
    split_response "$resp"
    assert_status "200" "$STATUS" "Get workflow detail"
    assert_contains "$BODY" "e2e_linear_workflow" "Workflow detail contains name"
fi

# ---------- 12. 手动触发工作流 ----------
log_info "=== 12. 手动触发工作流 ==="
if [ -n "$WORKFLOW_ID" ]; then
    resp=$(api_post "/api/v1/workflows/$WORKFLOW_ID/trigger" '{}')
    split_response "$resp"
    assert_status "200" "$STATUS" "Trigger workflow"
    INSTANCE_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null || echo "")
fi

# ---------- 13. 查看执行实例详情 ----------
log_info "=== 13. 查看执行实例详情 ==="
if [ -n "$INSTANCE_ID" ]; then
    resp=$(api_get "/api/v1/instances/$INSTANCE_ID")
    split_response "$resp"
    assert_status "200" "$STATUS" "Get instance detail"
fi

# ---------- 14. 暂停工作流 ----------
log_info "=== 14. 暂停工作流 ==="
if [ -n "$INSTANCE_ID" ]; then
    resp=$(api_post "/api/v1/instances/$INSTANCE_ID/pause" '{}')
    split_response "$resp"
    assert_status "200" "$STATUS" "Pause instance"
fi

# ---------- 15. 恢复工作流 ----------
log_info "=== 15. 恢复工作流 ==="
if [ -n "$INSTANCE_ID" ]; then
    resp=$(api_post "/api/v1/instances/$INSTANCE_ID/resume" '{}')
    split_response "$resp"
    assert_status "200" "$STATUS" "Resume instance"
fi

# ---------- 16. 取消工作流 ----------
log_info "=== 16. 取消工作流 ==="
if [ -n "$INSTANCE_ID" ]; then
    resp=$(api_post "/api/v1/instances/$INSTANCE_ID/cancel" '{}')
    split_response "$resp"
    assert_status "200" "$STATUS" "Cancel instance"
fi

# ---------- 17. 创建并行 DAG 工作流 ----------
log_info "=== 17. 创建并行 DAG 工作流 ==="
if [ -n "$CMD_TASK_ID" ] && [ -n "$SCRIPT_TASK_ID" ]; then
    resp=$(api_post "/api/v1/workflows" "{
        \"name\":\"e2e_parallel_workflow\",
        \"description\":\"E2E test parallel workflow\",
        \"dag\":{
            \"nodes\":[
                {\"id\":\"node_a\",\"task_id\":\"$CMD_TASK_ID\",\"task_name\":\"e2e_command_task\",\"task_type\":\"command\"},
                {\"id\":\"node_b1\",\"task_id\":\"$SCRIPT_TASK_ID\",\"task_name\":\"e2e_script_task\",\"task_type\":\"script\"},
                {\"id\":\"node_b2\",\"task_id\":\"$CMD_TASK_ID\",\"task_name\":\"e2e_command_task\",\"task_type\":\"command\"},
                {\"id\":\"node_c\",\"task_id\":\"$CMD_TASK_ID\",\"task_name\":\"e2e_command_task\",\"task_type\":\"command\"}
            ],
            \"edges\":[
                {\"source\":\"node_a\",\"target\":\"node_b1\"},
                {\"source\":\"node_a\",\"target\":\"node_b2\"},
                {\"source\":\"node_b1\",\"target\":\"node_c\"},
                {\"source\":\"node_b2\",\"target\":\"node_c\"}
            ]
        },
        \"schedule_strategy\":\"load_balance\"
    }")
    split_response "$resp"
    assert_status "200" "$STATUS" "Create parallel workflow"
fi

# ---------- 18. 创建带 Cron 的工作流 ----------
log_info "=== 18. 创建带 Cron 的工作流 ==="
if [ -n "$CMD_TASK_ID" ]; then
    resp=$(api_post "/api/v1/workflows" "{
        \"name\":\"e2e_cron_workflow\",
        \"description\":\"E2E test cron workflow\",
        \"dag\":{
            \"nodes\":[
                {\"id\":\"node_1\",\"task_id\":\"$CMD_TASK_ID\",\"task_name\":\"e2e_command_task\",\"task_type\":\"command\"}
            ],
            \"edges\":[]
        },
        \"schedule_strategy\":\"random\",
        \"cron_enabled\":true,
        \"cron_expression\":\"0 0 8 * * *\"
    }")
    split_response "$resp"
    assert_status "200" "$STATUS" "Create cron workflow"
fi

# ---------- 19. 查看工作流列表 ----------
log_info "=== 19. 查看工作流列表 ==="
resp=$(api_get "/api/v1/workflows?page=1&page_size=10")
split_response "$resp"
assert_status "200" "$STATUS" "List workflows"

# ---------- 20. 查看执行历史 ----------
log_info "=== 20. 查看执行历史 ==="
if [ -n "$WORKFLOW_ID" ]; then
    resp=$(api_get "/api/v1/workflows/$WORKFLOW_ID/instances?page=1&page_size=10")
    split_response "$resp"
    assert_status "200" "$STATUS" "List workflow instances"
fi

# ---------- 21. 查看 Worker 列表 ----------
log_info "=== 21. 查看 Worker 列表 ==="
resp=$(api_get "/api/v1/workers")
split_response "$resp"
assert_status "200" "$STATUS" "List workers"

# ---------- 22. 查看用户列表 ----------
log_info "=== 22. 查看用户列表 ==="
resp=$(api_get "/api/v1/users")
split_response "$resp"
assert_status "200" "$STATUS" "List users"

# ---------- 23. Token 刷新 ----------
log_info "=== 23. Token 刷新 ==="
resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" \
    "$BASE_URL/api/v1/auth/refresh" \
    -d "{\"refresh_token\":\"$(curl -s -X POST -H 'Content-Type: application/json' $BASE_URL/api/v1/auth/login -d '{\"username\":\"e2e_test_user\",\"password\":\"test123456\"}' | python3 -c 'import sys,json; print(json.load(sys.stdin)[\"data\"][\"refresh_token\"])' 2>/dev/null)\"}")
split_response "$resp"
assert_status "200" "$STATUS" "Refresh token"

# ---------- 24. 用户登出 ----------
log_info "=== 24. 用户登出 ==="
resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    "$BASE_URL/api/v1/auth/logout" \
    -d "{\"access_token\":\"$TOKEN\"}")
split_response "$resp"
assert_status "200" "$STATUS" "Logout"

# ---------- 25. 无效 Token 被拒绝 ----------
log_info "=== 25. 无效 Token 被拒绝 ==="
resp=$(curl -s -w "\n%{http_code}" -H "Authorization: Bearer invalid_token" \
    "$BASE_URL/api/v1/tasks?page=1&page_size=10")
split_response "$resp"
if [ "$STATUS" = "401" ]; then
    log_pass "Invalid token rejected (status=401)"
else
    log_fail "Invalid token not rejected (status=$STATUS)"
fi

# ---------- 26. 清理：删除测试数据 ----------
log_info "=== 26. 清理测试数据 ==="
# 重新登录获取新 token
resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" \
    "$BASE_URL/api/v1/auth/login" \
    -d '{"username":"e2e_test_user","password":"test123456"}')
split_response "$resp"
TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['access_token'])" 2>/dev/null || echo "")

if [ -n "$CMD_TASK_ID" ]; then
    resp=$(api_delete "/api/v1/tasks/$CMD_TASK_ID")
    split_response "$resp"
    assert_status "200" "$STATUS" "Delete command task"
fi
if [ -n "$SCRIPT_TASK_ID" ]; then
    resp=$(api_delete "/api/v1/tasks/$SCRIPT_TASK_ID")
    split_response "$resp"
    assert_status "200" "$STATUS" "Delete script task"
fi
if [ -n "$SQL_TASK_ID" ]; then
    resp=$(api_delete "/api/v1/tasks/$SQL_TASK_ID")
    split_response "$resp"
    assert_status "200" "$STATUS" "Delete SQL task"
fi

# ==================== 测试结果 ====================
echo ""
echo "========================================"
echo "测试结果汇总"
echo "========================================"
echo -e "通过: ${GREEN}$PASS${NC}"
echo -e "失败: ${RED}$FAIL${NC}"
echo -e "总计: $TOTAL"
echo "========================================"

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}全部测试通过！${NC}"
    exit 0
else
    echo -e "${RED}存在失败测试！${NC}"
    exit 1
fi
