#!/bin/bash
# TaskFlow 压力测试脚本
# 使用方式: ./tests/stress_test.sh [BASE_URL] [CONCURRENCY]
# 默认 BASE_URL=http://localhost:8080, CONCURRENCY=50

set -euo pipefail

BASE_URL="${1:-http://localhost:8080}"
CONCURRENCY="${2:-50}"
DURATION=60  # 测试持续时间（秒）

echo "========================================"
echo "TaskFlow 压力测试"
echo "BASE_URL: $BASE_URL"
echo "并发数: $CONCURRENCY"
echo "持续时间: ${DURATION}s"
echo "========================================"

# 1. 登录获取 Token
echo "[1/5] 登录获取 Token..."
LOGIN_RESP=$(curl -s -X POST -H "Content-Type: application/json" \
    "$BASE_URL/api/v1/auth/login" \
    -d '{"username":"admin","password":"admin123"}')
TOKEN=$(echo "$LOGIN_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['access_token'])" 2>/dev/null || echo "")

if [ -z "$TOKEN" ]; then
    echo "ERROR: 登录失败，无法获取 Token"
    exit 1
fi
echo "Token 获取成功"

# 2. 创建测试任务
echo "[2/5] 创建测试任务..."
TASK_RESP=$(curl -s -X POST -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    "$BASE_URL/api/v1/tasks" \
    -d '{
        "name":"stress_test_task",
        "type":"command",
        "config":{"command":"echo stress_test"},
        "description":"Stress test task",
        "timeout":30
    }')
TASK_ID=$(echo "$TASK_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null || echo "")

if [ -z "$TASK_ID" ]; then
    echo "ERROR: 创建任务失败"
    exit 1
fi
echo "测试任务创建成功: $TASK_ID"

# 3. 创建测试工作流
echo "[3/5] 创建测试工作流..."
WF_RESP=$(curl -s -X POST -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    "$BASE_URL/api/v1/workflows" \
    -d "{
        \"name\":\"stress_test_workflow\",
        \"description\":\"Stress test workflow\",
        \"dag\":{
            \"nodes\":[
                {\"id\":\"n1\",\"task_id\":\"$TASK_ID\",\"task_name\":\"stress_test_task\",\"task_type\":\"command\"}
            ],
            \"edges\":[]
        },
        \"schedule_strategy\":\"random\"
    }")
WF_ID=$(echo "$WF_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['id'])" 2>/dev/null || echo "")

if [ -z "$WF_ID" ]; then
    echo "ERROR: 创建工作流失败"
    exit 1
fi
echo "测试工作流创建成功: $WF_ID"

# 4. 并发触发工作流
echo "[4/5] 并发触发 $CONCURRENCY 个工作流执行..."

# 记录开始时间
START_TIME=$(date +%s)

# 响应时间记录文件
RESULT_FILE="/tmp/taskflow_stress_results_$$.txt"
rm -f "$RESULT_FILE"

# 并发触发函数
trigger_workflow() {
    local idx=$1
    local start_ms=$(date +%s%N | cut -b1-13)
    local status=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
        -H "Content-Type: application/json" \
        -H "Authorization: Bearer $TOKEN" \
        "$BASE_URL/api/v1/workflows/$WF_ID/trigger" \
        -d '{}')
    local end_ms=$(date +%s%N | cut -b1-13)
    local elapsed=$((end_ms - start_ms))
    echo "$status $elapsed" >> "$RESULT_FILE"
}

# 使用后台进程并发触发
for i in $(seq 1 "$CONCURRENCY"); do
    trigger_workflow "$i" &
done

# 等待所有触发完成
wait

echo "所有触发请求已完成"

# 5. 分析结果
echo "[5/5] 分析测试结果..."

TOTAL=0
SUCCESS=0
FAIL=0
TOTAL_TIME=0
MAX_TIME=0
MIN_TIME=999999

while IFS=' ' read -r status elapsed; do
    TOTAL=$((TOTAL + 1))
    TOTAL_TIME=$((TOTAL_TIME + elapsed))
    if [ "$elapsed" -gt "$MAX_TIME" ]; then
        MAX_TIME=$elapsed
    fi
    if [ "$elapsed" -lt "$MIN_TIME" ]; then
        MIN_TIME=$elapsed
    fi
    if [ "$status" = "200" ] || [ "$status" = "201" ]; then
        SUCCESS=$((SUCCESS + 1))
    else
        FAIL=$((FAIL + 1))
    fi
done < "$RESULT_FILE"

if [ "$TOTAL" -gt 0 ]; then
    AVG_TIME=$((TOTAL_TIME / TOTAL))
else
    AVG_TIME=0
    MIN_TIME=0
fi

# 计算 P99
P99_TIME=$(sort -k2 -n "$RESULT_FILE" | awk '{print $2}' | tail -n $(echo "$TOTAL * 1 / 100" | bc) | head -1)
if [ -z "$P99_TIME" ]; then
    P99_TIME=$MAX_TIME
fi

echo ""
echo "========================================"
echo "压力测试结果"
echo "========================================"
echo "总请求数:     $TOTAL"
echo "成功数:       $SUCCESS"
echo "失败数:       $FAIL"
echo "成功率:       $(echo "scale=2; $SUCCESS * 100 / $TOTAL" | bc 2>/dev/null || echo "N/A")%"
echo "平均响应时间: ${AVG_TIME}ms"
echo "最小响应时间: ${MIN_TIME}ms"
echo "最大响应时间: ${MAX_TIME}ms"
echo "P99 响应时间: ${P99_TIME}ms"
echo "========================================"

# 判断是否通过
if [ "$FAIL" -eq 0 ] && [ "$P99_TIME" -lt 1000 ]; then
    echo "压力测试通过！P99 < 1000ms，无失败请求"
    EXIT_CODE=0
else
    if [ "$FAIL" -gt 0 ]; then
        echo "警告：存在 $FAIL 个失败请求"
    fi
    if [ "$P99_TIME" -ge 1000 ]; then
        echo "警告：P99 响应时间 ${P99_TIME}ms >= 1000ms"
    fi
    EXIT_CODE=1
fi

# 清理
rm -f "$RESULT_FILE"

# 删除测试数据
curl -s -X DELETE -H "Authorization: Bearer $TOKEN" "$BASE_URL/api/v1/workflows/$WF_ID" > /dev/null 2>&1 || true
curl -s -X DELETE -H "Authorization: Bearer $TOKEN" "$BASE_URL/api/v1/tasks/$TASK_ID" > /dev/null 2>&1 || true

exit $EXIT_CODE
