#!/usr/bin/env bash
# TaskFlow 集成测试脚本 - 测试任务和工作流的创建、执行等情况
set -o pipefail

BASE_URL="http://localhost:8080/api/v1"
TOKEN=""
PASS=0
FAIL=0
TOTAL=0
TS=$(date +%s)

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'; NC='\033[0m'
log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); TOTAL=$((TOTAL+1)); }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1)); }
log_info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

assert_eq() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$expected" = "$actual" ]; then log_pass "$desc"; else log_fail "$desc (expected='$expected', actual='$actual')"; fi
}
assert_json() {
    local desc="$1" json="$2" key="$3" expected="$4"
    local actual; actual=$(echo "$json" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d$key)" 2>/dev/null || echo "PARSE_ERROR")
    if [ "$actual" = "$expected" ]; then log_pass "$desc"; else log_fail "$desc (key=$key expected='$expected', actual='$actual')"; fi
}

# 等待工作流实例完成，返回完整实例JSON
wait_instance() {
    local iid="$1" max="${2:-120}" elapsed=0
    while [ $elapsed -lt $max ]; do
        local r; r=$(curl -s -X GET "$BASE_URL/instances/$iid" -H "$AUTH_HEADER")
        local s; s=$(echo "$r" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['status'])" 2>/dev/null || echo "UNKNOWN")
        case "$s" in
            SUCCESS|FAILED|CANCELLED|TIMEOUT|UPSTREAM_FAILED|NODE_OFFLINE)
                echo "$r"; return 0
                ;;
        esac
        sleep 3; elapsed=$((elapsed+3))
    done
    curl -s -X GET "$BASE_URL/instances/$iid" -H "$AUTH_HEADER"
}

# ==================== 1. 认证 ====================
log_info "===== 1. 认证 ====="
USER="ituser${TS}"
REG=$(curl -s -X POST "$BASE_URL/auth/register" -H "Content-Type: application/json" \
  -d "{\"username\":\"$USER\",\"password\":\"Test123456\",\"email\":\"${USER}@test.com\"}")
assert_json "注册用户" "$REG" "['code']" "0"

LOGIN=$(curl -s -X POST "$BASE_URL/auth/login" -H "Content-Type: application/json" \
  -d "{\"username\":\"$USER\",\"password\":\"Test123456\"}")
TOKEN=$(echo "$LOGIN" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['access_token'])" 2>/dev/null || echo "")
if [ -n "$TOKEN" ]; then log_pass "登录获取token"; else log_fail "登录获取token失败"; exit 1; fi
AUTH_HEADER="Authorization: Bearer $TOKEN"

# ==================== 2. 创建任务 ====================
log_info "===== 2. 创建任务 ====="

# 2.1 简单echo任务
T1=$(curl -s -X POST "$BASE_URL/tasks" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"echo-${TS}\",\"type\":\"command\",\"description\":\"echo task\",\"config_json\":{\"command\":\"echo Hello World\"},\"timeout\":60}")
T1_ID=$(echo "$T1" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建echo任务" "$T1" "['code']" "0"

# 2.2 带参数sleep任务（{var}语法）
T2=$(curl -s -X POST "$BASE_URL/tasks" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"sleep-${TS}\",\"type\":\"command\",\"description\":\"sleep task\",\"config_json\":{\"command\":\"sleep {time}\"},\"parameters_json\":{\"time\":\"1\"},\"timeout\":60}")
T2_ID=$(echo "$T2" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建sleep任务({var}语法)" "$T2" "['code']" "0"

# 2.3 带参数greet任务（${var}语法）
T3=$(curl -s -X POST "$BASE_URL/tasks" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d '{
    "name": "greet-'"${TS}"'",
    "type": "command",
    "description": "greet task",
    "config_json": {"command": "echo Hello ${name}"},
    "parameters_json": {"name": "World"},
    "timeout": 60
  }')
T3_ID=$(echo "$T3" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建greet任务(${var}语法)" "$T3" "['code']" "0"

# 2.4 多参数任务
T4=$(curl -s -X POST "$BASE_URL/tasks" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"multi-${TS}\",\"type\":\"command\",\"description\":\"multi param task\",\"config_json\":{\"command\":\"echo {greeting} {name}\"},\"parameters_json\":{\"greeting\":\"Hi\",\"name\":\"User\"},\"timeout\":60}")
T4_ID=$(echo "$T4" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建多参数任务" "$T4" "['code']" "0"

# 2.5 script类型任务
T5=$(curl -s -X POST "$BASE_URL/tasks" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"script-${TS}\",\"type\":\"script\",\"description\":\"script task\",\"config_json\":{\"script_content\":\"#!/bin/bash\\necho script output\"},\"timeout\":60}")
T5_ID=$(echo "$T5" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建script任务" "$T5" "['code']" "0"

# ==================== 3. 任务查询 ====================
log_info "===== 3. 任务查询 ====="
GT1=$(curl -s -X GET "$BASE_URL/tasks/$T1_ID" -H "$AUTH_HEADER")
assert_json "获取单个任务" "$GT1" "['code']" "0"
assert_json "任务名称正确" "$GT1" "['data']['name']" "echo-${TS}"
assert_json "任务类型正确" "$GT1" "['data']['type']" "command"

GT2=$(curl -s -X GET "$BASE_URL/tasks/$T2_ID" -H "$AUTH_HEADER")
PARAM=$(echo "$GT2" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['parameters_json']['time'])" 2>/dev/null)
assert_eq "参数定义已保存(time=1)" "1" "$PARAM"

GT3=$(curl -s -X GET "$BASE_URL/tasks/$T3_ID" -H "$AUTH_HEADER")
PARAM3=$(echo "$GT3" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['parameters_json']['name'])" 2>/dev/null)
assert_eq "参数定义已保存(name=World)" "World" "$PARAM3"

LIST=$(curl -s -X GET "$BASE_URL/tasks?page=1&page_size=10" -H "$AUTH_HEADER")
assert_json "列出任务" "$LIST" "['code']" "0"

# ==================== 4. 创建工作流 ====================
log_info "===== 4. 创建工作流 ====="

# 4.1 简单单节点工作流
W1=$(curl -s -X POST "$BASE_URL/workflows" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"wf-echo-${TS}\",\"description\":\"simple echo workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$T1_ID\"}],\"edges\":[]},\"schedule_strategy\":\"random\"}")
W1_ID=$(echo "$W1" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建简单工作流" "$W1" "['code']" "0"

# 4.2 参数化工作流（sleep）
W2=$(curl -s -X POST "$BASE_URL/workflows" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"wf-sleep-${TS}\",\"description\":\"parameterized sleep workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$T2_ID\"}],\"edges\":[]},\"schedule_strategy\":\"random\"}")
W2_ID=$(echo "$W2" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建参数化工作流" "$W2" "['code']" "0"

# 4.3 串行工作流（echo -> sleep -> echo），中间节点带参数覆盖
W3=$(curl -s -X POST "$BASE_URL/workflows" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"wf-serial-${TS}\",\"description\":\"serial workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$T1_ID\"},{\"id\":\"n2\",\"task_id\":\"$T2_ID\",\"param_overrides\":{\"time\":\"2\"}},{\"id\":\"n3\",\"task_id\":\"$T1_ID\"}],\"edges\":[{\"source\":\"n1\",\"target\":\"n2\"},{\"source\":\"n2\",\"target\":\"n3\"}]},\"schedule_strategy\":\"random\"}")
W3_ID=$(echo "$W3" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建串行工作流" "$W3" "['code']" "0"

# 4.4 并行工作流
W4=$(curl -s -X POST "$BASE_URL/workflows" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"wf-parallel-${TS}\",\"description\":\"parallel workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$T1_ID\"},{\"id\":\"n2\",\"task_id\":\"$T1_ID\"}],\"edges\":[]},\"schedule_strategy\":\"random\"}")
W4_ID=$(echo "$W4" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建并行工作流" "$W4" "['code']" "0"

# 4.5 dollar-brace参数工作流
W5=$(curl -s -X POST "$BASE_URL/workflows" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d '{
    "name": "wf-greet-'"${TS}"'",
    "description": "greet workflow",
    "dag_json": {"nodes": [{"id": "n1", "task_id": "'"$T3_ID"'"}], "edges": []},
    "schedule_strategy": "random"
  }')
W5_ID=$(echo "$W5" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建dollar-brace工作流" "$W5" "['code']" "0"

# 4.6 菱形DAG工作流
W6=$(curl -s -X POST "$BASE_URL/workflows" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"wf-diamond-${TS}\",\"description\":\"diamond DAG workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"A\",\"task_id\":\"$T1_ID\"},{\"id\":\"B\",\"task_id\":\"$T1_ID\"},{\"id\":\"C\",\"task_id\":\"$T1_ID\"},{\"id\":\"D\",\"task_id\":\"$T1_ID\"}],\"edges\":[{\"source\":\"A\",\"target\":\"B\"},{\"source\":\"A\",\"target\":\"C\"},{\"source\":\"B\",\"target\":\"D\"},{\"source\":\"C\",\"target\":\"D\"}]},\"schedule_strategy\":\"random\"}")
W6_ID=$(echo "$W6" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建菱形DAG工作流" "$W6" "['code']" "0"

# ==================== 5. 触发与执行 ====================
log_info "===== 5. 触发与执行 ====="

# 5.1 简单echo工作流
log_info "--- 5.1 简单echo工作流 ---"
TR1=$(curl -s -X POST "$BASE_URL/workflows/$W1_ID/trigger" -H "Content-Type: application/json" -H "$AUTH_HEADER" -d '{}')
I1_ID=$(echo "$TR1" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['instance_id'])" 2>/dev/null)
assert_json "触发简单工作流" "$TR1" "['code']" "0"
I1=$(wait_instance "$I1_ID" 120)
I1_S=$(echo "$I1" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['status'])" 2>/dev/null || echo "UNKNOWN")
assert_eq "简单echo工作流SUCCESS" "SUCCESS" "$I1_S"

# 5.2 参数化sleep工作流（传入time=2）
log_info "--- 5.2 参数化sleep工作流 ---"
TR2=$(curl -s -X POST "$BASE_URL/workflows/$W2_ID/trigger" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d '{"param_overrides":{"time":"2"}}')
I2_ID=$(echo "$TR2" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['instance_id'])" 2>/dev/null)
assert_json "触发参数化工作流" "$TR2" "['code']" "0"
I2=$(wait_instance "$I2_ID" 120)
I2_S=$(echo "$I2" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['status'])" 2>/dev/null || echo "UNKNOWN")
assert_eq "参数化sleep工作流SUCCESS" "SUCCESS" "$I2_S"

# 验证resolved_config
RC=$(echo "$I2" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for ti in d['data']['task_instances']:
    rc = ti.get('resolved_config')
    if rc: print(json.dumps(rc)); break
" 2>/dev/null || echo "")
log_info "resolved_config: $RC"
if [ -n "$RC" ] && echo "$RC" | grep -q "command"; then
    log_pass "resolved_config包含解析后命令"
    if echo "$RC" | grep -q "sleep 2"; then
        log_pass "占位符{time}已被替换为2"
    else
        log_fail "占位符{time}未被替换 ($RC)"
    fi
else
    log_fail "resolved_config为空"
fi

# 验证param_overrides
PO=$(echo "$I2" | python3 -c "
import sys,json
d=json.load(sys.stdin)
po = d['data'].get('param_overrides', {})
print(json.dumps(po))
" 2>/dev/null || echo "")
if echo "$PO" | grep -q '"time"'; then log_pass "工作流实例包含param_overrides"; else log_fail "工作流实例不包含param_overrides"; fi

# 5.3 串行工作流
log_info "--- 5.3 串行工作流 ---"
TR3=$(curl -s -X POST "$BASE_URL/workflows/$W3_ID/trigger" -H "Content-Type: application/json" -H "$AUTH_HEADER" -d '{}')
I3_ID=$(echo "$TR3" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['instance_id'])" 2>/dev/null)
assert_json "触发串行工作流" "$TR3" "['code']" "0"
I3=$(wait_instance "$I3_ID" 120)
I3_S=$(echo "$I3" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['status'])" 2>/dev/null || echo "UNKNOWN")
assert_eq "串行工作流SUCCESS" "SUCCESS" "$I3_S"
TC3=$(echo "$I3" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d['data']['task_instances']))" 2>/dev/null || echo "0")
assert_eq "串行工作流3个任务实例" "3" "$TC3"

# 验证中间节点参数覆盖
N2_RC=$(echo "$I3" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for ti in d['data']['task_instances']:
    rc = ti.get('resolved_config')
    if rc and 'command' in rc:
        cmd = rc.get('command','')
        if 'sleep' in cmd: print(cmd)
" 2>/dev/null || echo "")
if echo "$N2_RC" | grep -q "sleep 2"; then log_pass "串行中间节点参数覆盖生效(sleep 2)"; else log_fail "串行中间节点参数覆盖未生效(got: $N2_RC)"; fi

# 5.4 并行工作流
log_info "--- 5.4 并行工作流 ---"
TR4=$(curl -s -X POST "$BASE_URL/workflows/$W4_ID/trigger" -H "Content-Type: application/json" -H "$AUTH_HEADER" -d '{}')
I4_ID=$(echo "$TR4" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['instance_id'])" 2>/dev/null)
assert_json "触发并行工作流" "$TR4" "['code']" "0"
I4=$(wait_instance "$I4_ID" 120)
I4_S=$(echo "$I4" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['status'])" 2>/dev/null || echo "UNKNOWN")
assert_eq "并行工作流SUCCESS" "SUCCESS" "$I4_S"
TC4=$(echo "$I4" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d['data']['task_instances']))" 2>/dev/null || echo "0")
assert_eq "并行工作流2个任务实例" "2" "$TC4"

# 5.5 dollar-brace参数工作流
log_info "--- 5.5 dollar-brace参数工作流 ---"
TR5=$(curl -s -X POST "$BASE_URL/workflows/$W5_ID/trigger" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d '{"param_overrides":{"name":"TaskFlow"}}')
I5_ID=$(echo "$TR5" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['instance_id'])" 2>/dev/null)
assert_json "触发dollar-brace工作流" "$TR5" "['code']" "0"
I5=$(wait_instance "$I5_ID" 120)
I5_S=$(echo "$I5" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['status'])" 2>/dev/null || echo "UNKNOWN")
assert_eq "dollar-brace工作流SUCCESS" "SUCCESS" "$I5_S"

# 验证dollar-brace替换
RC5=$(echo "$I5" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for ti in d['data']['task_instances']:
    rc = ti.get('resolved_config')
    if rc and 'command' in rc: print(rc['command'])
" 2>/dev/null || echo "")
if echo "$RC5" | grep -q "TaskFlow"; then log_pass "dollar-brace占位符替换正确"; else log_fail "dollar-brace占位符替换不正确(got: $RC5)"; fi

# 5.6 菱形DAG工作流
log_info "--- 5.6 菱形DAG工作流 ---"
TR6=$(curl -s -X POST "$BASE_URL/workflows/$W6_ID/trigger" -H "Content-Type: application/json" -H "$AUTH_HEADER" -d '{}')
I6_ID=$(echo "$TR6" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['instance_id'])" 2>/dev/null)
assert_json "触发菱形DAG工作流" "$TR6" "['code']" "0"
I6=$(wait_instance "$I6_ID" 120)
I6_S=$(echo "$I6" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['status'])" 2>/dev/null || echo "UNKNOWN")
assert_eq "菱形DAG工作流SUCCESS" "SUCCESS" "$I6_S"
TC6=$(echo "$I6" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d['data']['task_instances']))" 2>/dev/null || echo "0")
assert_eq "菱形DAG工作流4个任务实例" "4" "$TC6"

# ==================== 6. 实例查询 ====================
log_info "===== 6. 实例查询 ====="
AI=$(curl -s -X GET "$BASE_URL/instances?page=1&page_size=20" -H "$AUTH_HEADER")
assert_json "列出所有实例" "$AI" "['code']" "0"

WI=$(curl -s -X GET "$BASE_URL/workflows/$W1_ID/instances?page=1&page_size=10" -H "$AUTH_HEADER")
assert_json "按工作流ID列出实例" "$WI" "['code']" "0"

# 获取单个实例详情
GI=$(curl -s -X GET "$BASE_URL/instances/$I1_ID" -H "$AUTH_HEADER")
assert_json "获取单个实例" "$GI" "['code']" "0"
assert_json "实例状态正确" "$GI" "['data']['status']" "SUCCESS"

# ==================== 7. 任务日志 ====================
log_info "===== 7. 任务日志 ====="
TI_ID=$(echo "$I1" | python3 -c "import sys,json; d=json.load(sys.stdin); tis=d['data']['task_instances']; print(tis[0]['id'] if tis else '')" 2>/dev/null)
if [ -n "$TI_ID" ]; then
    LOG=$(curl -s -X GET "$BASE_URL/instances/$I1_ID/tasks/$TI_ID/logs" -H "$AUTH_HEADER")
    assert_json "获取任务日志" "$LOG" "['code']" "0"
else
    log_fail "无法获取任务实例ID"
fi

# ==================== 8. 边界测试 ====================
log_info "===== 8. 边界测试 ====="

# 无效UUID
INV=$(curl -s -X GET "$BASE_URL/tasks/not-a-uuid" -H "$AUTH_HEADER")
assert_json "无效UUID返回错误" "$INV" "['code']" "40001"

# 不存在的资源
NF=$(curl -s -X GET "$BASE_URL/tasks/00000000-0000-0000-0000-000000000000" -H "$AUTH_HEADER")
assert_json "不存在的任务返回错误" "$NF" "['code']" "40401"

# 无认证访问
NO_AUTH=$(curl -s -X GET "$BASE_URL/tasks" -H "Content-Type: application/json")
assert_json "无认证返回错误" "$NO_AUTH" "['code']" "40101"

# 无效的任务类型
BAD_TYPE=$(curl -s -X POST "$BASE_URL/tasks" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d '{"name":"bad-type","type":"invalid_type","config_json":{"command":"echo"},"timeout":60}')
assert_json "无效任务类型返回错误" "$BAD_TYPE" "['code']" "40003"

# ==================== 9. 更新测试 ====================
log_info "===== 9. 更新测试 ====="
UP=$(curl -s -X PUT "$BASE_URL/tasks/$T1_ID" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"echo-updated-${TS}\",\"type\":\"command\",\"description\":\"updated task\",\"config_json\":{\"command\":\"echo Updated\"},\"timeout\":120,\"max_retries\":1}")
assert_json "更新任务" "$UP" "['code']" "0"
GU=$(curl -s -X GET "$BASE_URL/tasks/$T1_ID" -H "$AUTH_HEADER")
UN=$(echo "$GU" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['name'])" 2>/dev/null)
assert_eq "任务名称已更新" "echo-updated-${TS}" "$UN"
UT=$(echo "$GU" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['timeout'])" 2>/dev/null)
assert_eq "任务超时已更新" "120" "$UT"

# 更新工作流
UW=$(curl -s -X PUT "$BASE_URL/workflows/$W1_ID" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"wf-echo-updated-${TS}\",\"description\":\"updated workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$T1_ID\"}],\"edges\":[]},\"schedule_strategy\":\"random\"}")
assert_json "更新工作流" "$UW" "['code']" "0"

# ==================== 10. 删除测试 ====================
log_info "===== 10. 删除测试 ====="
DT=$(curl -s -X POST "$BASE_URL/tasks" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d '{"name":"del-task","type":"command","config_json":{"command":"echo"},"timeout":60}')
DT_ID=$(echo "$DT" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
DR=$(curl -s -X DELETE "$BASE_URL/tasks/$DT_ID" -H "$AUTH_HEADER")
assert_json "删除任务" "$DR" "['code']" "0"

# 删除工作流
DW=$(curl -s -X POST "$BASE_URL/workflows" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d '{"name":"del-wf","description":"to delete","dag_json":{"nodes":[{"id":"n1","task_id":"'$T1_ID'"}],"edges":[]},"schedule_strategy":"random"}')
DW_ID=$(echo "$DW" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
DWR=$(curl -s -X DELETE "$BASE_URL/workflows/$DW_ID" -H "$AUTH_HEADER")
assert_json "删除工作流" "$DWR" "['code']" "0"

# ==================== 11. 多参数工作流执行 ====================
log_info "===== 11. 多参数工作流执行 ====="
W7=$(curl -s -X POST "$BASE_URL/workflows" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d "{\"name\":\"wf-multi-${TS}\",\"description\":\"multi param workflow\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$T4_ID\"}],\"edges\":[]},\"schedule_strategy\":\"random\"}")
W7_ID=$(echo "$W7" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['id'])" 2>/dev/null)
assert_json "创建多参数工作流" "$W7" "['code']" "0"

TR7=$(curl -s -X POST "$BASE_URL/workflows/$W7_ID/trigger" -H "Content-Type: application/json" -H "$AUTH_HEADER" \
  -d '{"param_overrides":{"greeting":"Hey","name":"TaskFlow"}}')
I7_ID=$(echo "$TR7" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['instance_id'])" 2>/dev/null)
assert_json "触发多参数工作流" "$TR7" "['code']" "0"
I7=$(wait_instance "$I7_ID" 120)
I7_S=$(echo "$I7" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['status'])" 2>/dev/null || echo "UNKNOWN")
assert_eq "多参数工作流SUCCESS" "SUCCESS" "$I7_S"

# 验证多参数替换
RC7=$(echo "$I7" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for ti in d['data']['task_instances']:
    rc = ti.get('resolved_config')
    if rc and 'command' in rc: print(rc['command'])
" 2>/dev/null || echo "")
if echo "$RC7" | grep -q "Hey TaskFlow"; then log_pass "多参数占位符替换正确"; else log_fail "多参数占位符替换不正确(got: $RC7)"; fi

# ==================== 汇总 ====================
echo ""
echo "=========================================="
echo -e "  集成测试结果: ${GREEN}$PASS 通过${NC}, ${RED}$FAIL 失败${NC}, 共 $TOTAL 项"
echo "=========================================="
[ $FAIL -gt 0 ] && exit 1 || exit 0
