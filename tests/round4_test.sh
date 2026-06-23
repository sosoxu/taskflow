#!/bin/bash
# Round 4: 边界测试 / 压力测试 / 性能测试 (修正版)
# 覆盖之前未测试的边界场景
set -uo pipefail

BASE="http://localhost:8080"
LOG_DIR="/tmp/round4_tests"
mkdir -p "$LOG_DIR"

# 登录获取 token
TOKEN=$(curl -s -X POST "$BASE/api/v1/auth/login" -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}' | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['access_token'])")
AUTH="Authorization: Bearer $TOKEN"

echo "=== Token acquired ==="

# 辅助函数：创建任务并返回 task_id
create_task() {
  local name=$1
  local resp=$(curl -s -X POST "$BASE/api/v1/tasks" -H "$AUTH" -H "Content-Type: application/json" \
    -d "{\"name\":\"$name\",\"type\":\"command\",\"config\":{\"command\":\"echo hi\"}}")
  echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null
}

# 辅助函数：创建工作流并返回 workflow_id
create_workflow() {
  local name=$1
  local task_id=$2
  local resp=$(curl -s -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" \
    -d "{\"name\":\"$name\",\"description\":\"test\",\"dag\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$task_id\"}],\"edges\":[]}}")
  echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null
}

# 先创建一个基础任务供后续测试使用
BASE_TASK_ID=$(create_task "base-task-$(date +%s)")
echo "Base task ID: $BASE_TASK_ID"

if [ -z "$BASE_TASK_ID" ]; then
  echo "FATAL: 无法创建基础任务，退出"
  exit 1
fi

############################################
# 测试 1: 并发创建同名工作流（重复资源边界）
############################################
echo ""
echo "=== Test 1: 并发创建同名工作流 ==="
WF_NAME="dup-wf-$(date +%s)"
PAYLOAD='{"name":"'"$WF_NAME"'","description":"dup test","dag":{"nodes":[{"id":"n1","task_id":"'"$BASE_TASK_ID"'"}],"edges":[]}}'

RESULTS=$(seq 1 20 | xargs -P 20 -I {} curl -s -o /dev/null -w "%{http_code}\n" -X POST "$BASE/api/v1/workflows" \
  -H "$AUTH" -H "Content-Type: application/json" -d "$PAYLOAD")
echo "20 并发创建同名工作流 $WF_NAME 的状态码分布:"
echo "$RESULTS" | sort | uniq -c
echo "（预期：1 个 200，19 个 400 duplicate）"

# 验证只有一个工作流被创建
COUNT=$(curl -s "$BASE/api/v1/workflows?keyword=$WF_NAME" -H "$AUTH" | python3 -c "
import sys,json
d=json.load(sys.stdin)
items = d.get('data',{}).get('items',[])
print(len(items))
" 2>/dev/null)
echo "实际创建数量: $COUNT"

############################################
# 测试 2: 超长 DAG 链（100 节点线性）
############################################
echo ""
echo "=== Test 2: 超长 DAG 链（100 节点线性）==="
python3 -c "
import json
nodes = [{'id': f'n{i}', 'task_id': '$BASE_TASK_ID'} for i in range(100)]
edges = [{'source': f'n{i}', 'target': f'n{i+1}'} for i in range(99)]
print(json.dumps({'name': 'long-dag-test', 'description': '100 node linear', 'dag': {'nodes': nodes, 'edges': edges}}))
" > /tmp/long_dag.json

START=$(date +%s%3N)
RESP=$(curl -s -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d @/tmp/long_dag.json)
END=$(date +%s%3N)
echo "创建 100 节点 DAG 耗时: $((END-START)) ms"
echo "响应: $(echo $RESP | head -c 200)"
WF_ID=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)
echo "Workflow ID: $WF_ID"

if [ -n "$WF_ID" ]; then
  START=$(date +%s%3N)
  TRIG_RESP=$(curl -s -X POST "$BASE/api/v1/workflows/$WF_ID/trigger" -H "$AUTH" -H "Content-Type: application/json" -d '{}')
  END=$(date +%s%3N)
  echo "触发 100 节点 DAG 耗时: $((END-START)) ms"
  echo "触发响应: $(echo $TRIG_RESP | head -c 200)"
fi

############################################
# 测试 3: HTTP 方法误用
############################################
echo ""
echo "=== Test 3: HTTP 方法误用 ==="
echo "PATCH /api/v1/workflows/{id}:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X PATCH "$BASE/api/v1/workflows/$WF_ID" -H "$AUTH" -H "Content-Type: application/json" -d '{}'
echo "DELETE /api/v1/workflows (集合端点不支持 DELETE):"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X DELETE "$BASE/api/v1/workflows" -H "$AUTH"
echo "PUT /api/v1/workflows (应为 POST):"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X PUT "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d '{}'
echo "GET /api/v1/workflows with body:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X GET "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d '{"foo":"bar"}'

############################################
# 测试 4: Content-Type 边界
############################################
echo ""
echo "=== Test 4: Content-Type 边界 ==="
echo "Content-Type: text/plain:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: text/plain" -d '{"name":"x"}'
echo "Content-Type: (空):"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -d '{"name":"x"}'
echo "Content-Type: application/xml:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/xml" -d '{"name":"x"}'
echo "Content-Type: application/json; charset=invalid:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json; charset=invalid" -d '{"name":"x"}'

############################################
# 测试 5: 空 body / null body / 缺失必填字段
############################################
echo ""
echo "=== Test 5: 空 body / null body / 缺失必填字段 ==="
echo "空 body:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json"
echo "null body:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d 'null'
echo "{} body:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d '{}'
echo "缺 name 字段:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d '{"description":"no name"}'
echo "name 为空字符串:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d '{"name":""}'
echo "name 为 null:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d '{"name":null}'

############################################
# 测试 6: 特殊字符 / Unicode / 控制字符
############################################
echo ""
echo "=== Test 6: 特殊字符 / Unicode / 控制字符 ==="
echo "Emoji 名称:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" \
  -d '{"name":"测试🚀工作流","description":"emoji","dag":{"nodes":[{"id":"n1","task_id":"'"$BASE_TASK_ID"'"}],"edges":[]}}' | head -c 200
echo ""
echo "超长名称 (10000 字符):"
LONG_NAME=$(python3 -c "print('a'*10000)")
curl -s -o /dev/null -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" \
  -d '{"name":"'"$LONG_NAME"'"}'
echo "控制字符名称:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" \
  -d '{"name":"test\u0000\u0001\u0002ctrl","dag":{"nodes":[{"id":"n1","task_id":"'"$BASE_TASK_ID"'"}],"edges":[]}}' | head -c 200
echo ""
echo "SQL 注入名称:"
curl -s -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" \
  -d '{"name":"test; DROP TABLE workflows;--","dag":{"nodes":[{"id":"n1","task_id":"'"$BASE_TASK_ID"'"}],"edges":[]}}' | head -c 200
echo ""

############################################
# 测试 7: 无效 UUID / 超长路径参数
############################################
echo ""
echo "=== Test 7: 无效 UUID / 超长路径参数 ==="
echo "非 UUID 路径:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X GET "$BASE/api/v1/workflows/not-a-uuid" -H "$AUTH"
echo "超长路径 (10000 字符):"
LONG_PATH=$(python3 -c "print('a'*10000)")
curl -s -o /dev/null -w "  status=%{http_code}\n" -X GET "$BASE/api/v1/workflows/$LONG_PATH" -H "$AUTH"
echo "路径穿越:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X GET "$BASE/api/v1/workflows/../../../etc/passwd" -H "$AUTH"
echo "UUID 格式但不存在:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X GET "$BASE/api/v1/workflows/00000000-0000-0000-0000-000000000000" -H "$AUTH"

############################################
# 测试 8: 认证边界 - 畸形 token
############################################
echo ""
echo "=== Test 8: 认证边界 - 畸形 token ==="
echo "无 Authorization 头:"
curl -s -o /dev/null -w "  status=%{http_code}\n" "$BASE/api/v1/workflows"
echo "空 Bearer:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -H "Authorization: Bearer " "$BASE/api/v1/workflows"
echo "Basic 而非 Bearer:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -H "Authorization: Basic admin:admin123" "$BASE/api/v1/workflows"
echo "单段 token:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -H "Authorization: Bearer abc" "$BASE/api/v1/workflows"
echo "两段 token:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -H "Authorization: Bearer a.b" "$BASE/api/v1/workflows"
echo "四段 token:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -H "Authorization: Bearer a.b.c.d" "$BASE/api/v1/workflows"
echo "超长 token (100KB):"
LONG_TOKEN=$(python3 -c "print('a'*100000)")
curl -s -o /dev/null -w "  status=%{http_code}\n" -H "Authorization: Bearer $LONG_TOKEN" "$BASE/api/v1/workflows"
echo "超长 token 响应体:"
curl -s -H "Authorization: Bearer $LONG_TOKEN" "$BASE/api/v1/workflows" | head -c 300
echo ""

############################################
# 测试 9: 并发更新用户角色
############################################
echo ""
echo "=== Test 9: 并发更新用户角色 ==="
USER_RESP=$(curl -s -X POST "$BASE/api/v1/users" -H "$AUTH" -H "Content-Type: application/json" \
  -d '{"username":"role_test_'$(date +%s)'","password":"Test12345!","email":"role@test.com","role":"viewer"}')
USER_ID=$(echo "$USER_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)
echo "Created user: $USER_ID"

if [ -n "$USER_ID" ]; then
  echo "20 并发更新同一用户角色 (viewer->operator):"
  RESULTS=$(seq 1 20 | xargs -P 20 -I {} curl -s -o /dev/null -w "%{http_code}\n" -X PUT "$BASE/api/v1/users/$USER_ID/role" \
    -H "$AUTH" -H "Content-Type: application/json" -d '{"role":"operator"}')
  echo "状态码分布:"
  echo "$RESULTS" | sort | uniq -c
  echo "最终角色:"
  curl -s "$BASE/api/v1/users/$USER_ID" -H "$AUTH" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('data',{}).get('role','?'))" 2>/dev/null
fi

############################################
# 测试 10: 并发触发同一工作流 + 立即取消
############################################
echo ""
echo "=== Test 10: 并发触发 + 立即取消 ==="
TC_WF_ID=$(create_workflow "trig-cancel-test-$(date +%s)" "$BASE_TASK_ID")
echo "Created workflow: $TC_WF_ID"

if [ -n "$TC_WF_ID" ]; then
  echo "10 并发触发:"
  seq 1 10 | xargs -P 10 -I {} curl -s -o /dev/null -w "%{http_code}\n" -X POST "$BASE/api/v1/workflows/$TC_WF_ID/trigger" \
    -H "$AUTH" -H "Content-Type: application/json" -d '{}' > /tmp/trig_results.txt
  echo "触发状态码分布:"
  cat /tmp/trig_results.txt | sort | uniq -c

  # 获取实例列表然后取消
  sleep 2
  INSTANCES=$(curl -s "$BASE/api/v1/instances?workflow_id=$TC_WF_ID&page=1&page_size=20" -H "$AUTH" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for inst in d.get('data',{}).get('items',[]):
    print(inst['id'])
" 2>/dev/null)
  echo "找到实例数: $(echo "$INSTANCES" | wc -l)"
  for inst_id in $INSTANCES; do
    curl -s -o /dev/null -w "  cancel $inst_id: %{http_code}\n" -X POST "$BASE/api/v1/instances/$inst_id/cancel" -H "$AUTH"
  done
fi

############################################
# 测试 11: 超大响应体（深度嵌套 DAG 查询）
############################################
echo ""
echo "=== Test 11: 超大响应体（500 节点 DAG）==="
python3 -c "
import json
nodes = [{'id': f'n{i}', 'task_id': '$BASE_TASK_ID'} for i in range(500)]
edges = [{'source': f'n{i}', 'target': f'n{(i+1)%500}'} for i in range(500)]
print(json.dumps({'name': 'huge-dag', 'description': '500 nodes', 'dag': {'nodes': nodes, 'edges': edges}}))
" > /tmp/huge_dag.json

START=$(date +%s%3N)
HUGE_RESP=$(curl -s -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" -d @/tmp/huge_dag.json)
END=$(date +%s%3N)
echo "创建 500 节点 DAG 耗时: $((END-START)) ms"
echo "响应: $(echo $HUGE_RESP | head -c 200)"
HUGE_WF_ID=$(echo "$HUGE_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)

if [ -n "$HUGE_WF_ID" ]; then
  START=$(date +%s%3N)
  GET_RESP=$(curl -s "$BASE/api/v1/workflows/$HUGE_WF_ID" -H "$AUTH")
  END=$(date +%s%3N)
  RESP_SIZE=$(echo "$GET_RESP" | wc -c)
  echo "查询 500 节点 DAG 耗时: $((END-START)) ms, 响应大小: $RESP_SIZE bytes"
fi

############################################
# 测试 12: 并发创建同名任务
############################################
echo ""
echo "=== Test 12: 并发创建同名任务 ==="
TASK_NAME="dup-task-$(date +%s)"
TPAYLOAD='{"name":"'"$TASK_NAME"'","type":"command","config":{"command":"echo hi"}}'
RESULTS=$(seq 1 20 | xargs -P 20 -I {} curl -s -o /dev/null -w "%{http_code}\n" -X POST "$BASE/api/v1/tasks" \
  -H "$AUTH" -H "Content-Type: application/json" -d "$TPAYLOAD")
echo "20 并发创建同名任务 $TASK_NAME 状态码分布:"
echo "$RESULTS" | sort | uniq -c
echo "（预期：1 个 200，19 个 400 duplicate）"

############################################
# 测试 13: 极限并发触发（500 并发）
############################################
echo ""
echo "=== Test 13: 极限并发触发（500 并发）==="
STRESS_WF_ID=$(create_workflow "stress-500-$(date +%s)" "$BASE_TASK_ID")

if [ -n "$STRESS_WF_ID" ]; then
  echo "500 并发触发工作流 $STRESS_WF_ID (分批 100 并发)..."
  START=$(date +%s%3N)
  RESULTS=$(seq 1 500 | xargs -P 100 -I {} curl -s -o /dev/null -w "%{http_code}\n" -X POST "$BASE/api/v1/workflows/$STRESS_WF_ID/trigger" \
    -H "$AUTH" -H "Content-Type: application/json" -d '{}')
  END=$(date +%s%3N)
  echo "总耗时: $((END-START)) ms"
  echo "状态码分布:"
  echo "$RESULTS" | sort | uniq -c
fi

############################################
# 测试 14: 持续 30s 高负载 + 内存监控
############################################
echo ""
echo "=== Test 14: 持续 30s 高负载 + 内存监控 ==="
SCHED_PID=$(pgrep -f "build/src/scheduler/scheduler$" | head -1)
WORKER_PID=$(pgrep -f "build/src/worker/worker$" | head -1)
echo "Scheduler PID: $SCHED_PID, Worker PID: $WORKER_PID"

if [ -n "$SCHED_PID" ]; then
  echo "初始内存:"
  grep VmRSS /proc/$SCHED_PID/status 2>/dev/null | awk '{print "  scheduler: "$2" KB"}'
  [ -n "$WORKER_PID" ] && grep VmRSS /proc/$WORKER_PID/status 2>/dev/null | awk '{print "  worker: "$2" KB"}'

  # 后台持续触发
  ( for i in $(seq 1 300); do
      curl -s -o /dev/null -X POST "$BASE/api/v1/workflows/$STRESS_WF_ID/trigger" -H "$AUTH" -H "Content-Type: application/json" -d '{}' &
    done
    wait
  ) &
  LOAD_PID=$!

  # 每 5s 采样内存
  for i in 1 2 3 4 5 6; do
    sleep 5
    SCHED_MEM=$(grep VmRSS /proc/$SCHED_PID/status 2>/dev/null | awk '{print $2}')
    WORKER_MEM=""
    [ -n "$WORKER_PID" ] && WORKER_MEM=$(grep VmRSS /proc/$WORKER_PID/status 2>/dev/null | awk '{print $2}')
    echo "[$((i*5))s] scheduler: ${SCHED_MEM} KB, worker: ${WORKER_MEM} KB"
  done
  wait $LOAD_PID 2>/dev/null

  echo "最终内存:"
  grep VmRSS /proc/$SCHED_PID/status 2>/dev/null | awk '{print "  scheduler: "$2" KB"}'
  [ -n "$WORKER_PID" ] && grep VmRSS /proc/$WORKER_PID/status 2>/dev/null | awk '{print "  worker: "$2" KB"}'
fi

############################################
# 测试 15: 并发错误密码登录
############################################
echo ""
echo "=== Test 15: 并发错误密码登录（账号锁定边界）==="
RESULTS=$(seq 1 30 | xargs -P 30 -I {} curl -s -o /dev/null -w "%{http_code}\n" -X POST "$BASE/api/v1/auth/login" \
  -H "Content-Type: application/json" -d '{"username":"admin","password":"wrongpassword"}')
echo "30 并发错误密码登录状态码分布:"
echo "$RESULTS" | sort | uniq -c

# 验证正确密码仍能登录
echo "错误密码尝试后，正确密码登录:"
curl -s -o /dev/null -w "  status=%{http_code}\n" -X POST "$BASE/api/v1/auth/login" \
  -H "Content-Type: application/json" -d '{"username":"admin","password":"admin123"}'

############################################
# 测试 16: 并发登录获取 token（性能基线）
############################################
echo ""
echo "=== Test 16: 并发登录性能（50 并发）==="
START=$(date +%s%3N)
RESULTS=$(seq 1 50 | xargs -P 50 -I {} curl -s -o /dev/null -w "%{http_code} %{time_total}\n" -X POST "$BASE/api/v1/auth/login" \
  -H "Content-Type: application/json" -d '{"username":"admin","password":"admin123"}')
END=$(date +%s%3N)
echo "总耗时: $((END-START)) ms"
echo "状态码分布:"
echo "$RESULTS" | awk '{print $1}' | sort | uniq -c
echo "延迟分布 (秒):"
echo "$RESULTS" | awk '{print $2}' | sort -n | awk '
BEGIN{c=0}
{a[c++]=$1}
END{
  if(c==0) exit
  print "  min: " a[0]
  print "  P50: " a[int(c*0.5)]
  print "  P90: " a[int(c*0.9)]
  print "  P99: " a[int(c*0.99)]
  print "  max: " a[c-1]
}'

echo ""
echo "=== Round 4 测试完成 ==="
