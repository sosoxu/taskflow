#!/bin/bash
# Round 5: 长耗时任务和实时任务测试
set -uo pipefail

BASE="${BASE:-http://localhost:8080}"

# 登录获取 token
TOKEN=$(curl -s -X POST "$BASE/api/v1/auth/login" -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}' | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['access_token'])")
AUTH="Authorization: Bearer $TOKEN"

# Fix #339: 断言框架——此前本脚本只打印不断言且永远 exit 0，
# 且 create_task/create_workflow 使用旧字段名（config/dag）导致
# 所有请求静默失败。现在：断言终态 + 失败非零退出 + 数据清理。
PASS=0
FAIL=0
CREATED_WF_IDS=()
CREATED_TASK_IDS=()

check() {
  local name="$1" expected="$2" actual="$3"
  if [ "$expected" = "$actual" ]; then
    PASS=$((PASS+1)); echo "  [PASS] $name (实际=$actual)"
  else
    FAIL=$((FAIL+1)); echo "  [FAIL] $name 期望=$expected 实际=$actual"
  fi
}

get_status_value() {
  curl -s "$BASE/api/v1/instances/$1" -H "$AUTH" | python3 -c "
import sys,json
try:
    print(json.load(sys.stdin).get('data',{}).get('status','?'))
except Exception:
    print('?')" 2>/dev/null
}

get_retry_count() {
  curl -s "$BASE/api/v1/instances/$1" -H "$AUTH" | python3 -c "
import sys,json
try:
    tis=json.load(sys.stdin).get('data',{}).get('task_instances',[])
    print(tis[0].get('retry_count',0) if tis else '?')
except Exception:
    print('?')" 2>/dev/null
}

wait_status() {
  # 轮询实例状态直到进入期望终态或超时（秒），回显最终状态
  local inst_id=$1 want=$2 timeout_s=${3:-40}
  local st="" waited=0
  while [ "$waited" -lt "$timeout_s" ]; do
    st=$(get_status_value "$inst_id")
    case "$st" in
      SUCCESS|FAILED|CANCELLED|TIMEOUT) break;;
    esac
    sleep 3; waited=$((waited+3))
  done
  echo "$st"
}

echo "=== Token acquired ==="

# 辅助函数
create_task() {
  local name=$1
  local cmd=$2
  local timeout=${3:-3600}
  local max_retries=${4:-0}
  local resp=$(curl -s -X POST "$BASE/api/v1/tasks" -H "$AUTH" -H "Content-Type: application/json" \
    -d "{\"name\":\"$name\",\"type\":\"command\",\"config_json\":{\"command\":\"$cmd\"},\"timeout\":$timeout,\"max_retries\":$max_retries,\"retry_interval\":5}")
  local id
  id=$(echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)
  [ -n "$id" ] && CREATED_TASK_IDS+=("$id")
  echo "$id"
}

create_workflow() {
  local name=$1
  local task_id=$2
  local resp=$(curl -s -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" \
    -d "{\"name\":\"$name\",\"description\":\"test\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$task_id\"}],\"edges\":[]}}")
  local id
  id=$(echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)
  [ -n "$id" ] && CREATED_WF_IDS+=("$id")
  echo "$id"
}

trigger_wf() {
  local wf_id=$1
  curl -s -X POST "$BASE/api/v1/workflows/$wf_id/trigger" -H "$AUTH" -H "Content-Type: application/json" -d '{}' | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['instance_id'])" 2>/dev/null
}

get_instance_status() {
  local inst_id=$1
  curl -s "$BASE/api/v1/instances/$inst_id" -H "$AUTH" | python3 -c "
import sys,json
d=json.load(sys.stdin)
inst = d.get('data',{})
print(f\"workflow_status={inst.get('status','?')}\")
for ti in inst.get('task_instances',[]):
    print(f\"  task: status={ti.get('status','?')} exit_code={ti.get('exit_code','?')} retry_count={ti.get('retry_count',0)} started_at={ti.get('started_at','')} finished_at={ti.get('finished_at','')} error={ti.get('error_message','')[:80]}\")
" 2>/dev/null
}

get_task_instance_id() {
  local inst_id=$1
  curl -s "$BASE/api/v1/instances/$inst_id" -H "$AUTH" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for ti in d.get('data',{}).get('task_instances',[]):
    print(ti['id'])
    break
" 2>/dev/null
}

############################################
# 测试 1: 长耗时任务正常完成（30s）
############################################
echo ""
echo "=== Test 1: 长耗时任务正常完成（30s sleep）==="
TASK_ID=$(create_task "long-task-30s-$(date +%s%N)" "sleep 30 && echo done" 60 0)
echo "Task ID: $TASK_ID"
WF_ID=$(create_workflow "long-wf-30s-$(date +%s)" "$TASK_ID")
echo "Workflow ID: $WF_ID"
INST_ID=$(trigger_wf "$WF_ID")
echo "Instance ID: $INST_ID"

echo "[5s] 状态:"
sleep 5
get_instance_status "$INST_ID"

echo "[35s] 状态（应已完成）:"
sleep 30
get_instance_status "$INST_ID"
FINAL1=$(wait_status "$INST_ID" "SUCCESS" 20)
check "Test1: 长耗时任务（30s）最终成功" "SUCCESS" "$FINAL1"

############################################
# 测试 2: 任务超时处理（timeout=10s，任务 sleep 30s）
############################################
echo ""
echo "=== Test 2: 任务超时处理（timeout=10s, task sleep 30s）==="
TASK_ID=$(create_task "timeout-task-10s-$(date +%s%N)" "sleep 30" 10 0)
echo "Task ID: $TASK_ID (timeout=10s)"
WF_ID=$(create_workflow "timeout-wf-$(date +%s)" "$TASK_ID")
INST_ID=$(trigger_wf "$WF_ID")
echo "Instance ID: $INST_ID"

echo "[5s] 状态（应 RUNNING）:"
sleep 5
get_instance_status "$INST_ID"

echo "[15s] 状态（应 TIMEOUT，超时检测在 drive_interval=2s 周期）:"
sleep 10
get_instance_status "$INST_ID"
wait_status "$INST_ID" "TIMEOUT" 20
# 工作流实例状态枚举无 TIMEOUT（超时任务的工作流终态为 FAILED），
# TIMEOUT 语义体现在任务实例级别——断言任务实例状态
TASK_STATUS2=$(curl -s "$BASE/api/v1/instances/$INST_ID" -H "$AUTH" | python3 -c "
import sys,json
tis=json.load(sys.stdin).get('data',{}).get('task_instances',[])
print(tis[0].get('status','?') if tis else '?')" 2>/dev/null)
check "Test2: 超时任务实例被标记 TIMEOUT" "TIMEOUT" "$TASK_STATUS2"

############################################
# 测试 3: 任务重试（max_retries=2，任务失败）
############################################
echo ""
echo "=== Test 3: 任务重试（max_retries=2, 任务失败）==="
TASK_ID=$(create_task "retry-task-fail-$(date +%s%N)" "exit 1" 30 2)
echo "Task ID: $TASK_ID (max_retries=2, retry_interval=5s)"
WF_ID=$(create_workflow "retry-wf-$(date +%s)" "$TASK_ID")
INST_ID=$(trigger_wf "$WF_ID")
echo "Instance ID: $INST_ID"

echo "[3s] 第一次失败:"
sleep 3
get_instance_status "$INST_ID"

echo "[10s] 第一次重试后:"
sleep 7
get_instance_status "$INST_ID"

echo "[20s] 第二次重试后:"
sleep 10
get_instance_status "$INST_ID"

echo "[30s] 最终状态（应 FAILED，retry_count=2）:"
sleep 10
get_instance_status "$INST_ID"
FINAL3=$(wait_status "$INST_ID" "FAILED" 30)
check "Test3: 重试耗尽后最终 FAILED" "FAILED" "$FINAL3"
check "Test3: 重试次数为 2（max_retries=2）" "2" "$(get_retry_count "$INST_ID")"

############################################
# 测试 4: 任务取消（长耗时任务运行中取消）
############################################
echo ""
echo "=== Test 4: 任务取消（长耗时任务运行中取消）==="
TASK_ID=$(create_task "cancel-task-60s-$(date +%s%N)" "sleep 60" 120 0)
echo "Task ID: $TASK_ID"
WF_ID=$(create_workflow "cancel-wf-$(date +%s)" "$TASK_ID")
INST_ID=$(trigger_wf "$WF_ID")
echo "Instance ID: $INST_ID"

echo "[5s] 状态（应 RUNNING）:"
sleep 5
get_instance_status "$INST_ID"

echo "发起取消..."
CANCEL_RESP=$(curl -s -X POST "$BASE/api/v1/instances/$INST_ID/cancel" -H "$AUTH")
echo "取消响应: $CANCEL_RESP" | head -c 200
echo ""

echo "[3s] 取消后状态:"
sleep 3
get_instance_status "$INST_ID"
FINAL4=$(wait_status "$INST_ID" "CANCELLED" 15)
check "Test4: 运行中取消 → CANCELLED" "CANCELLED" "$FINAL4"

############################################
# 测试 5: 实时日志查看（任务执行中获取日志）
############################################
echo ""
############################################
# 测试 5: 实时日志查看（任务执行中获取日志）
############################################
echo ""
echo "=== Test 5: 实时日志查看（任务执行中获取日志）==="
# 创建一个持续输出日志的任务（20s 内每秒输出一行）
# 注意：command 内不能用双引号（会破坏 JSON 转义导致创建 400）
TASK_ID=$(create_task "log-streaming-task-$(date +%s%N)" "for i in \$(seq 1 20); do echo line-\$i; sleep 1; done" 60 0)
echo "Task ID: $TASK_ID"
WF_ID=$(create_workflow "log-stream-wf-$(date +%s)" "$TASK_ID")
INST_ID=$(trigger_wf "$WF_ID")
echo "Instance ID: $INST_ID"

sleep 5  # 等任务开始并产出若干行日志
TI_ID=$(get_task_instance_id "$INST_ID")
echo "Task Instance ID: $TI_ID"

LOG_LINES=$(curl -s "$BASE/api/v1/instances/$INST_ID/tasks/$TI_ID/logs" -H "$AUTH" | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    log = d.get('data',{}).get('log','') if isinstance(d.get('data'),dict) else ''
    print(len([l for l in log.split('\n') if l.strip()]))
except Exception:
    print(0)" 2>/dev/null)
if [ "${LOG_LINES:-0}" -ge 2 ] 2>/dev/null; then
  PASS=$((PASS+1)); echo "  [PASS] Test5: 执行中可读到实时日志 (行数=$LOG_LINES)"
else
  FAIL=$((FAIL+1)); echo "  [FAIL] Test5: 执行中可读到实时日志 (行数=${LOG_LINES:-0})"
fi

sleep 20  # 等任务自然结束，避免残留进程
FINAL5=$(get_status_value "$INST_ID")
check "Test5: 日志任务最终成功" "SUCCESS" "$FINAL5"

############################################
# 测试 6: Worker 日志文件检查
############################################
echo ""
echo "=== Test 6: Worker 日志文件检查 ==="
# Fix #339: 目录改为环境变量可配置；不可访问时记 SKIP 不计入失败
WORKER_LOG_DIR="${WORKER_LOG_DIR:-/workspace/taskflow/backend/logs/tasks}"
if [ -d "$WORKER_LOG_DIR" ]; then
  echo "日志目录结构:"
  find "$WORKER_LOG_DIR" -name "*.log" 2>/dev/null | head -10
  LATEST_LOG=$(find "$WORKER_LOG_DIR" -name "*.log" -newermt "-10 minutes" 2>/dev/null | head -1)
  if [ -n "$LATEST_LOG" ]; then
    echo "最新日志文件: $LATEST_LOG ($(wc -c < "$LATEST_LOG") bytes)"
  else
    echo "（10 分钟内无新日志文件）"
  fi
else
  echo "[SKIP] 日志目录不可访问: $WORKER_LOG_DIR（可用环境变量 WORKER_LOG_DIR 指定）"
fi

############################################
# 收尾：清理测试数据 + 汇总退出
############################################
echo ""
echo "=== 清理测试数据 ==="
for wf_id in "${CREATED_WF_IDS[@]}"; do
  # Fix #345: 先删实例（工作流删除受"存在执行实例"约束）
  INSTS=$(curl -s "$BASE/api/v1/workflows/$wf_id/instances?page=1&page_size=100" -H "$AUTH" | python3 -c "import sys,json; d=json.load(sys.stdin); print(' '.join(i.get('id','') for i in (d.get('data',{}).get('items') or []) if i.get('id')))" 2>/dev/null)
  for iid in $INSTS; do
    curl -s -o /dev/null -X DELETE "$BASE/api/v1/instances/$iid" -H "$AUTH"
  done
  CODE=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$BASE/api/v1/workflows/$wf_id" -H "$AUTH")
  echo "  删除工作流 $wf_id: HTTP $CODE"
done
for t_id in "${CREATED_TASK_IDS[@]}"; do
  CODE=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$BASE/api/v1/tasks/$t_id" -H "$AUTH")
  echo "  删除任务 $t_id: HTTP $CODE"
done

echo ""
echo "=== Round 5 测试完成: 通过 $PASS，失败 $FAIL ==="
if [ "$FAIL" -gt 0 ]; then
  exit 1
fi
exit 0
