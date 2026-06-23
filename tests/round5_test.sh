#!/bin/bash
# Round 5: 长耗时任务和实时任务测试
set -uo pipefail

BASE="http://localhost:8080"

# 登录获取 token
TOKEN=$(curl -s -X POST "$BASE/api/v1/auth/login" -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}' | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['access_token'])")
AUTH="Authorization: Bearer $TOKEN"

echo "=== Token acquired ==="

# 辅助函数
create_task() {
  local name=$1
  local cmd=$2
  local timeout=${3:-3600}
  local max_retries=${4:-0}
  local resp=$(curl -s -X POST "$BASE/api/v1/tasks" -H "$AUTH" -H "Content-Type: application/json" \
    -d "{\"name\":\"$name\",\"type\":\"command\",\"config\":{\"command\":\"$cmd\"},\"timeout\":$timeout,\"max_retries\":$max_retries,\"retry_interval\":5}")
  echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null
}

create_workflow() {
  local name=$1
  local task_id=$2
  local resp=$(curl -s -X POST "$BASE/api/v1/workflows" -H "$AUTH" -H "Content-Type: application/json" \
    -d "{\"name\":\"$name\",\"description\":\"test\",\"dag\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$task_id\"}],\"edges\":[]}}")
  echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null
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
TASK_ID=$(create_task "long-task-30s" "sleep 30 && echo done" 60 0)
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

############################################
# 测试 2: 任务超时处理（timeout=10s，任务 sleep 30s）
############################################
echo ""
echo "=== Test 2: 任务超时处理（timeout=10s, task sleep 30s）==="
TASK_ID=$(create_task "timeout-task-10s" "sleep 30" 10 0)
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

############################################
# 测试 3: 任务重试（max_retries=2，任务失败）
############################################
echo ""
echo "=== Test 3: 任务重试（max_retries=2, 任务失败）==="
TASK_ID=$(create_task "retry-task-fail" "exit 1" 30 2)
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

############################################
# 测试 4: 任务取消（长耗时任务运行中取消）
############################################
echo ""
echo "=== Test 4: 任务取消（长耗时任务运行中取消）==="
TASK_ID=$(create_task "cancel-task-60s" "sleep 60" 120 0)
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

############################################
# 测试 5: 实时日志查看（任务执行中获取日志）
############################################
echo ""
echo "=== Test 5: 实时日志查看（任务执行中获取日志）==="
# 创建一个持续输出日志的任务
TASK_ID=$(create_task "log-streaming-task" "for i in \$(seq 1 20); do echo \"line \$i\"; sleep 1; done" 60 0)
echo "Task ID: $TASK_ID"
WF_ID=$(create_workflow "log-stream-wf-$(date +%s)" "$TASK_ID")
INST_ID=$(trigger_wf "$WF_ID")
echo "Instance ID: $INST_ID"

# 等待任务开始执行
sleep 3
TI_ID=$(get_task_instance_id "$INST_ID")
echo "Task Instance ID: $TI_ID"

echo "[3s] 任务执行中获取日志（实时性测试）:"
LOG_RESP=$(curl -s "$BASE/api/v1/instances/$INST_ID/tasks/$TI_ID/logs" -H "$AUTH")
echo "日志内容:"
echo "$LOG_RESP" | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    if d.get('code') == 0:
        log = d.get('data',{}).get('log_content','') if isinstance(d.get('data'),dict) else str(d.get('data',''))
        lines = log.split('\n') if log else []
        print(f'  日志行数: {len(lines)}')
        for l in lines[:10]:
            print(f'  {l}')
        if len(lines) > 10:
            print(f'  ... ({len(lines)-10} more)')
    else:
        print(f'  错误: {d}')
except Exception as e:
    print(f'  解析失败: {e}')
    print(sys.stdin.read()[:300])
" 2>/dev/null

echo ""
echo "[8s] 任务继续执行后再次获取日志:"
sleep 5
LOG_RESP=$(curl -s "$BASE/api/v1/instances/$INST_ID/tasks/$TI_ID/logs" -H "$AUTH")
echo "日志内容:"
echo "$LOG_RESP" | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    if d.get('code') == 0:
        log = d.get('data',{}).get('log_content','') if isinstance(d.get('data'),dict) else str(d.get('data',''))
        lines = log.split('\n') if log else []
        print(f'  日志行数: {len(lines)}')
        for l in lines[-5:]:
            print(f'  {l}')
    else:
        print(f'  错误: {d}')
except Exception as e:
    print(f'  解析失败: {e}')
" 2>/dev/null

echo ""
echo "=== SSE 流式日志接口测试 ==="
SSE_RESP=$(curl -s -N "$BASE/api/v1/instances/$INST_ID/tasks/$TI_ID/logs/stream" -H "$AUTH" --max-time 5 2>/dev/null)
echo "SSE 响应（前 500 字符）:"
echo "$SSE_RESP" | head -c 500
echo ""

# 等待任务完成
echo "[等待任务完成...]"
sleep 15
get_instance_status "$INST_ID"

############################################
# 测试 6: 超长耗时任务（验证 started_at 解析）
############################################
echo ""
echo "=== Test 6: 超长耗时任务 started_at 格式验证 ==="
# 创建一个 5s 任务，检查 started_at 的实际格式
TASK_ID=$(create_task "format-check-task" "sleep 5" 30 0)
WF_ID=$(create_workflow "format-check-wf-$(date +%s)" "$TASK_ID")
INST_ID=$(trigger_wf "$WF_ID")
echo "Instance ID: $INST_ID"

sleep 3
echo "[3s] started_at 实际格式:"
curl -s "$BASE/api/v1/instances/$INST_ID" -H "$AUTH" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for ti in d.get('data',{}).get('task_instances',[]):
    sa = ti.get('started_at','')
    print(f'  started_at=\"{sa}\"')
    print(f'  长度={len(sa)}')
    # 检查是否包含毫秒或时区
    if '.' in sa:
        print('  ⚠️ 包含毫秒后缀，std::get_time(\"%Y-%m-%d %H:%M:%S\") 解析会失败！')
    if '+' in sa or sa.endswith('Z'):
        print('  ⚠️ 包含时区后缀，std::get_time(\"%Y-%m-%d %H:%M:%S\") 解析会失败！')
" 2>/dev/null

sleep 5
echo "[8s] 最终状态:"
get_instance_status "$INST_ID"

############################################
# 测试 7: 超时检测是否生效（关键验证）
############################################
echo ""
echo "=== Test 7: 超时检测是否生效（timeout=5s, task sleep 20s）==="
TASK_ID=$(create_task "timeout-verify-5s" "sleep 20" 5 0)
echo "Task ID: $TASK_ID (timeout=5s)"
WF_ID=$(create_workflow "timeout-verify-wf-$(date +%s)" "$TASK_ID")
INST_ID=$(trigger_wf "$WF_ID")
echo "Instance ID: $INST_ID"

echo "[3s] 状态（应 RUNNING）:"
sleep 3
get_instance_status "$INST_ID"

echo "[10s] 状态（若超时检测生效，应 TIMEOUT；若 started_at 解析失败，仍 RUNNING）:"
sleep 7
get_instance_status "$INST_ID"

echo "[15s] 状态:"
sleep 5
get_instance_status "$INST_ID"

############################################
# 测试 8: Worker 日志文件检查
############################################
echo ""
echo "=== Test 8: Worker 日志文件检查 ==="
echo "日志目录结构:"
find /workspace/taskflow/backend/logs/tasks -name "*.log" 2>/dev/null | head -10
echo ""
echo "最新日志文件内容示例:"
LATEST_LOG=$(find /workspace/taskflow/backend/logs/tasks -name "*.log" -newer /tmp/round4_results.log 2>/dev/null | head -1)
if [ -n "$LATEST_LOG" ]; then
  echo "文件: $LATEST_LOG"
  echo "大小: $(wc -c < "$LATEST_LOG") bytes"
  echo "内容（前 10 行）:"
  head -10 "$LATEST_LOG"
else
  echo "未找到新日志文件"
fi

echo ""
echo "=== Round 5 测试完成 ==="
