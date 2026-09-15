#!/bin/bash
# TaskFlow 端到端自动化测试脚本
# 使用方式: ./tests/e2e_test.sh [BASE_URL]
# 默认 BASE_URL=http://localhost:8080

set -euo pipefail

BASE_URL="${1:-http://localhost:8080}"
PASS=0
FAIL=0
TOTAL=0

# 测试账号：用户名是全局唯一约束且软删除不释放，固定名字会让脚本只能跑一次，
# 因此每次运行带时间戳后缀。
E2E_USER="e2e_test_user_$(date +%s)"

# 变量预初始化：脚本开了 set -u，失败分支下这些变量可能尚未赋值，
# 直接引用会以 "unbound variable" 中断整个脚本。
WORKFLOW_ID=""
INSTANCE_ID=""
CMD_TASK_ID=""
SCRIPT_TASK_ID=""
SQL_TASK_ID=""

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

# 删除工作流：先取消并删除它的实例，再删工作流。
# Fix #308 之后「有实例的工作流」会被拒绝删除（400），所以清理必须先处理实例。
delete_workflow_with_instances() {
    local wid="$1" token="$2"
    local inst_ids
    inst_ids=$(curl -s -H "Authorization: Bearer $token" \
        "$BASE_URL/api/v1/workflows/$wid/instances?page=1&page_size=100" \
        | python3 -c "import sys,json; d=json.load(sys.stdin); items=(d.get('data') or {}).get('items') or []; print(' '.join(i['id'] for i in items))" 2>/dev/null || echo "")
    for iid in $inst_ids; do
        curl -s -o /dev/null -X POST -H "Authorization: Bearer $token" \
            "$BASE_URL/api/v1/instances/$iid/cancel" || true
        curl -s -o /dev/null -X DELETE -H "Authorization: Bearer $token" \
            "$BASE_URL/api/v1/instances/$iid" || true
    done
    # 返回最终的状态码，便于调用方断言
    curl -s -o /dev/null -w '%{http_code}' -X DELETE -H "Authorization: Bearer $token" \
        "$BASE_URL/api/v1/workflows/$wid" || echo "000"
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
# ---------- 1.5 预清理：删除上次运行遗留的同名数据 ----------
# 脚本用固定的测试名（e2e_* 任务与工作流；用户见 E2E_USER 注释），重复运行时若不清
# 就会在"注册/创建"处得到 400，后续断言被 if 静默跳过。这里先用 admin 清场。
log_info "=== 1.5 预清理（同名遗留数据）==="
ADMIN_USER="${ADMIN_USER:-admin}"
ADMIN_PASSWORD="${ADMIN_PASSWORD:-admin123}"
ADMIN_TOKEN=$(curl -s -X POST -H 'Content-Type: application/json' \
    "$BASE_URL/api/v1/auth/login" \
    -d "{\"username\":\"$ADMIN_USER\",\"password\":\"$ADMIN_PASSWORD\"}" \
    | python3 -c 'import sys,json; print(json.load(sys.stdin)["data"]["access_token"])' 2>/dev/null || echo "")

if [ -n "$ADMIN_TOKEN" ]; then
    # 工作流要先删实例（有实例的工作流会被拒绝删除），实例是运行中的先取消
    WF_IDS=$(curl -s -H "Authorization: Bearer $ADMIN_TOKEN" \
        "$BASE_URL/api/v1/workflows?page=1&page_size=100" \
        | python3 -c "import sys,json; d=json.load(sys.stdin); items=(d.get('data') or {}).get('items') or []; print(' '.join(i['id'] for i in items if i.get('name','').startswith('e2e_')))" 2>/dev/null || echo "")
    for wid in $WF_IDS; do
        delete_workflow_with_instances "$wid" "$ADMIN_TOKEN" >/dev/null
    done

    TASK_IDS=$(curl -s -H "Authorization: Bearer $ADMIN_TOKEN" \
        "$BASE_URL/api/v1/tasks?page=1&page_size=100" \
        | python3 -c "import sys,json; d=json.load(sys.stdin); items=(d.get('data') or {}).get('items') or []; print(' '.join(i['id'] for i in items if i.get('name','').startswith('e2e_')))" 2>/dev/null || echo "")
    for tid in $TASK_IDS; do
        curl -s -o /dev/null -X DELETE -H "Authorization: Bearer $ADMIN_TOKEN" \
            "$BASE_URL/api/v1/tasks/$tid" || true
    done

    # 注意：用户是软删除，且 username 是全局唯一约束（删了也不释放名字），
    # 所以这里不删用户——重复运行时改为复用已有账号（见下一步的注册分支）。
    log_info "预清理完成（工作流: ${WF_IDS:-无}；任务: ${TASK_IDS:-无}）"
else
    log_fail "预清理失败：无法用 $ADMIN_USER 登录"
fi

log_info "=== 2. 用户注册 ==="
resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" \
    "$BASE_URL/api/v1/auth/register" \
    -d "{\"username\":\"$E2E_USER\",\"password\":\"test123456\"}")
split_response "$resp"
if [ "$STATUS" = "200" ]; then
    log_pass "Register user (status=200)"
elif [ "$STATUS" = "400" ] && echo "$BODY" | grep -q "already exists"; then
    # 用户名软删除后不释放，重复运行时直接复用已有账号登录
    log_info "测试账号已存在，复用已有账号"
else
    log_fail "Register user (expected=200, actual=$STATUS)"
fi

# ---------- 3. 用户登录 ----------
log_info "=== 3. 用户登录 ==="
resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" \
    "$BASE_URL/api/v1/auth/login" \
    -d "{\"username\":\"$E2E_USER\",\"password\":\"test123456\"}")
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
    # 可重复运行：先清掉上次异常退出留下的同名工作流（名称唯一约束会导致 400）
    EXISTING_WF=$(curl -s -H "Authorization: Bearer $TOKEN" \
        "$BASE_URL/api/v1/workflows?keyword=e2e_linear_workflow&page=1&page_size=50" \
        | python3 -c "import sys,json; d=json.load(sys.stdin); items=(d.get('data') or {}).get('items') or []; print(' '.join(i['id'] for i in items if i.get('name')=='e2e_linear_workflow'))" 2>/dev/null || echo "")
    for wid in $EXISTING_WF; do
        curl -s -o /dev/null -X DELETE -H "Authorization: Bearer $TOKEN" \
            "$BASE_URL/api/v1/workflows/$wid" || true
    done
    [ -n "$EXISTING_WF" ] && log_info "已清理同名遗留工作流: $EXISTING_WF"

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
    # Fix: 触发接口返回的是 data.instance_id（此前取 data.id 恒为空，
    # 导致下面所有实例相关断言都被 if 静默跳过）
    INSTANCE_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['instance_id'])" 2>/dev/null || echo "")
    if [ -n "$INSTANCE_ID" ]; then
        log_pass "Got instance id"
    else
        log_fail "Trigger did not return instance_id"
    fi
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
log_info "=== 22. 查看用户列表（非 admin 应被拒，admin 可访问）==="
resp=$(api_get "/api/v1/users")
split_response "$resp"
# Fix: 用户管理接口仅 admin 可访问（RoleFilter fail-closed）。脚本此前用注册
# 出来的普通账号断言 200，与权限设计相反；改为断言 403，并用 admin 复核 200。
assert_status "403" "$STATUS" "List users as non-admin is forbidden"

ADMIN_USER="${ADMIN_USER:-admin}"
ADMIN_PASSWORD="${ADMIN_PASSWORD:-admin123}"
ADMIN_TOKEN=$(curl -s -X POST -H 'Content-Type: application/json' \
    "$BASE_URL/api/v1/auth/login" \
    -d "{\"username\":\"$ADMIN_USER\",\"password\":\"$ADMIN_PASSWORD\"}" \
    | python3 -c 'import sys,json; print(json.load(sys.stdin)["data"]["access_token"])' 2>/dev/null || echo "")
ADMIN_STATUS=$(curl -s -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $ADMIN_TOKEN" "$BASE_URL/api/v1/users")
assert_status "200" "$ADMIN_STATUS" "List users as admin"

# ---------- 23. Token 刷新 ----------
log_info "=== 23. Token 刷新 ==="
resp=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" \
    "$BASE_URL/api/v1/auth/refresh" \
    -d "{\"refresh_token\":\"$(curl -s -X POST -H 'Content-Type: application/json' $BASE_URL/api/v1/auth/login -d "{\"username\":\"$E2E_USER\",\"password\":\"test123456\"}" | python3 -c 'import sys,json; print(json.load(sys.stdin)["data"]["refresh_token"])' 2>/dev/null)\"}")
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
    -d "{\"username\":\"$E2E_USER\",\"password\":\"test123456\"}")
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
if [ -n "$WORKFLOW_ID" ]; then
    # 工作流也要删掉，否则下次运行会因同名冲突在创建处失败。
    # 先删它下面的实例——Fix #308 之后有实例的工作流会被拒绝删除。
    WF_DELETE_STATUS=$(delete_workflow_with_instances "$WORKFLOW_ID" "$TOKEN")
    assert_status "200" "$WF_DELETE_STATUS" "Delete workflow"
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
