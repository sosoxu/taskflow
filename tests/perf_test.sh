#!/bin/bash
# TaskFlow 压力测试与性能测试脚本
# 覆盖：
#   - API 响应延迟（GET/POST 单次与并发）
#   - 高并发工作流触发（突发 100 并发）
#   - 批量任务创建（100 个任务）
#   - 持续吞吐测试（30s 内最大触发量）
#   - 大 DAG 工作流（20 节点并行）
#   - 数据库连接池压力（并发查询）
#   - 资源占用监控（CPU/内存）
#   - 错误率与稳定性
set -o pipefail

BASE_URL="http://localhost:8080/api/v1"
RESULTS_DIR="/tmp/perf_results"
mkdir -p "$RESULTS_DIR"

info()  { echo "[INFO] $1"; }
line()  { echo "------------------------------------------------------------"; }

# 登录
LOGIN=$(curl -s "$BASE_URL/auth/login" -H 'Content-Type: application/json' -d '{"username":"admin","password":"admin123"}')
TOKEN=$(echo "$LOGIN" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['access_token'])" 2>/dev/null)
if [ -z "$TOKEN" ]; then echo "CRITICAL: 登录失败"; exit 1; fi
info "登录成功"

AUTH=(-H "Authorization: Bearer $TOKEN")
TS=$(date +%s)

# 准备一个快速任务和一个工作流模板
info "准备测试任务..."
QUICK_TASK=$(curl -s "${AUTH[@]}" -H 'Content-Type: application/json' "$BASE_URL/tasks" -d "{\"name\":\"perf-quick-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo ok\"},\"timeout\":30}")
QUICK_TASK_ID=$(echo "$QUICK_TASK" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)
QUICK_WF=$(curl -s "${AUTH[@]}" -H 'Content-Type: application/json' "$BASE_URL/workflows" -d "{\"name\":\"perf-quick-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${QUICK_TASK_ID}\"}],\"edges\":[]}}")
QUICK_WF_ID=$(echo "$QUICK_WF" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)
info "测试任务/工作流就绪: task=$QUICK_TASK_ID wf=$QUICK_WF_ID"

# 资源监控（后台采样）
MONITOR_PID=""
start_monitor() {
    local out=$1
    : > "$out"
    while true; do
        ps -p $(pgrep -f "build/src/scheduler/scheduler" | head -1) -o %cpu,%rss --no-headers 2>/dev/null >> "$out"
        ps -p $(pgrep -f "build/src/worker/worker" | head -1) -o %cpu,%rss --no-headers 2>/dev/null >> "$out"
        sleep 1
    done
}
stop_monitor() {
    if [ -n "$MONITOR_PID" ]; then kill "$MONITOR_PID" 2>/dev/null; MONITOR_PID=""; fi
}

# 计算监控数据的峰值
peak_stats() {
    local f=$1
    python3 -c "
import sys
vals=[]
try:
    with open('$f') as fp:
        for ln in fp:
            ln=ln.strip()
            if not ln: continue
            parts=ln.split()
            if len(parts)==2:
                try: vals.append((float(parts[0]), int(parts[1])))
                except: pass
except: pass
if not vals:
    print('  (无采样数据)')
else:
    cpu=[v[0] for v in vals]
    rss=[v[1]/1024 for v in vals]  # MB
    print(f'  采样点数: {len(vals)}')
    print(f'  CPU 峰值: {max(cpu):.1f}%  平均: {sum(cpu)/len(cpu):.1f}%')
    print(f'  内存峰值: {max(rss):.0f} MB  平均: {sum(rss)/len(rss):.0f} MB')
"
}

# ============================================================
# 测试 1：API 单次响应延迟
# ============================================================
line; info "测试 1：API 单次响应延迟（10 次取样）"
latency_test() {
    local name=$1 method=$2 url=$3 data=$4
    local total=0 min=999999 max=0 n=10
    for i in $(seq 1 $n); do
        if [ "$method" = "GET" ]; then
            t=$(curl -s -o /dev/null -w "%{time_total}" "${AUTH[@]}" "$BASE_URL$url")
        else
            t=$(curl -s -o /dev/null -w "%{time_total}" "${AUTH[@]}" -H 'Content-Type: application/json' -X "$method" "$BASE_URL$url" -d "$data")
        fi
        ms=$(python3 -c "print(int(float('$t')*1000))")
        total=$((total+ms))
        [ $ms -lt $min ] && min=$ms
        [ $ms -gt $max ] && max=$ms
    done
    avg=$((total/n))
    echo "  $name: 平均=${avg}ms 最小=${min}ms 最大=${max}ms"
    echo "$name,avg=${avg}ms,min=${min}ms,max=${max}ms" >> "$RESULTS_DIR/latency.csv"
}

: > "$RESULTS_DIR/latency.csv"
latency_test "GET /health (无认证)" GET "/health" ""
latency_test "GET /tasks (列表)" GET "/tasks?page=1&page_size=10" ""
latency_test "GET /workflows (列表)" GET "/workflows?page=1&page_size=10" ""
latency_test "GET /instances (列表)" GET "/instances?page=1&page_size=10" ""
latency_test "GET /workers (列表)" GET "/workers" ""
latency_test "GET /dashboard" GET "/dashboard/stats" ""
latency_test "POST /auth/login (登录)" POST "/auth/login" '{"username":"admin","password":"admin123"}'

# ============================================================
# 测试 2：高并发 API 读取（50 并发）
# ============================================================
line; info "测试 2：高并发 API 读取（50 并发 GET /tasks）"
start_monitor "$RESULTS_DIR/mon_concurrent_read.csv" &
MONITOR_PID=$!
T0=$(date +%s.%N)
seq 1 50 | xargs -P 50 -I {} curl -s -o /dev/null -w "%{http_code} %{time_total}\n" "${AUTH[@]}" "$BASE_URL/tasks?page=1&page_size=10" > "$RESULTS_DIR/concurrent_read.csv" 2>&1
T1=$(date +%s.%N)
stop_monitor
DUR=$(python3 -c "print(f'{$T1-$T0:.2f}')")
codes=$(awk '{print $1}' "$RESULTS_DIR/concurrent_read.csv" | sort | uniq -c | tr '\n' ' ')
times=$(awk '{print $2}' "$RESULTS_DIR/concurrent_read.csv" | sort -n)
p50=$(echo "$times" | sed -n '25p')
p95=$(echo "$times" | sed -n '48p')
echo "  总耗时: ${DUR}s  吞吐: $(python3 -c "print(f'{50/$T1-$T0+50/$T0:.1f}')")  req/s"
echo "  状态码分布: $codes"
echo "  P50: $(python3 -c "print(f'{float(\"$p50\")*1000:.0f}ms')")  P95: $(python3 -c "print(f'{float(\"$p95\")*1000:.0f}ms')")"
peak_stats "$RESULTS_DIR/mon_concurrent_read.csv"

# ============================================================
# 测试 3：高并发工作流触发（突发 100 并发）
# ============================================================
line; info "测试 3：高并发工作流触发（100 并发 trigger）"
start_monitor "$RESULTS_DIR/mon_concurrent_trigger.csv" &
MONITOR_PID=$!
T0=$(date +%s.%N)
seq 1 100 | xargs -P 100 -I {} curl -s -o /dev/null -w "%{http_code} %{time_total}\n" "${AUTH[@]}" -H 'Content-Type: application/json' -X POST "$BASE_URL/workflows/${QUICK_WF_ID}/trigger" -d '{}' > "$RESULTS_DIR/concurrent_trigger.csv" 2>&1
T1=$(date +%s.%N)
stop_monitor
DUR=$(python3 -c "print(f'{$T1-$T0:.2f}')")
codes=$(awk '{print $1}' "$RESULTS_DIR/concurrent_trigger.csv" | sort | uniq -c | tr '\n' ' ')
ok_count=$(grep -c "^2" "$RESULTS_DIR/concurrent_trigger.csv" 2>/dev/null || echo 0)
fail_count=$((100 - ok_count))
echo "  总耗时: ${DUR}s  触发吞吐: $(python3 -c "print(f'{100/($T1-$T0):.1f}')")  trigger/s"
echo "  状态码分布: $codes"
echo "  成功: ${ok_count}/100  失败: ${fail_count}/100"
if [ "$fail_count" -gt "0" ]; then
    echo "  [警告] 高并发触发存在失败，错误率 $(python3 -c "print(f'{$fail_count/100*100:.1f}%')")"
fi
peak_stats "$RESULTS_DIR/mon_concurrent_trigger.csv"

# 等待这批实例执行完
info "等待 100 个触发实例执行完成..."
wait_for_instances() {
    local max_wait=$1
    local elapsed=0
    while [ $elapsed -lt $max_wait ]; do
        # 统计活跃实例数
        active=$(curl -s "${AUTH[@]}" "$BASE_URL/instances?page=1&page_size=200" | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    items=d.get('data',{}).get('items',[])
    print(sum(1 for i in items if i.get('status') in ('PENDING','RUNNING','PAUSED')))
except: print(0)
" 2>/dev/null)
        if [ "$active" = "0" ]; then
            echo "  所有实例完成（${elapsed}s）"
            return 0
        fi
        sleep 2
        elapsed=$((elapsed+2))
        [ $((elapsed % 10)) -eq 0 ] && echo "  ... 已等 ${elapsed}s，活跃实例 ${active}"
    done
    echo "  [警告] ${max_wait}s 后仍有 ${active} 个活跃实例"
    return 1
}
wait_for_instances 120

# ============================================================
# 测试 4：批量任务创建（100 个）
# ============================================================
line; info "测试 4：批量任务创建（100 个并发 POST /tasks）"
start_monitor "$RESULTS_DIR/mon_batch_create.csv" &
MONITOR_PID=$!
T0=$(date +%s.%N)
seq 1 100 | xargs -P 20 -I {} curl -s -o /dev/null -w "%{http_code}\n" "${AUTH[@]}" -H 'Content-Type: application/json' -X POST "$BASE_URL/tasks" -d "{\"name\":\"batch-task-${TS}-{}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo batch{}\"},\"timeout\":30}" > "$RESULTS_DIR/batch_create.csv" 2>&1
T1=$(date +%s.%N)
stop_monitor
DUR=$(python3 -c "print(f'{$T1-$T0:.2f}')")
ok_count=$(grep -c "^2" "$RESULTS_DIR/batch_create.csv" 2>/dev/null || echo 0)
echo "  总耗时: ${DUR}s  创建吞吐: $(python3 -c "print(f'{100/($T1-$T0):.1f}')")  task/s"
echo "  成功: ${ok_count}/100"

# ============================================================
# 测试 5：大 DAG 工作流（20 节点并行）
# ============================================================
line; info "测试 5：大 DAG 工作流（20 节点并行）"
# 创建 20 个任务
info "  创建 20 个任务..."
NODES_JSON="["
for i in $(seq 1 20); do
    T=$(curl -s "${AUTH[@]}" -H 'Content-Type: application/json' "$BASE_URL/tasks" -d "{\"name\":\"dag-node-${TS}-${i}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo node${i}\"},\"timeout\":30}")
    TID=$(echo "$T" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)
    [ $i -gt 1 ] && NODES_JSON+=","
    NODES_JSON+="{\"id\":\"n${i}\",\"task_id\":\"${TID}\"}"
done
NODES_JSON+="]"
BIG_WF=$(curl -s "${AUTH[@]}" -H 'Content-Type: application/json' "$BASE_URL/workflows" -d "{\"name\":\"big-dag-wf-${TS}\",\"dag_json\":{\"nodes\":${NODES_JSON},\"edges\":[]}}")
BIG_WF_ID=$(echo "$BIG_WF" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null)
if [ -n "$BIG_WF_ID" ]; then
    info "  触发 20 节点并行工作流..."
    T0=$(date +%s.%N)
    TRIG=$(curl -s "${AUTH[@]}" -H 'Content-Type: application/json' -X POST "$BASE_URL/workflows/${BIG_WF_ID}/trigger" -d '{}')
    INST_ID=$(echo "$TRIG" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['instance_id'])" 2>/dev/null)
    # 等待完成
    for w in $(seq 1 30); do
        sleep 2
        ST=$(curl -s "${AUTH[@]}" "$BASE_URL/instances/${INST_ID}" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['status'])" 2>/dev/null)
        if [ "$ST" = "SUCCESS" ] || [ "$ST" = "FAILED" ]; then break; fi
    done
    T1=$(date +%s.%N)
    DUR=$(python3 -c "print(f'{$T1-$T0:.2f}')")
    echo "  20 节点并行工作流执行: 状态=$ST 耗时=${DUR}s"
    if [ "$ST" = "SUCCESS" ]; then
        echo "  并行度验证: 20 个 echo 任务在 ${DUR}s 内完成（worker max_tasks=10，需 2 批）"
    fi
else
    echo "  [警告] 创建大 DAG 工作流失败"
fi

# ============================================================
# 测试 6：持续吞吐测试（20s 内最大触发量）
# ============================================================
line; info "测试 6：持续吞吐测试（20s 持续触发）"
start_monitor "$RESULTS_DIR/mon_sustained.csv" &
MONITOR_PID=$!
END_TIME=$(( $(date +%s) + 20 ))
COUNT=0
FAILS=0
: > "$RESULTS_DIR/sustained.csv"
( while [ $(date +%s) -lt $END_TIME ]; do
    curl -s -o /dev/null -w "%{http_code}\n" "${AUTH[@]}" -H 'Content-Type: application/json' -X POST "$BASE_URL/workflows/${QUICK_WF_ID}/trigger" -d '{}' >> "$RESULTS_DIR/sustained.csv"
done ) &
SUSTAINED_PID=$!
wait $SUSTAINED_PID
stop_monitor
COUNT=$(wc -l < "$RESULTS_DIR/sustained.csv")
ok_count=$(grep -c "^2" "$RESULTS_DIR/sustained.csv" 2>/dev/null || echo 0)
echo "  20s 内触发: ${COUNT} 次  成功: ${ok_count}  吞吐: $(python3 -c "print(f'{$COUNT/20:.1f}')")  trigger/s"
peak_stats "$RESULTS_DIR/mon_sustained.csv"

# 等待清理
info "等待持续触发的实例执行完成..."
wait_for_instances 180

# ============================================================
# 测试 7：数据库连接池压力（并发分页查询）
# ============================================================
line; info "测试 7：数据库连接池压力（30 并发分页查询 /instances）"
start_monitor "$RESULTS_DIR/mon_dbpool.csv" &
MONITOR_PID=$!
T0=$(date +%s.%N)
seq 1 30 | xargs -P 30 -I {} curl -s -o /dev/null -w "%{http_code} %{time_total}\n" "${AUTH[@]}" "$BASE_URL/instances?page=1&page_size=50" > "$RESULTS_DIR/dbpool.csv" 2>&1
T1=$(date +%s.%N)
stop_monitor
codes=$(awk '{print $1}' "$RESULTS_DIR/dbpool.csv" | sort | uniq -c | tr '\n' ' ')
ok_count=$(grep -c "^2" "$RESULTS_DIR/dbpool.csv" 2>/dev/null || echo 0)
echo "  状态码分布: $codes  成功: ${ok_count}/30"
times=$(awk '{print $2}' "$RESULTS_DIR/dbpool.csv" | sort -n)
p50=$(echo "$times" | sed -n '15p')
p95=$(echo "$times" | sed -n '29p')
echo "  P50: $(python3 -c "print(f'{float(\"$p50\")*1000:.0f}ms')")  P95: $(python3 -c "print(f'{float(\"$p95\")*1000:.0f}ms')")"
peak_stats "$RESULTS_DIR/mon_dbpool.csv"

# ============================================================
# 测试 8：错误率与稳定性（无效请求压力）
# ============================================================
line; info "测试 8：错误率与稳定性（50 并发无效触发）"
seq 1 50 | xargs -P 50 -I {} curl -s -o /dev/null -w "%{http_code}\n" "${AUTH[@]}" -H 'Content-Type: application/json' -X POST "$BASE_URL/workflows/nonexistent-wf-id/trigger" -d '{}' > "$RESULTS_DIR/error_stress.csv" 2>&1
codes=$(sort "$RESULTS_DIR/error_stress.csv" | uniq -c | tr '\n' ' ')
echo "  对不存在工作流触发 50 次，状态码分布: $codes"
# 验证服务仍正常
HEALTH=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/health")
if [ "$HEALTH" = "200" ]; then
    echo "  压力后服务健康检查: 200 (服务稳定)"
else
    echo "  [警告] 压力后健康检查异常: $HEALTH"
fi

# ============================================================
# 汇总
# ============================================================
line
info "测试完成。结果文件位于 $RESULTS_DIR"
echo ""
echo "============================================================"
echo "TaskFlow 压力/性能测试结果汇总"
echo "============================================================"
echo ""
echo "[API 响应延迟]"
cat "$RESULTS_DIR/latency.csv"
echo ""
echo "[并发读取（50并发 GET /tasks）]"
echo "  详见 concurrent_read.csv"
echo ""
echo "[高并发触发（100并发 trigger）]"
echo "  成功: $(grep -c '^2' "$RESULTS_DIR/concurrent_trigger.csv" 2>/dev/null || echo 0)/100"
echo ""
echo "[批量任务创建（100个）]"
echo "  成功: $(grep -c '^2' "$RESULTS_DIR/batch_create.csv" 2>/dev/null || echo 0)/100"
echo ""
echo "[持续吞吐（20s）]"
echo "  总触发: $(wc -l < "$RESULTS_DIR/sustained.csv") 次"
echo ""
echo "[数据库连接池（30并发）]"
echo "  成功: $(grep -c '^2' "$RESULTS_DIR/dbpool.csv" 2>/dev/null || echo 0)/30"
echo ""
echo "[错误压力后服务状态]"
echo "  健康检查: $HEALTH"
echo "============================================================"
