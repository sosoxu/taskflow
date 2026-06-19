#!/bin/bash
# TaskFlow 集成测试脚本
# 按照 completed-features.md 逐项测试

# set -e  # Disabled to continue on failures

BASE_URL="http://localhost:8080/api/v1"
PASS=0
FAIL=0
ISSUES=()

# Fix #246: 测试数据清理。原脚本创建测试用户但不清理，重复运行时因
# 用户名重复（"already exists"）导致注册相关断言失败。
# cleanup 函数在脚本退出时（正常或异常）删除所有测试创建的用户。
CLEANUP_USERS="user1 viewer1 testadmin2 operatorb publicuser todelete"
cleanup() {
    local exit_code=$?
    echo -e "\n${YELLOW}========================================${NC}"
    echo -e "${YELLOW}清理测试数据${NC}"
    echo -e "${YELLOW}========================================${NC}"
    for username in $CLEANUP_USERS; do
        PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow \
            -c "DELETE FROM users WHERE username='$username';" >/dev/null 2>&1
    done
    # 清理测试创建的任务和工作流（按名称匹配）
    PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow \
        -c "DELETE FROM tasks WHERE name IN ('cmd-task-1','script-task-1','sql-task-1','crud-task','crud-task-updated','admin-task','operator-a-task','viewer-to-op-task','bad-task');" >/dev/null 2>&1
    PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow \
        -c "DELETE FROM workflows WHERE name IN ('test-workflow-1','bad-workflow','trigger-workflow');" >/dev/null 2>&1
    echo -e "${GREEN}清理完成${NC}"
    exit $exit_code
}
trap cleanup EXIT

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

assert_eq() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$expected" = "$actual" ]; then
        echo -e "  ${GREEN}PASS${NC}: $desc"
        PASS=$((PASS+1))
    else
        echo -e "  ${RED}FAIL${NC}: $desc (expected=$expected, actual=$actual)"
        FAIL=$((FAIL+1))
        ISSUES+=("$desc: expected=$expected, actual=$actual")
    fi
}

assert_contains() {
    local desc="$1" haystack="$2" needle="$3"
    if echo "$haystack" | grep -q "$needle"; then
        echo -e "  ${GREEN}PASS${NC}: $desc"
        PASS=$((PASS+1))
    else
        echo -e "  ${RED}FAIL${NC}: $desc (expected to contain '$needle')"
        FAIL=$((FAIL+1))
        ISSUES+=("$desc: expected to contain '$needle'")
    fi
}

assert_not_contains() {
    local desc="$1" haystack="$2" needle="$3"
    if ! echo "$haystack" | grep -q "$needle"; then
        echo -e "  ${GREEN}PASS${NC}: $desc"
        PASS=$((PASS+1))
    else
        echo -e "  ${RED}FAIL${NC}: $desc (should not contain '$needle')"
        FAIL=$((FAIL+1))
        ISSUES+=("$desc: should not contain '$needle'")
    fi
}

assert_http_status() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$expected" = "$actual" ]; then
        echo -e "  ${GREEN}PASS${NC}: $desc (HTTP $actual)"
        PASS=$((PASS+1))
    else
        echo -e "  ${RED}FAIL${NC}: $desc (expected HTTP $expected, got $actual)"
        FAIL=$((FAIL+1))
        ISSUES+=("$desc: expected HTTP $expected, got $actual")
    fi
}

# Helper: make API call and capture HTTP status + body
api_call() {
    local method="$1" endpoint="$2" data="$3" token="$4"
    local url="${BASE_URL}${endpoint}"
    local args=(-s -w "\n%{http_code}")
    args+=(-X "$method")
    args+=(-H "Content-Type: application/json")
    if [ -n "$token" ]; then
        args+=(-H "Authorization: Bearer $token")
    fi
    if [ -n "$data" ]; then
        args+=(-d "$data")
    fi
    curl "${args[@]}" "$url" 2>/dev/null
}

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.1 用户注册${NC}"
echo -e "${YELLOW}========================================${NC}"

# 正常注册 (returns 201)
RESP=$(api_call POST /auth/register '{"username":"user1","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "正常注册返回201" "201" "$STATUS"
assert_contains "注册返回access_token" "$BODY" "access_token"
assert_contains "注册返回user信息" "$BODY" "username"
USER1_TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])" 2>/dev/null)
USER1_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['user']['id'])" 2>/dev/null)

# 重复用户名
RESP=$(api_call POST /auth/register '{"username":"user1","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "重复用户名返回400" "400" "$STATUS"
assert_contains "重复用户名错误信息" "$BODY" "already exists"

# 密码太短
RESP=$(api_call POST /auth/register '{"username":"user2","password":"short"}')
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "密码<8字符返回400" "400" "$STATUS"
assert_contains "密码太短错误信息" "$BODY" "8"

# 数据库中 password_hash 以 $2b$10$ 开头
HASH=$(PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow -t -c "SELECT password_hash FROM users WHERE username='user1';" 2>/dev/null | xargs)
assert_contains "password_hash以\$2b\$10\$开头" "$HASH" '$2b$10$'

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.2 用户登录${NC}"
echo -e "${YELLOW}========================================${NC}"

# 正确凭据
RESP=$(api_call POST /auth/login '{"username":"user1","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "正确凭据返回200" "200" "$STATUS"
assert_contains "登录返回access_token" "$BODY" "access_token"
assert_contains "登录返回refresh_token" "$BODY" "refresh_token"
assert_contains "登录返回expires_in" "$BODY" "expires_in"
LOGIN_TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])" 2>/dev/null)
REFRESH_TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['refresh_token'])" 2>/dev/null)

# 错误密码
RESP=$(api_call POST /auth/login '{"username":"user1","password":"wrongpassword"}')
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "错误密码返回401" "401" "$STATUS"

# 不存在用户
RESP=$(api_call POST /auth/login '{"username":"nonexistent","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "不存在用户返回401" "401" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.3 Token 刷新${NC}"
echo -e "${YELLOW}========================================${NC}"

# 有效 refreshToken
RESP=$(api_call POST /auth/refresh "{\"refresh_token\":\"$REFRESH_TOKEN\"}")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "有效refreshToken返回200" "200" "$STATUS"
assert_contains "刷新返回新access_token" "$BODY" "access_token"

# type 不为 refresh 的 token（用 accessToken 尝试刷新）
RESP=$(api_call POST /auth/refresh "{\"refresh_token\":\"$LOGIN_TOKEN\"}")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "accessToken刷新返回401" "401" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.4 用户登出${NC}"
echo -e "${YELLOW}========================================${NC}"

# 先登录获取新 token
RESP=$(api_call POST /auth/login '{"username":"user1","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
LOGOUT_TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])" 2>/dev/null)

# 登出 - 需要在 body 中传递 access_token
RESP=$(api_call POST /auth/logout "{\"access_token\":\"$LOGOUT_TOKEN\"}" "$LOGOUT_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "登出返回200" "200" "$STATUS"

# 登出后 token 失效
RESP=$(api_call GET /tasks '' "$LOGOUT_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "登出后token返回401" "401" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.11 API 接口权限校验${NC}"
echo -e "${YELLOW}========================================${NC}"

# 无 token 访问受保护接口
RESP=$(api_call GET /tasks)
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "无token返回401" "401" "$STATUS"

# 免认证路径
RESP=$(api_call POST /auth/register '{"username":"publicuser","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "注册免认证" "201" "$STATUS"

# 有效 token 正常放行
RESP=$(api_call GET /tasks '' "$USER1_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "有效token正常放行" "200" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.5 RBAC 角色控制${NC}"
echo -e "${YELLOW}========================================${NC}"

# 创建 viewer 用户
RESP=$(api_call POST /auth/register '{"username":"viewer1","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
VIEWER_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['user']['id'])" 2>/dev/null)
# 设置为 viewer
PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow -c "UPDATE users SET role='viewer' WHERE username='viewer1';" >/dev/null 2>&1
# 重新登录获取 viewer 角色 token
RESP=$(api_call POST /auth/login '{"username":"viewer1","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
VIEWER_TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])" 2>/dev/null)

# viewer GET 请求
RESP=$(api_call GET /tasks '' "$VIEWER_TOKEN")
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "viewer可GET" "200" "$STATUS"

# viewer POST 请求
RESP=$(api_call POST /tasks '{"name":"test","type":"command","config_json":{"command":"echo hello"}}' "$VIEWER_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "viewer POST返回403" "403" "$STATUS"

# 创建 admin 用户用于后续测试
RESP=$(api_call POST /auth/register '{"username":"testadmin2","password":"admin123456"}')
BODY=$(echo "$RESP" | head -n -1)
ADMIN_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['user']['id'])" 2>/dev/null)
# 设置为 admin
PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow -c "UPDATE users SET role='admin' WHERE username='testadmin2';" >/dev/null 2>&1
# 重新登录获取 admin token
RESP=$(api_call POST /auth/login '{"username":"testadmin2","password":"admin123456"}')
BODY=$(echo "$RESP" | head -n -1)
ADMIN_TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])" 2>/dev/null)

RESP=$(api_call GET /tasks '' "$ADMIN_TOKEN")
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "admin可GET" "200" "$STATUS"

RESP=$(api_call POST /tasks '{"name":"admin-task","type":"command","config_json":{"command":"echo hello"}}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "admin可POST" "200" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.7 用户管理 API${NC}"
echo -e "${YELLOW}========================================${NC}"

# admin 查看用户列表
RESP=$(api_call GET /users '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "admin查看用户列表" "200" "$STATUS"
assert_contains "用户列表包含数据" "$BODY" "username"

# operator 访问用户管理接口
RESP=$(api_call GET /users '' "$USER1_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "operator访问用户管理返回403" "403" "$STATUS"

# admin 修改角色
RESP=$(api_call PUT /users/$VIEWER_ID/role '{"role":"operator"}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "admin修改角色" "200" "$STATUS"

# 验证角色修改生效 - viewer 变为 operator 后可以 POST
RESP=$(api_call POST /auth/login '{"username":"viewer1","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
NEW_VIEWER_TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])" 2>/dev/null)
RESP=$(api_call POST /tasks '{"name":"viewer-to-op-task","type":"command","config_json":{"command":"echo test"}}' "$NEW_VIEWER_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "角色修改后可POST(原viewer)" "200" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§3.1 三种任务类型${NC}"
echo -e "${YELLOW}========================================${NC}"

# 创建 Command 任务
RESP=$(api_call POST /tasks '{"name":"cmd-task-1","type":"command","config_json":{"command":"echo hello"},"description":"test command task"}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "创建Command任务" "200" "$STATUS"
assert_contains "Command任务类型" "$BODY" '"type":"command"'
CMD_TASK_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)

# 创建 Script 任务
RESP=$(api_call POST /tasks '{"name":"script-task-1","type":"script","config_json":{"script_content":"#!/bin/bash\necho hello from script"},"description":"test script task"}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "创建Script任务" "200" "$STATUS"
assert_contains "Script任务类型" "$BODY" '"type":"script"'

# 创建 SQL 任务
RESP=$(api_call POST /tasks '{"name":"sql-task-1","type":"sql","config_json":{"db_host":"localhost","db_port":5432,"db_name":"taskflow","db_user":"taskflow","db_password":"taskflow123","sql_statement":"SELECT 1"},"description":"test sql task"}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "创建SQL任务" "200" "$STATUS"
assert_contains "SQL任务类型" "$BODY" '"type":"sql"'
SQL_TASK_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)

# 无效类型
RESP=$(api_call POST /tasks '{"name":"bad-task","type":"invalid","config_json":{}}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "无效类型返回400" "400" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.10 SQL 密码 AES-256 加密${NC}"
echo -e "${YELLOW}========================================${NC}"

# 数据库中 SQL 任务的 db_password 为密文
DB_PASS=$(PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow -t -c "SELECT config_json->>'db_password' FROM tasks WHERE id='$SQL_TASK_ID';" 2>/dev/null | xargs)
assert_not_contains "数据库中db_password非明文" "$DB_PASS" "taskflow123"
assert_contains "数据库中db_password为密文" "$DB_PASS" "Encrypted:"

# API 返回 db_password 为 ***
RESP=$(api_call GET /tasks/$SQL_TASK_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "查看SQL任务详情" "200" "$STATUS"
assert_contains "API返回db_password脱敏" "$BODY" '***'
assert_not_contains "API不返回明文密码" "$BODY" "taskflow123"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§3.3 任务 CRUD 操作${NC}"
echo -e "${YELLOW}========================================${NC}"

# 创建任务
RESP=$(api_call POST /tasks '{"name":"crud-task","type":"command","config_json":{"command":"ls"},"description":"crud test","timeout":120,"max_retries":3}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "创建任务返回200" "200" "$STATUS"
assert_contains "创建返回任务信息" "$BODY" "crud-task"
CRUD_TASK_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)

# 列表查询
RESP=$(api_call GET '/tasks?page=1&page_size=10' '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "任务列表返回200" "200" "$STATUS"
assert_contains "列表包含total" "$BODY" "total"
assert_contains "列表包含items" "$BODY" "items"

# 按类型筛选
RESP=$(api_call GET '/tasks?page=1&page_size=10&type=command' '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "按类型筛选返回200" "200" "$STATUS"

# 关键词搜索
RESP=$(api_call GET '/tasks?page=1&page_size=10&keyword=crud' '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "关键词搜索返回200" "200" "$STATUS"
assert_contains "搜索结果包含crud" "$BODY" "crud"

# 查看详情
RESP=$(api_call GET /tasks/$CRUD_TASK_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "任务详情返回200" "200" "$STATUS"
assert_contains "详情包含任务名" "$BODY" "crud-task"

# 更新任务
RESP=$(api_call PUT /tasks/$CRUD_TASK_ID '{"name":"crud-task-updated","description":"updated desc"}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "更新任务返回200" "200" "$STATUS"

# 验证 version +1
RESP=$(api_call GET /tasks/$CRUD_TASK_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
VERSION=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['version'])" 2>/dev/null)
assert_eq "更新后version+1" "2" "$VERSION"

# 软删除
RESP=$(api_call DELETE /tasks/$CRUD_TASK_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "删除任务返回200" "200" "$STATUS"

# 软删除后列表不可见
RESP=$(api_call GET '/tasks?page=1&page_size=100&keyword=crud-task' '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
DELETED_VISIBLE=$(echo "$BODY" | python3 -c "
import sys,json
data = json.load(sys.stdin)
items = data.get('data',{}).get('items',[])
found = [i for i in items if i.get('name')=='crud-task-updated']
print('yes' if found else 'no')
" 2>/dev/null)
assert_eq "软删除后列表不可见" "no" "$DELETED_VISIBLE"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§2.6 资源级权限${NC}"
echo -e "${YELLOW}========================================${NC}"

# operator A 创建任务
RESP=$(api_call POST /tasks '{"name":"operator-a-task","type":"command","config_json":{"command":"echo a"}}' "$USER1_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
OPA_TASK_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)

# 创建 operator B
RESP=$(api_call POST /auth/register '{"username":"operatorb","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
OPB_TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])" 2>/dev/null)

# operator B 无法编辑 operator A 的任务
RESP=$(api_call PUT /tasks/$OPA_TASK_ID '{"name":"hacked"}' "$OPB_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "operatorB编辑operatorA任务返回403" "403" "$STATUS"

# admin 可编辑任意用户的任务
RESP=$(api_call PUT /tasks/$OPA_TASK_ID '{"description":"admin edit"}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "admin编辑任意任务" "200" "$STATUS"

# operator 列表只看到自己的任务
RESP=$(api_call GET '/tasks?page=1&page_size=100' '' "$USER1_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
OP_VISIBLE=$(echo "$BODY" | python3 -c "
import sys,json
data = json.load(sys.stdin)
items = data.get('data',{}).get('items',[])
non_owned = [i for i in items if i.get('creator_id') != '$USER1_ID']
print('only_own' if not non_owned else 'see_others')
" 2>/dev/null)
assert_eq "operator只看到自己的任务" "only_own" "$OP_VISIBLE"

# admin 列表看到所有任务
RESP=$(api_call GET '/tasks?page=1&page_size=100' '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
ADMIN_VISIBLE=$(echo "$BODY" | python3 -c "
import sys,json
data = json.load(sys.stdin)
items = data.get('data',{}).get('items',[])
creators = set(i.get('creator_id','') for i in items)
print('multiple' if len(creators) > 1 else 'single')
" 2>/dev/null)
assert_eq "admin看到所有任务" "multiple" "$ADMIN_VISIBLE"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§4.4 工作流 CRUD 操作${NC}"
echo -e "${YELLOW}========================================${NC}"

# 创建工作流
RESP=$(api_call POST /workflows "{\"name\":\"test-workflow-1\",\"description\":\"test workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$CMD_TASK_ID\",\"task_name\":\"cmd-task-1\",\"task_type\":\"command\"}],\"edges\":[]},\"schedule_strategy\":\"random\"}" "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "创建工作流返回200" "200" "$STATUS"
assert_contains "工作流包含dag_json" "$BODY" "nodes"
WF_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)

# DAG 校验失败 - 引用不存在的 task_id
RESP=$(api_call POST /workflows '{"name":"bad-workflow","dag_json":{"nodes":[{"id":"n1","task_id":"00000000-0000-0000-0000-000000000000","task_name":"bad","task_type":"command"}],"edges":[]},"schedule_strategy":"random"}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "DAG校验失败返回400" "400" "$STATUS"

# 查看工作流详情
RESP=$(api_call GET /workflows/$WF_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "工作流详情返回200" "200" "$STATUS"
assert_contains "详情含dag_json" "$BODY" "dag_json"

# 更新工作流
RESP=$(api_call PUT /workflows/$WF_ID '{"description":"updated workflow"}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "更新工作流返回200" "200" "$STATUS"

# 验证 version +1
RESP=$(api_call GET /workflows/$WF_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
WF_VERSION=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['version'])" 2>/dev/null)
assert_eq "工作流version+1" "2" "$WF_VERSION"

# 软删除
RESP=$(api_call DELETE /workflows/$WF_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "删除工作流返回200" "200" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§4.5 手动触发${NC}"
echo -e "${YELLOW}========================================${NC}"

# 创建一个可触发的工作流
RESP=$(api_call POST /workflows "{\"name\":\"trigger-workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$CMD_TASK_ID\",\"task_name\":\"cmd-task-1\",\"task_type\":\"command\"}],\"edges\":[]},\"schedule_strategy\":\"random\"}" "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
TRIGGER_WF_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)

# 手动触发
RESP=$(api_call POST /workflows/$TRIGGER_WF_ID/trigger '{}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "手动触发返回200" "200" "$STATUS"
assert_contains "触发返回instance_id" "$BODY" "instance_id"
INSTANCE_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['instance_id'])" 2>/dev/null)

# 验证 WorkflowInstance 存在
WF_INST_STATUS=$(PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow -t -c "SELECT status FROM workflow_instances WHERE id='$INSTANCE_ID';" 2>/dev/null | xargs)
assert_eq "WorkflowInstance状态为PENDING" "PENDING" "$WF_INST_STATUS"

# 验证 TaskInstance 存在
TASK_INST_COUNT=$(PGPASSWORD=taskflow123 psql -h localhost -U taskflow -d taskflow -t -c "SELECT COUNT(*) FROM task_instances WHERE workflow_instance_id='$INSTANCE_ID';" 2>/dev/null | xargs)
assert_eq "TaskInstance数量为1" "1" "$TASK_INST_COUNT"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§7 执行状态与控制${NC}"
echo -e "${YELLOW}========================================${NC}"

# 查看执行实例详情
RESP=$(api_call GET /instances/$INSTANCE_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "实例详情返回200" "200" "$STATUS"

# 查看执行历史
RESP=$(api_call GET /workflows/$TRIGGER_WF_ID/instances '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "执行历史返回200" "200" "$STATUS"

# 暂停
RESP=$(api_call POST /instances/$INSTANCE_ID/pause '{}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "暂停返回200" "200" "$STATUS"

# 恢复
RESP=$(api_call POST /instances/$INSTANCE_ID/resume '{}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "恢复返回200" "200" "$STATUS"

# 取消
RESP=$(api_call POST /instances/$INSTANCE_ID/cancel '{}' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "取消返回200" "200" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§1.1 REST/JSON 通信${NC}"
echo -e "${YELLOW}========================================${NC}"

# 验证 Content-Type
CONTENT_TYPE=$(curl -s -o /dev/null -w "%{content_type}" http://localhost:8080/api/v1/tasks -H "Authorization: Bearer $ADMIN_TOKEN")
assert_contains "Content-Type为application/json" "$CONTENT_TYPE" "application/json"

# 统一响应格式
RESP=$(api_call GET /tasks '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
assert_contains "响应包含code字段" "$BODY" '"code"'
assert_contains "响应包含message字段" "$BODY" '"message"'

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§12.6 执行节点接口${NC}"
echo -e "${YELLOW}========================================${NC}"

RESP=$(api_call GET /workers '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "Worker列表返回200" "200" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}§12.7 用户管理接口${NC}"
echo -e "${YELLOW}========================================${NC}"

# admin 删除用户
RESP=$(api_call POST /auth/register '{"username":"todelete","password":"password123"}')
BODY=$(echo "$RESP" | head -n -1)
DELETE_USER_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['user']['id'])" 2>/dev/null)

RESP=$(api_call DELETE /users/$DELETE_USER_ID '' "$ADMIN_TOKEN")
BODY=$(echo "$RESP" | head -n -1)
STATUS=$(echo "$RESP" | tail -n 1)
assert_http_status "admin删除用户" "200" "$STATUS"

# ================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}测试结果汇总${NC}"
echo -e "${YELLOW}========================================${NC}"
echo -e "${GREEN}PASS: $PASS${NC}"
echo -e "${RED}FAIL: $FAIL${NC}"

if [ ${#ISSUES[@]} -gt 0 ]; then
    echo -e "\n${RED}失败项:${NC}"
    for issue in "${ISSUES[@]}"; do
        echo -e "  ${RED}- $issue${NC}"
    done
fi

exit $FAIL
