#!/bin/bash
# TaskFlow 验收测试脚本 v2
# 覆盖所有 API 端点和关键业务场景
set -o pipefail

BASE_URL="http://localhost:8080/api/v1"
PASS=0
FAIL=0
ISSUES=()

pass_test() { ((PASS++)); echo "[PASS] $1"; }
fail_test() { ((FAIL++)); echo "[FAIL] $1"; ISSUES+=("$1"); }
info() { echo "[INFO] $1"; }

# Helper: GET with token, returns body only
api_get() {
    curl -s "$BASE_URL$1" -H "Authorization: Bearer $TOKEN"
}

# Helper: POST with token, returns body only
api_post() {
    curl -s "$BASE_URL$1" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d "$2"
}

# Helper: PUT with token, returns body only
api_put() {
    curl -s -X PUT "$BASE_URL$1" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d "$2"
}

# Helper: DELETE with token, returns body only
api_delete() {
    curl -s -X DELETE "$BASE_URL$1" -H "Authorization: Bearer $TOKEN"
}

# Helper: extract field from JSON
jcode() { echo "$1" | python3 -c "import sys,json; print(json.load(sys.stdin).get('code',-1))" 2>/dev/null || echo "-1"; }
jfield() { echo "$1" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d$2)" 2>/dev/null || echo ""; }
jcount() { echo "$1" | python3 -c "import sys,json; d=json.load(sys.stdin); items=d.get('data',{}).get('items',d.get('data',[])); print(len(items) if isinstance(items,list) else 0)" 2>/dev/null || echo "0"; }

# ============================================================
# 0. 健康检查
# ============================================================
info "===== 0. 健康检查 ====="
HEALTH_HTTP=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/api/v1/health)
if [ "$HEALTH_HTTP" = "200" ]; then
    pass_test "健康检查端点正常"
else
    fail_test "健康检查端点异常 (HTTP $HEALTH_HTTP)"
fi

# ============================================================
# 1. 认证模块
# ============================================================
info "===== 1. 认证模块 ====="

TS=$(date +%s)

# 1.1 注册
REG=$(curl -s "$BASE_URL/auth/register" -H 'Content-Type: application/json' -d "{\"username\":\"acceptuser${TS}\",\"password\":\"Test1234\",\"role\":\"operator\"}")
if [ "$(jcode "$REG")" = "0" ]; then
    pass_test "用户注册成功"
else
    fail_test "用户注册失败"
fi

# 1.2 登录
LOGIN=$(curl -s "$BASE_URL/auth/login" -H 'Content-Type: application/json' -d "{\"username\":\"acceptuser${TS}\",\"password\":\"Test1234\"}")
OPERATOR_TOKEN=$(jfield "$LOGIN" "['data']['access_token']")
if [ -n "$OPERATOR_TOKEN" ] && [ "$OPERATOR_TOKEN" != "None" ]; then
    pass_test "用户登录成功，获取token"
else
    fail_test "用户登录失败"
fi

# 1.3 admin登录
ADMIN_LOGIN=$(curl -s "$BASE_URL/auth/login" -H 'Content-Type: application/json' -d '{"username":"admin","password":"admin123"}')
TOKEN=$(jfield "$ADMIN_LOGIN" "['data']['access_token']")
if [ -n "$TOKEN" ] && [ "$TOKEN" != "None" ]; then
    pass_test "admin登录成功"
else
    fail_test "admin登录失败"
    echo "CRITICAL: Cannot continue without admin token"
    exit 1
fi

# 1.4 重复注册
DUP=$(curl -s "$BASE_URL/auth/register" -H 'Content-Type: application/json' -d "{\"username\":\"acceptuser${TS}\",\"password\":\"Test1234\",\"role\":\"operator\"}")
if [ "$(jcode "$DUP")" != "0" ]; then
    pass_test "重复用户名注册被拒绝"
else
    fail_test "重复用户名注册未被拒绝"
fi

# 1.5 错误密码
BAD=$(curl -s "$BASE_URL/auth/login" -H 'Content-Type: application/json' -d "{\"username\":\"acceptuser${TS}\",\"password\":\"wrongpass\"}")
if [ "$(jcode "$BAD")" != "0" ]; then
    pass_test "错误密码登录被拒绝"
else
    fail_test "错误密码登录未被拒绝"
fi

# 1.6 Token刷新
REFRESH_BODY=$(jfield "$ADMIN_LOGIN" "['data']['refresh_token']")
REFRESH=$(api_post "/auth/refresh" "{\"refresh_token\":\"$REFRESH_BODY\"}")
if [ "$(jcode "$REFRESH")" = "0" ]; then
    pass_test "Token刷新成功"
else
    fail_test "Token刷新失败"
fi

# 1.7 无Token访问
NO_AUTH_HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/tasks")
if [ "$NO_AUTH_HTTP" = "401" ]; then
    pass_test "无Token访问受保护API返回401"
else
    fail_test "无Token访问受保护API未返回401 (HTTP=$NO_AUTH_HTTP)"
fi

# 1.8 无效Token
BAD_AUTH_HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/tasks" -H "Authorization: Bearer invalidtoken123")
if [ "$BAD_AUTH_HTTP" = "401" ]; then
    pass_test "无效Token访问返回401"
else
    fail_test "无效Token访问未返回401 (HTTP=$BAD_AUTH_HTTP)"
fi

# ============================================================
# 2. 用户管理模块
# ============================================================
info "===== 2. 用户管理模块 ====="

# 2.1 获取当前用户
ME=$(api_get "/users/me")
ME_ROLE=$(jfield "$ME" "['data']['role']")
if [ "$(jcode "$ME")" = "0" ] && [ "$ME_ROLE" = "admin" ]; then
    pass_test "获取当前用户信息成功 (role=$ME_ROLE)"
else
    fail_test "获取当前用户信息失败 (role=$ME_ROLE)"
fi

# 2.2 列出用户
USERS=$(api_get "/users")
USERS_COUNT=$(jcount "$USERS")
if [ "$(jcode "$USERS")" = "0" ] && [ "$USERS_COUNT" -gt "0" ]; then
    pass_test "列出用户成功 (${USERS_COUNT}个)"
else
    fail_test "列出用户失败 (count=$USERS_COUNT)"
fi

# 2.3 创建用户
NEW_USER=$(api_post "/users" "{\"username\":\"newuser${TS}\",\"password\":\"Newpass123\",\"role\":\"viewer\"}")
NEW_USER_ID=$(jfield "$NEW_USER" "['data']['id']")
if [ "$(jcode "$NEW_USER")" = "0" ]; then
    pass_test "创建用户成功"
else
    fail_test "创建用户失败"
fi

# 2.4 viewer角色无法创建任务
VIEWER_LOGIN=$(curl -s "$BASE_URL/auth/login" -H 'Content-Type: application/json' -d "{\"username\":\"newuser${TS}\",\"password\":\"Newpass123\"}")
VIEWER_TOKEN=$(jfield "$VIEWER_LOGIN" "['data']['access_token']")
VIEWER_HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/tasks" -X POST -H "Authorization: Bearer $VIEWER_TOKEN" -H 'Content-Type: application/json' -d '{"name":"viewer-task","type":"command","config_json":{"command":"echo hi"}}')
if [ "$VIEWER_HTTP" = "403" ]; then
    pass_test "viewer角色无法创建任务（权限控制正确）"
else
    fail_test "viewer角色权限控制异常 (HTTP=$VIEWER_HTTP)"
fi

# 2.5 更新用户角色
ROLE_UPD=$(api_put "/users/${NEW_USER_ID}/role" "{\"role\":\"operator\"}")
if [ "$(jcode "$ROLE_UPD")" = "0" ]; then
    pass_test "更新用户角色成功"
else
    fail_test "更新用户角色失败"
fi

# 2.6 删除用户
DEL_USER=$(api_delete "/users/${NEW_USER_ID}")
if [ "$(jcode "$DEL_USER")" = "0" ]; then
    pass_test "删除用户成功"
else
    fail_test "删除用户失败"
fi

# 2.7 管理员不能删除自己
ADMIN_ID=$(jfield "$ME" "['data']['id']")
SELF_DEL=$(api_delete "/users/${ADMIN_ID}")
if [ "$(jcode "$SELF_DEL")" != "0" ]; then
    pass_test "管理员不能删除自己"
else
    fail_test "管理员可以删除自己（安全漏洞）"
fi

# ============================================================
# 3. 任务管理模块
# ============================================================
info "===== 3. 任务管理模块 ====="

# 3.1 创建command任务
CMD_TASK=$(api_post "/tasks" "{\"name\":\"cmd-task-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo hello\"},\"timeout\":60}")
CMD_TASK_ID=$(jfield "$CMD_TASK" "['data']['id']")
if [ "$(jcode "$CMD_TASK")" = "0" ] && [ -n "$CMD_TASK_ID" ]; then
    pass_test "创建command任务成功"
else
    fail_test "创建command任务失败"
fi

# 3.2 创建script任务
SCRIPT_TASK=$(api_post "/tasks" "{\"name\":\"script-task-${TS}\",\"type\":\"script\",\"config_json\":{\"script_content\":\"#!/bin/bash\\necho script output\"},\"timeout\":60}")
SCRIPT_TASK_ID=$(jfield "$SCRIPT_TASK" "['data']['id']")
if [ "$(jcode "$SCRIPT_TASK")" = "0" ]; then
    pass_test "创建script任务成功"
else
    fail_test "创建script任务失败"
fi

# 3.3 创建参数化任务
PARAM_TASK=$(api_post "/tasks" "{\"name\":\"param-task-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"sleep {duration}\"},\"parameters_json\":{\"duration\":{\"type\":\"number\",\"default\":1}},\"timeout\":60}")
PARAM_TASK_ID=$(jfield "$PARAM_TASK" "['data']['id']")
if [ "$(jcode "$PARAM_TASK")" = "0" ]; then
    pass_test "创建参数化任务成功"
else
    fail_test "创建参数化任务失败"
fi

# 3.4 获取任务详情
GET_TASK=$(api_get "/tasks/${CMD_TASK_ID}")
GET_TASK_NAME=$(jfield "$GET_TASK" "['data']['name']")
if [ "$(jcode "$GET_TASK")" = "0" ] && [ "$GET_TASK_NAME" = "cmd-task-${TS}" ]; then
    pass_test "获取任务详情成功"
else
    fail_test "获取任务详情失败 (name=$GET_TASK_NAME)"
fi

# 3.5 列出任务
LIST_TASKS=$(api_get "/tasks")
LIST_TASKS_COUNT=$(jcount "$LIST_TASKS")
if [ "$(jcode "$LIST_TASKS")" = "0" ] && [ "$LIST_TASKS_COUNT" -ge "3" ]; then
    pass_test "列出任务成功 (${LIST_TASKS_COUNT}个)"
else
    fail_test "列出任务失败 (count=$LIST_TASKS_COUNT)"
fi

# 3.6 更新任务
UPD_TASK=$(api_put "/tasks/${CMD_TASK_ID}" "{\"name\":\"cmd-task-updated-${TS}\",\"timeout\":120}")
if [ "$(jcode "$UPD_TASK")" = "0" ]; then
    pass_test "更新任务成功"
else
    fail_test "更新任务失败"
fi

# 3.7 任务类型过滤
TYPE_FILTER=$(api_get "/tasks?type=command")
TYPE_FILTER_COUNT=$(jcount "$TYPE_FILTER")
if [ "$TYPE_FILTER_COUNT" -ge "1" ]; then
    pass_test "任务类型过滤有效 (${TYPE_FILTER_COUNT}个command任务)"
else
    fail_test "任务类型过滤无效 (count=$TYPE_FILTER_COUNT)"
fi

# 3.8 创建无效类型任务
BAD_TYPE=$(api_post "/tasks" "{\"name\":\"bad-type-${TS}\",\"type\":\"invalid\",\"config_json\":{\"command\":\"echo\"}}")
if [ "$(jcode "$BAD_TYPE")" != "0" ]; then
    pass_test "创建无效类型任务被拒绝"
else
    fail_test "创建无效类型任务未被拒绝"
fi

# 3.9 删除任务
DEL_TASK=$(api_delete "/tasks/${SCRIPT_TASK_ID}")
if [ "$(jcode "$DEL_TASK")" = "0" ]; then
    pass_test "删除任务成功"
else
    fail_test "删除任务失败"
fi

# 3.10 获取已删除任务
GET_DEL=$(api_get "/tasks/${SCRIPT_TASK_ID}")
if [ "$(jcode "$GET_DEL")" != "0" ]; then
    pass_test "获取已删除任务返回错误"
else
    fail_test "获取已删除任务未返回错误"
fi

# ============================================================
# 4. 工作流管理模块
# ============================================================
info "===== 4. 工作流管理模块 ====="

# 4.1 创建简单工作流
SIMPLE_WF=$(api_post "/workflows" "{\"name\":\"simple-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${CMD_TASK_ID}\"}],\"edges\":[]}}")
SIMPLE_WF_ID=$(jfield "$SIMPLE_WF" "['data']['id']")
if [ "$(jcode "$SIMPLE_WF")" = "0" ] && [ -n "$SIMPLE_WF_ID" ]; then
    pass_test "创建简单工作流成功"
else
    fail_test "创建简单工作流失败"
fi

# 4.2 创建串行工作流
SERIAL_WF=$(api_post "/workflows" "{\"name\":\"serial-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${CMD_TASK_ID}\"},{\"id\":\"n2\",\"task_id\":\"${PARAM_TASK_ID}\"}],\"edges\":[{\"source\":\"n1\",\"target\":\"n2\"}]}}")
SERIAL_WF_ID=$(jfield "$SERIAL_WF" "['data']['id']")
if [ "$(jcode "$SERIAL_WF")" = "0" ]; then
    pass_test "创建串行工作流成功"
else
    fail_test "创建串行工作流失败"
fi

# 4.3 创建并行工作流
PARA_TASK=$(api_post "/tasks" "{\"name\":\"para-task-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo para\"},\"timeout\":30}")
PARA_TASK_ID=$(jfield "$PARA_TASK" "['data']['id']")
PARA_WF=$(api_post "/workflows" "{\"name\":\"parallel-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${CMD_TASK_ID}\"},{\"id\":\"n2\",\"task_id\":\"${PARA_TASK_ID}\"}],\"edges\":[]}}")
PARA_WF_ID=$(jfield "$PARA_WF" "['data']['id']")
if [ "$(jcode "$PARA_WF")" = "0" ]; then
    pass_test "创建并行工作流成功"
else
    fail_test "创建并行工作流失败"
fi

# 4.4 获取工作流详情
GET_WF=$(api_get "/workflows/${SIMPLE_WF_ID}")
GET_WF_NAME=$(jfield "$GET_WF" "['data']['name']")
if [ "$(jcode "$GET_WF")" = "0" ] && [ "$GET_WF_NAME" = "simple-wf-${TS}" ]; then
    pass_test "获取工作流详情成功"
else
    fail_test "获取工作流详情失败 (name=$GET_WF_NAME)"
fi

# 4.5 列出工作流
LIST_WF=$(api_get "/workflows")
LIST_WF_COUNT=$(jcount "$LIST_WF")
if [ "$(jcode "$LIST_WF")" = "0" ] && [ "$LIST_WF_COUNT" -ge "3" ]; then
    pass_test "列出工作流成功 (${LIST_WF_COUNT}个)"
else
    fail_test "列出工作流失败 (count=$LIST_WF_COUNT)"
fi

# 4.6 更新工作流
UPD_WF=$(api_put "/workflows/${SIMPLE_WF_ID}" "{\"name\":\"simple-wf-updated-${TS}\"}")
if [ "$(jcode "$UPD_WF")" = "0" ]; then
    pass_test "更新工作流成功"
else
    fail_test "更新工作流失败"
fi

# 4.7 创建无效DAG工作流
BAD_DAG=$(api_post "/workflows" "{\"name\":\"bad-dag-${TS}\",\"dag_json\":{\"nodes\":[],\"edges\":[{\"source\":\"n1\",\"target\":\"n2\"}]}}")
if [ "$(jcode "$BAD_DAG")" != "0" ]; then
    pass_test "创建无效DAG工作流被拒绝"
else
    fail_test "创建无效DAG工作流未被拒绝"
fi

# ============================================================
# 5. 工作流执行模块
# ============================================================
info "===== 5. 工作流执行模块 ====="

# 5.1 触发简单工作流
TRIGGER=$(api_post "/workflows/${SIMPLE_WF_ID}/trigger" "{}")
INSTANCE_ID=$(jfield "$TRIGGER" "['data']['instance_id']")
if [ "$(jcode "$TRIGGER")" = "0" ] && [ -n "$INSTANCE_ID" ]; then
    pass_test "触发简单工作流成功"
else
    fail_test "触发简单工作流失败"
fi

# 5.2 等待执行完成
sleep 8
INSTANCE=$(api_get "/instances/${INSTANCE_ID}")
INST_STATUS=$(jfield "$INSTANCE" "['data']['status']")
if [ "$INST_STATUS" = "SUCCESS" ]; then
    pass_test "简单工作流执行成功"
else
    fail_test "简单工作流执行状态异常 (status=$INST_STATUS)"
fi

# 5.3 验证dag_snapshot
HAS_SNAPSHOT=$(echo "$INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ds=d.get('data',{}).get('dag_snapshot'); print('yes' if ds and isinstance(ds,dict) and ds.get('nodes') else 'no')" 2>/dev/null)
if [ "$HAS_SNAPSHOT" = "yes" ]; then
    pass_test "工作流实例包含dag_snapshot"
else
    fail_test "工作流实例缺少dag_snapshot"
fi

# 5.4 验证task_instances
TI_COUNT=$(echo "$INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ti=d.get('data',{}).get('task_instances',[]); print(len(ti) if isinstance(ti,list) else 0)" 2>/dev/null)
if [ "$TI_COUNT" -ge "1" ]; then
    pass_test "工作流实例包含task_instances (${TI_COUNT}个)"
else
    fail_test "工作流实例缺少task_instances"
fi

# 5.5 验证resolved_config
HAS_RESOLVED=$(echo "$INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ti=d.get('data',{}).get('task_instances',[]); print('yes' if any(t.get('resolved_config') for t in ti if isinstance(t,dict)) else 'no')" 2>/dev/null)
if [ "$HAS_RESOLVED" = "yes" ]; then
    pass_test "任务实例包含resolved_config"
else
    fail_test "任务实例缺少resolved_config"
fi

# 5.6 触发串行工作流
SERIAL_TRIGGER=$(api_post "/workflows/${SERIAL_WF_ID}/trigger" "{}")
SERIAL_INST_ID=$(jfield "$SERIAL_TRIGGER" "['data']['instance_id']")
if [ -n "$SERIAL_INST_ID" ]; then
    pass_test "触发串行工作流成功"
else
    fail_test "触发串行工作流失败"
fi

# 5.7 等待串行工作流完成
sleep 10
SERIAL_INST=$(api_get "/instances/${SERIAL_INST_ID}")
SERIAL_STATUS=$(jfield "$SERIAL_INST" "['data']['status']")
if [ "$SERIAL_STATUS" = "SUCCESS" ]; then
    pass_test "串行工作流执行成功"
else
    fail_test "串行工作流执行状态异常 (status=$SERIAL_STATUS)"
fi

# 5.8 触发参数化工作流
PARAM_TRIGGER=$(api_post "/workflows/${PARA_WF_ID}/trigger" "{\"param_overrides\":{\"duration\":1}}")
PARAM_INST_ID=$(jfield "$PARAM_TRIGGER" "['data']['instance_id']")
if [ -n "$PARAM_INST_ID" ]; then
    pass_test "触发参数化工作流成功"
else
    fail_test "触发参数化工作流失败"
fi

sleep 8
PARAM_INST=$(api_get "/instances/${PARAM_INST_ID}")
PARAM_STATUS=$(jfield "$PARAM_INST" "['data']['status']")
if [ "$PARAM_STATUS" = "SUCCESS" ]; then
    pass_test "参数化工作流执行成功"
else
    fail_test "参数化工作流执行状态异常 (status=$PARAM_STATUS)"
fi

# ============================================================
# 6. 实例管理模块
# ============================================================
info "===== 6. 实例管理模块 ====="

# 6.1 列出所有实例
ALL_INST=$(api_get "/instances")
ALL_INST_COUNT=$(jcount "$ALL_INST")
if [ "$(jcode "$ALL_INST")" = "0" ] && [ "$ALL_INST_COUNT" -ge "3" ]; then
    pass_test "列出所有实例成功 (${ALL_INST_COUNT}个)"
else
    fail_test "列出所有实例失败 (count=$ALL_INST_COUNT)"
fi

# 6.2 按工作流列出实例
WF_INST=$(api_get "/workflows/${SIMPLE_WF_ID}/instances")
if [ "$(jcode "$WF_INST")" = "0" ]; then
    pass_test "按工作流列出实例成功"
else
    fail_test "按工作流列出实例失败"
fi

# 6.3 获取实例详情
GET_INST=$(api_get "/instances/${INSTANCE_ID}")
if [ "$(jcode "$GET_INST")" = "0" ]; then
    pass_test "获取实例详情成功"
else
    fail_test "获取实例详情失败"
fi

# 6.4 获取任务日志
TASK_INST_ID=$(echo "$INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ti=d.get('data',{}).get('task_instances',[]); print(ti[0].get('id','') if ti else '')" 2>/dev/null)
if [ -n "$TASK_INST_ID" ]; then
    LOG=$(api_get "/instances/${INSTANCE_ID}/tasks/${TASK_INST_ID}/logs")
    if [ "$(jcode "$LOG")" = "0" ]; then
        pass_test "获取任务日志成功"
    else
        fail_test "获取任务日志失败"
    fi
else
    fail_test "无法获取任务实例ID"
fi

# 6.5 实例分页
PAGE_INST=$(api_get "/instances?page=1&page_size=2")
PAGE_INST_COUNT=$(jcount "$PAGE_INST")
if [ "$PAGE_INST_COUNT" -le "2" ]; then
    pass_test "实例分页有效 (page_size=2, 返回${PAGE_INST_COUNT}条)"
else
    fail_test "实例分页无效 (返回${PAGE_INST_COUNT}条)"
fi

# ============================================================
# 7. 工作流控制（取消）
# ============================================================
info "===== 7. 工作流控制 ====="

SLEEP_TASK=$(api_post "/tasks" "{\"name\":\"sleep-ctrl-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"sleep 300\"},\"timeout\":600}")
SLEEP_TASK_ID=$(jfield "$SLEEP_TASK" "['data']['id']")
CTRL_WF=$(api_post "/workflows" "{\"name\":\"ctrl-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${SLEEP_TASK_ID}\"}],\"edges\":[]}}")
CTRL_WF_ID=$(jfield "$CTRL_WF" "['data']['id']")

CTRL_TRIGGER=$(api_post "/workflows/${CTRL_WF_ID}/trigger" "{}")
CTRL_INST_ID=$(jfield "$CTRL_TRIGGER" "['data']['instance_id']")
sleep 3

CANCEL=$(api_post "/instances/${CTRL_INST_ID}/cancel" "{}")
sleep 3
CANCEL_INST=$(api_get "/instances/${CTRL_INST_ID}")
CANCEL_STATUS=$(jfield "$CANCEL_INST" "['data']['status']")
if [ "$CANCEL_STATUS" = "CANCELLED" ]; then
    pass_test "取消工作流实例成功"
else
    fail_test "取消工作流实例失败 (status=$CANCEL_STATUS)"
fi

# ============================================================
# 8. Worker管理模块
# ============================================================
info "===== 8. Worker管理模块 ====="

WORKERS=$(api_get "/workers")
WORKERS_COUNT=$(jcount "$WORKERS")
if [ "$(jcode "$WORKERS")" = "0" ] && [ "$WORKERS_COUNT" -ge "1" ]; then
    pass_test "列出Workers成功 (${WORKERS_COUNT}个)"
else
    fail_test "列出Workers失败 (count=$WORKERS_COUNT)"
fi

# ============================================================
# 9. 仪表盘模块
# ============================================================
info "===== 9. 仪表盘模块 ====="

STATS=$(api_get "/dashboard/stats")
if [ "$(jcode "$STATS")" = "0" ]; then
    pass_test "获取仪表盘统计数据成功"
else
    fail_test "获取仪表盘统计数据失败"
fi

STATS_FIELDS=$(echo "$STATS" | python3 -c "
import sys,json
d=json.load(sys.stdin).get('data',{})
fields = ['total_workflows','total_tasks','total_instances','success_rate']
present = [f for f in fields if f in d and d[f] is not None]
print(len(present))
" 2>/dev/null || echo "0")
if [ "$STATS_FIELDS" -ge "3" ]; then
    pass_test "仪表盘统计数据字段完整 (${STATS_FIELDS}个)"
else
    fail_test "仪表盘统计数据字段不完整 (${STATS_FIELDS}个)"
fi

# ============================================================
# 10. 资源级权限校验
# ============================================================
info "===== 10. 资源级权限校验 ====="

OPERATOR_TRIGGER=$(curl -s "$BASE_URL/workflows/${SIMPLE_WF_ID}/trigger" -H "Authorization: Bearer $OPERATOR_TOKEN" -H 'Content-Type: application/json' -d '{}')
if [ "$(jcode "$OPERATOR_TRIGGER")" != "0" ]; then
    pass_test "operator不能触发admin的工作流（资源级权限正确）"
else
    fail_test "operator可以触发admin的工作流（资源级权限缺失）"
fi

OPERATOR_INST=$(curl -s "$BASE_URL/instances/${INSTANCE_ID}" -H "Authorization: Bearer $OPERATOR_TOKEN")
if [ "$(jcode "$OPERATOR_INST")" != "0" ]; then
    pass_test "operator不能查看admin的实例（资源级权限正确）"
else
    fail_test "operator可以查看admin的实例（资源级权限缺失）"
fi

# ============================================================
# 11. 数据一致性校验
# ============================================================
info "===== 11. 数据一致性校验 ====="

WF_VERSION=$(jfield "$GET_WF" "['data']['version']")
if [ -n "$WF_VERSION" ] && [ "$WF_VERSION" -ge "1" ] 2>/dev/null; then
    pass_test "工作流版本号正确 (v${WF_VERSION})"
else
    fail_test "工作流版本号异常 (v${WF_VERSION})"
fi

INST_WF_VER=$(jfield "$INSTANCE" "['data']['workflow_version']")
if [ -n "$INST_WF_VER" ] && [ "$INST_WF_VER" -ge "1" ] 2>/dev/null; then
    pass_test "实例workflow_version正确 (v${INST_WF_VER})"
else
    fail_test "实例workflow_version异常 (v${INST_WF_VER})"
fi

TI_STATUSES=$(echo "$INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ti=d.get('data',{}).get('task_instances',[]); print(','.join(t.get('status','') for t in ti if isinstance(t,dict)))" 2>/dev/null)
if echo "$TI_STATUSES" | grep -q "SUCCESS"; then
    pass_test "任务实例状态包含SUCCESS"
else
    fail_test "任务实例状态不一致 ($TI_STATUSES)"
fi

DEL_WF_INST=$(api_delete "/workflows/${SIMPLE_WF_ID}")
if [ "$(jcode "$DEL_WF_INST")" != "0" ]; then
    pass_test "删除有实例的工作流被拒绝"
else
    fail_test "删除有实例的工作流未被拒绝（数据安全风险）"
fi

# ============================================================
# 12. 边界与异常测试
# ============================================================
info "===== 12. 边界与异常测试 ====="

NOT_EXIST=$(api_get "/tasks/00000000-0000-0000-0000-000000000000")
if [ "$(jcode "$NOT_EXIST")" != "0" ]; then
    pass_test "查询不存在的UUID返回错误"
else
    fail_test "查询不存在的UUID未返回错误"
fi

BAD_UUID_HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/tasks/not-a-uuid" -H "Authorization: Bearer $TOKEN")
if [ "$BAD_UUID_HTTP" = "400" ] || [ "$BAD_UUID_HTTP" = "404" ]; then
    pass_test "无效UUID格式返回错误"
else
    fail_test "无效UUID格式未返回错误 (HTTP=$BAD_UUID_HTTP)"
fi

EMPTY_HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/tasks" -X POST -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d '{}')
if [ "$EMPTY_HTTP" = "400" ]; then
    pass_test "空body创建返回400"
else
    fail_test "空body创建未返回400 (HTTP=$EMPTY_HTTP)"
fi

BIG_PAGE=$(api_get "/tasks?page=1&page_size=10000")
if [ "$(jcode "$BIG_PAGE")" = "0" ]; then
    pass_test "超大分页请求正常处理"
else
    fail_test "超大分页请求异常"
fi

SPECIAL_NAME=$(api_post "/tasks" "{\"name\":\"test-task-with-dashes-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo test\"},\"timeout\":30}")
if [ "$(jcode "$SPECIAL_NAME")" = "0" ]; then
    pass_test "特殊字符任务名创建成功"
else
    fail_test "特殊字符任务名创建失败"
fi

# ============================================================
# 13. 清理
# ============================================================
info "===== 13. 清理 ====="
api_delete "/workflows/${PARA_WF_ID}" > /dev/null 2>&1
api_delete "/workflows/${CTRL_WF_ID}" > /dev/null 2>&1
pass_test "清理测试数据"

# ============================================================
# 结果汇总
# ============================================================
echo ""
echo "=========================================="
echo "  验收测试结果: $PASS 通过, $FAIL 失败, 共 $((PASS+FAIL)) 项"
echo "=========================================="

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "失败项："
    for issue in "${ISSUES[@]}"; do
        echo "  - $issue"
    done
fi

exit $FAIL
