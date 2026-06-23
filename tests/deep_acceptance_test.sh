#!/bin/bash
# TaskFlow 深入验收测试脚本 v2
# 覆盖：任务重试、Cron调度、SQL任务、超时处理、并发执行、暂停/恢复、SSE日志流、前端实例列表
set -o pipefail

BASE_URL="http://localhost:8080/api/v1"
FRONTEND_DIR="/workspace/taskflow/frontend"
PASS=0
FAIL=0
ISSUES=()

pass_test() { ((PASS++)); echo "[PASS] $1"; }
fail_test() { ((FAIL++)); echo "[FAIL] $1"; ISSUES+=("$1"); }
info() { echo "[INFO] $1"; }

api_get()  { curl -s "$BASE_URL$1" -H "Authorization: Bearer $TOKEN"; }
api_post() { curl -s "$BASE_URL$1" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d "$2"; }
api_put()  { curl -s -X PUT "$BASE_URL$1" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d "$2"; }
api_delete() { curl -s -X DELETE "$BASE_URL$1" -H "Authorization: Bearer $TOKEN"; }

jcode()  { echo "$1" | python3 -c "import sys,json; print(json.load(sys.stdin).get('code',-1))" 2>/dev/null || echo "-1"; }
jfield() { echo "$1" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d$2)" 2>/dev/null || echo ""; }
jcount() { echo "$1" | python3 -c "import sys,json; d=json.load(sys.stdin); items=d.get('data',{}).get('items',d.get('data',[])); print(len(items) if isinstance(items,list) else 0)" 2>/dev/null || echo "0"; }

# 登录获取 admin token
info "===== 登录 ====="
LOGIN=$(curl -s "$BASE_URL/auth/login" -H 'Content-Type: application/json' -d '{"username":"admin","password":"admin123"}')
TOKEN=$(jfield "$LOGIN" "['data']['access_token']")
if [ -z "$TOKEN" ] || [ "$TOKEN" = "None" ]; then
    echo "CRITICAL: admin 登录失败，无法继续测试"; exit 1
fi
info "admin 登录成功"

TS=$(date +%s)

# ============================================================
# 1. 任务重试
# ============================================================
info "===== 1. 任务重试 ====="

# 1.1 创建一个会失败的任务（exit 1）
FAIL_TASK=$(api_post "/tasks" "{\"name\":\"fail-task-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"exit 1\"},\"timeout\":30}")
FAIL_TASK_ID=$(jfield "$FAIL_TASK" "['data']['id']")
if [ "$(jcode "$FAIL_TASK")" = "0" ] && [ -n "$FAIL_TASK_ID" ]; then
    pass_test "创建失败任务成功"
else
    fail_test "创建失败任务失败"; FAIL_TASK_ID=""
fi

# 1.2 创建工作流并触发
if [ -n "$FAIL_TASK_ID" ]; then
    RETRY_WF=$(api_post "/workflows" "{\"name\":\"retry-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${FAIL_TASK_ID}\"}],\"edges\":[]}}")
    RETRY_WF_ID=$(jfield "$RETRY_WF" "['data']['id']")
    TRIGGER=$(api_post "/workflows/${RETRY_WF_ID}/trigger" "{}")
    RI_ID=$(jfield "$TRIGGER" "['data']['instance_id']")
    if [ "$(jcode "$TRIGGER")" = "0" ] && [ -n "$RI_ID" ]; then
        pass_test "触发含失败任务的工作流成功"
    else
        fail_test "触发含失败任务的工作流失败"; RI_ID=""
    fi

    # 1.3 等待执行失败
    if [ -n "$RI_ID" ]; then
        sleep 5
        INSTANCE=$(api_get "/instances/${RI_ID}")
        INST_STATUS=$(jfield "$INSTANCE" "['data']['status']")
        if [ "$INST_STATUS" = "FAILED" ]; then
            pass_test "工作流执行失败状态正确 (FAILED)"
        else
            fail_test "工作流执行状态异常 (status=$INST_STATUS, 期望 FAILED)"
        fi

        # 1.4 获取失败的 task_instance_id
        TI_ID=$(echo "$INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ti=d.get('data',{}).get('task_instances',[]); print(ti[0]['id'] if ti else '')" 2>/dev/null)
        if [ -n "$TI_ID" ]; then
            pass_test "获取失败 task_instance_id 成功"
        else
            fail_test "获取失败 task_instance_id 失败"
        fi

        # 1.5 先修复任务（改为成功命令），再重试
        if [ -n "$TI_ID" ]; then
            UPD=$(api_put "/tasks/${FAIL_TASK_ID}" "{\"name\":\"fail-task-fixed-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo retry-ok\"},\"timeout\":30}")
            # 注意：dag_snapshot 保存了旧任务定义，重试可能仍用旧配置。先测试重试接口本身
            RETRY=$(api_post "/instances/${RI_ID}/tasks/${TI_ID}/retry" "{}")
            RETRY_CODE=$(jcode "$RETRY")
            if [ "$RETRY_CODE" = "0" ]; then
                pass_test "任务重试接口调用成功"
            else
                fail_test "任务重试接口调用失败 (code=$RETRY_CODE)"
            fi

            # 1.6 验证实例状态变为 RUNNING（重试后）
            sleep 2
            INSTANCE2=$(api_get "/instances/${RI_ID}")
            INST_STATUS2=$(jfield "$INSTANCE2" "['data']['status']")
            if [ "$INST_STATUS2" = "RUNNING" ] || [ "$INST_STATUS2" = "PENDING" ]; then
                pass_test "重试后实例状态变为活跃 (status=$INST_STATUS2)"
            else
                fail_test "重试后实例状态未变为活跃 (status=$INST_STATUS2)"
            fi

            # 1.7 等待重试完成
            sleep 5
            INSTANCE3=$(api_get "/instances/${RI_ID}")
            INST_STATUS3=$(jfield "$INSTANCE3" "['data']['status']")
            info "重试后最终状态: $INST_STATUS3"
        fi
    fi
fi

# 1.8 重试不存在的 task_instance
BAD_RETRY=$(api_post "/instances/nonexistent-id/tasks/nonexistent-ti/retry" "{}")
if [ "$(jcode "$BAD_RETRY")" != "0" ]; then
    pass_test "重试不存在的 task_instance 被拒绝"
else
    fail_test "重试不存在的 task_instance 未被拒绝"
fi

# ============================================================
# 2. Cron 定时调度
# ============================================================
info "===== 2. Cron 定时调度 ====="

# 2.1 创建带 cron 的工作流（每分钟执行：0 * * * * *）
CRON_TASK=$(api_post "/tasks" "{\"name\":\"cron-task-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo cron-triggered\"},\"timeout\":30}")
CRON_TASK_ID=$(jfield "$CRON_TASK" "['data']['id']")
CRON_WF=$(api_post "/workflows" "{\"name\":\"cron-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${CRON_TASK_ID}\"}],\"edges\":[]},\"cron_expression\":\"0 * * * * *\",\"cron_enabled\":true}")
CRON_WF_ID=$(jfield "$CRON_WF" "['data']['id']")
if [ "$(jcode "$CRON_WF")" = "0" ] && [ -n "$CRON_WF_ID" ]; then
    pass_test "创建带 cron 的工作流成功"
else
    fail_test "创建带 cron 的工作流失败"; CRON_WF_ID=""
fi

# 2.2 验证工作流 cron 字段已保存
if [ -n "$CRON_WF_ID" ]; then
    GET_CRON=$(api_get "/workflows/${CRON_WF_ID}")
    CRON_EXPR=$(jfield "$GET_CRON" "['data']['cron_expression']")
    CRON_EN=$(jfield "$GET_CRON" "['data']['cron_enabled']")
    if [ "$CRON_EXPR" = "0 * * * * *" ] && [ "$CRON_EN" = "True" ]; then
        pass_test "工作流 cron 字段保存正确 (expr=$CRON_EXPR, enabled=$CRON_EN)"
    else
        fail_test "工作流 cron 字段保存异常 (expr=$CRON_EXPR, enabled=$CRON_EN)"
    fi

    # 2.3 等待 cron 自动触发（最多等 70 秒）
    info "等待 cron 自动触发（最多 70 秒）..."
    CRON_TRIGGERED=0
    for i in $(seq 1 14); do
        sleep 5
        CRON_INSTANCES=$(api_get "/workflows/${CRON_WF_ID}/instances?page=1&page_size=5")
        CRON_COUNT=$(jcount "$CRON_INSTANCES")
        if [ "$CRON_COUNT" -ge "1" ]; then
            # 检查是否有 cron 触发的实例
            TRIGGER_TYPE=$(echo "$CRON_INSTANCES" | python3 -c "import sys,json; d=json.load(sys.stdin); items=d.get('data',{}).get('items',[]); print(items[0].get('trigger_type','') if items else '')" 2>/dev/null)
            if [ "$TRIGGER_TYPE" = "cron" ]; then
                CRON_TRIGGERED=1
                break
            fi
        fi
    done
    if [ "$CRON_TRIGGERED" = "1" ]; then
        pass_test "Cron 自动触发工作流成功 (trigger_type=cron)"
    else
        fail_test "Cron 未在 70 秒内自动触发工作流"
    fi
fi

# 2.4 禁用 cron
if [ -n "$CRON_WF_ID" ]; then
    DIS=$(api_put "/workflows/${CRON_WF_ID}" "{\"name\":\"cron-wf-disabled-${TS}\",\"cron_expression\":\"0 * * * * *\",\"cron_enabled\":false}")
    if [ "$(jcode "$DIS")" = "0" ]; then
        pass_test "禁用 cron 成功"
    else
        fail_test "禁用 cron 失败"
    fi
fi

# ============================================================
# 3. SQL 任务
# ============================================================
info "===== 3. SQL 任务 ====="

# 3.1 创建 SQL 任务（查询 tasks 表）
SQL_TASK=$(api_post "/tasks" "{\"name\":\"sql-task-${TS}\",\"type\":\"sql\",\"config_json\":{\"db_host\":\"127.0.0.1\",\"db_port\":5432,\"db_name\":\"taskflow\",\"db_user\":\"taskflow\",\"db_password\":\"taskflow123\",\"sql_statement\":\"SELECT COUNT(*) AS cnt FROM tasks\"},\"timeout\":30}")
SQL_TASK_ID=$(jfield "$SQL_TASK" "['data']['id']")
if [ "$(jcode "$SQL_TASK")" = "0" ] && [ -n "$SQL_TASK_ID" ]; then
    pass_test "创建 SQL 任务成功"
else
    fail_test "创建 SQL 任务失败"; SQL_TASK_ID=""
fi

# 3.2 创建工作流并触发 SQL 任务
if [ -n "$SQL_TASK_ID" ]; then
    SQL_WF=$(api_post "/workflows" "{\"name\":\"sql-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${SQL_TASK_ID}\"}],\"edges\":[]}}")
    SQL_WF_ID=$(jfield "$SQL_WF" "['data']['id']")
    SQL_TRIGGER=$(api_post "/workflows/${SQL_WF_ID}/trigger" "{}")
    SQL_INST_ID=$(jfield "$SQL_TRIGGER" "['data']['instance_id']")
    if [ "$(jcode "$SQL_TRIGGER")" = "0" ] && [ -n "$SQL_INST_ID" ]; then
        pass_test "触发 SQL 工作流成功"
    else
        fail_test "触发 SQL 工作流失败"; SQL_INST_ID=""
    fi

    # 3.3 等待 SQL 任务执行完成
    if [ -n "$SQL_INST_ID" ]; then
        sleep 6
        SQL_INSTANCE=$(api_get "/instances/${SQL_INST_ID}")
        SQL_STATUS=$(jfield "$SQL_INSTANCE" "['data']['status']")
        if [ "$SQL_STATUS" = "SUCCESS" ]; then
            pass_test "SQL 任务执行成功"
        else
            fail_test "SQL 任务执行状态异常 (status=$SQL_STATUS)"
            # 打印日志帮助诊断
            SQL_TI_ID=$(echo "$SQL_INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ti=d.get('data',{}).get('task_instances',[]); print(ti[0]['id'] if ti else '')" 2>/dev/null)
            if [ -n "$SQL_TI_ID" ]; then
                LOGS=$(api_get "/instances/${SQL_INST_ID}/tasks/${SQL_TI_ID}/logs")
                info "SQL 任务日志: $(echo "$LOGS" | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d.get("data",{}).get("logs","")[:500])' 2>/dev/null)"
            fi
        fi
    fi
fi

# 3.4 创建无效 SQL 任务（语法错误）
BAD_SQL_TASK=$(api_post "/tasks" "{\"name\":\"bad-sql-${TS}\",\"type\":\"sql\",\"config_json\":{\"db_host\":\"127.0.0.1\",\"db_port\":5432,\"db_name\":\"taskflow\",\"db_user\":\"taskflow\",\"db_password\":\"taskflow123\",\"sql_statement\":\"SELECT FROM nonexistent_table\"},\"timeout\":30}")
BAD_SQL_ID=$(jfield "$BAD_SQL_TASK" "['data']['id']")
if [ -n "$BAD_SQL_ID" ]; then
    BAD_SQL_WF=$(api_post "/workflows" "{\"name\":\"bad-sql-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${BAD_SQL_ID}\"}],\"edges\":[]}}")
    BAD_SQL_WF_ID=$(jfield "$BAD_SQL_WF" "['data']['id']")
    BAD_SQL_TRIG=$(api_post "/workflows/${BAD_SQL_WF_ID}/trigger" "{}")
    BAD_SQL_INST=$(jfield "$BAD_SQL_TRIG" "['data']['instance_id']")
    if [ -n "$BAD_SQL_INST" ]; then
        sleep 6
        BAD_SQL_INSTANCE=$(api_get "/instances/${BAD_SQL_INST}")
        BAD_SQL_STATUS=$(jfield "$BAD_SQL_INSTANCE" "['data']['status']")
        if [ "$BAD_SQL_STATUS" = "FAILED" ]; then
            pass_test "无效 SQL 任务执行失败状态正确 (FAILED)"
        else
            fail_test "无效 SQL 任务执行状态异常 (status=$BAD_SQL_STATUS, 期望 FAILED)"
        fi
    fi
fi

# ============================================================
# 4. 超时处理
# ============================================================
info "===== 4. 超时处理 ====="

# 4.1 创建会超时的任务（sleep 10s，timeout=3s）
TIMEOUT_TASK=$(api_post "/tasks" "{\"name\":\"timeout-task-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"sleep 10\"},\"timeout\":3}")
TIMEOUT_TASK_ID=$(jfield "$TIMEOUT_TASK" "['data']['id']")
if [ "$(jcode "$TIMEOUT_TASK")" = "0" ] && [ -n "$TIMEOUT_TASK_ID" ]; then
    pass_test "创建超时任务成功 (timeout=3s, sleep 10s)"
else
    fail_test "创建超时任务失败"; TIMEOUT_TASK_ID=""
fi

# 4.2 触发并验证 TIMEOUT 状态
if [ -n "$TIMEOUT_TASK_ID" ]; then
    TO_WF=$(api_post "/workflows" "{\"name\":\"timeout-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${TIMEOUT_TASK_ID}\"}],\"edges\":[]}}")
    TO_WF_ID=$(jfield "$TO_WF" "['data']['id']")
    TO_TRIG=$(api_post "/workflows/${TO_WF_ID}/trigger" "{}")
    TO_INST_ID=$(jfield "$TO_TRIG" "['data']['instance_id']")
    if [ -n "$TO_INST_ID" ]; then
        pass_test "触发超时工作流成功"
        # 等待超时（3s timeout + 处理时间）
        sleep 8
        TO_INSTANCE=$(api_get "/instances/${TO_INST_ID}")
        TO_STATUS=$(jfield "$TO_INSTANCE" "['data']['status']")
        TO_TI_STATUS=$(echo "$TO_INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ti=d.get('data',{}).get('task_instances',[]); print(ti[0].get('status','') if ti else '')" 2>/dev/null)
        if [ "$TO_TI_STATUS" = "TIMEOUT" ]; then
            pass_test "超时任务 task_instance 状态为 TIMEOUT"
        else
            fail_test "超时任务 task_instance 状态异常 (status=$TO_TI_STATUS, 期望 TIMEOUT)"
        fi
        if [ "$TO_STATUS" = "FAILED" ]; then
            pass_test "超时工作流最终状态为 FAILED"
        else
            fail_test "超时工作流最终状态异常 (status=$TO_STATUS, 期望 FAILED)"
        fi
    fi
fi

# ============================================================
# 5. 并发执行
# ============================================================
info "===== 5. 并发执行 ====="

# 5.1 创建并行工作流（3 个 sleep 2s 任务并行）
CONC_TASK1=$(api_post "/tasks" "{\"name\":\"conc-task1-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"sleep 2\"},\"timeout\":30}")
CONC_TASK1_ID=$(jfield "$CONC_TASK1" "['data']['id']")
CONC_TASK2=$(api_post "/tasks" "{\"name\":\"conc-task2-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"sleep 2\"},\"timeout\":30}")
CONC_TASK2_ID=$(jfield "$CONC_TASK2" "['data']['id']")
CONC_TASK3=$(api_post "/tasks" "{\"name\":\"conc-task3-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"sleep 2\"},\"timeout\":30}")
CONC_TASK3_ID=$(jfield "$CONC_TASK3" "['data']['id']")

if [ -n "$CONC_TASK1_ID" ] && [ -n "$CONC_TASK2_ID" ] && [ -n "$CONC_TASK3_ID" ]; then
    CONC_WF=$(api_post "/workflows" "{\"name\":\"conc-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${CONC_TASK1_ID}\"},{\"id\":\"n2\",\"task_id\":\"${CONC_TASK2_ID}\"},{\"id\":\"n3\",\"task_id\":\"${CONC_TASK3_ID}\"}],\"edges\":[]}}")
    CONC_WF_ID=$(jfield "$CONC_WF" "['data']['id']")
    CONC_TRIG=$(api_post "/workflows/${CONC_WF_ID}/trigger" "{}")
    CONC_INST_ID=$(jfield "$CONC_TRIG" "['data']['instance_id']")
    if [ -n "$CONC_INST_ID" ]; then
        pass_test "触发并行工作流成功 (3 个 sleep 2s 任务)"
        # 并行执行应约 2s 完成；串行需 6s。等待 4s 检查是否完成
        sleep 4
        CONC_INSTANCE=$(api_get "/instances/${CONC_INST_ID}")
        CONC_STATUS=$(jfield "$CONC_INSTANCE" "['data']['status']")
        if [ "$CONC_STATUS" = "SUCCESS" ]; then
            pass_test "并行工作流在 4s 内完成（验证并行执行）"
        else
            fail_test "并行工作流在 4s 内未完成 (status=$CONC_STATUS, 可能未并行执行)"
            # 再等几秒确保最终完成
            sleep 4
        fi
    fi
fi

# ============================================================
# 6. 暂停/恢复
# ============================================================
info "===== 6. 暂停/恢复 ====="

# 6.1 创建一个长串行工作流（n1 -> n2，n1 sleep 8s）
PAUSE_TASK1=$(api_post "/tasks" "{\"name\":\"pause-task1-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"sleep 8\"},\"timeout\":60}")
PAUSE_TASK1_ID=$(jfield "$PAUSE_TASK1" "['data']['id']")
PAUSE_TASK2=$(api_post "/tasks" "{\"name\":\"pause-task2-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo done\"},\"timeout\":30}")
PAUSE_TASK2_ID=$(jfield "$PAUSE_TASK2" "['data']['id']")

if [ -n "$PAUSE_TASK1_ID" ] && [ -n "$PAUSE_TASK2_ID" ]; then
    PAUSE_WF=$(api_post "/workflows" "{\"name\":\"pause-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${PAUSE_TASK1_ID}\"},{\"id\":\"n2\",\"task_id\":\"${PAUSE_TASK2_ID}\"}],\"edges\":[{\"source\":\"n1\",\"target\":\"n2\"}]}}")
    PAUSE_WF_ID=$(jfield "$PAUSE_WF" "['data']['id']")
    PAUSE_TRIG=$(api_post "/workflows/${PAUSE_WF_ID}/trigger" "{}")
    PAUSE_INST_ID=$(jfield "$PAUSE_TRIG" "['data']['instance_id']")
    if [ -n "$PAUSE_INST_ID" ]; then
        pass_test "触发串行工作流成功（用于暂停测试）"
        # 等待 2s 让 n1 开始运行
        sleep 2
        # 6.2 暂停
        PAUSE_RES=$(api_post "/instances/${PAUSE_INST_ID}/pause" "{}")
        if [ "$(jcode "$PAUSE_RES")" = "0" ]; then
            pass_test "暂停工作流实例成功"
        else
            fail_test "暂停工作流实例失败"
        fi
        # 6.3 验证状态为 PAUSED
        sleep 1
        PAUSED_INSTANCE=$(api_get "/instances/${PAUSE_INST_ID}")
        PAUSED_STATUS=$(jfield "$PAUSED_INSTANCE" "['data']['status']")
        if [ "$PAUSED_STATUS" = "PAUSED" ]; then
            pass_test "暂停后实例状态为 PAUSED"
        else
            fail_test "暂停后实例状态异常 (status=$PAUSED_STATUS, 期望 PAUSED)"
        fi
        # 6.4 恢复
        RESUME_RES=$(api_post "/instances/${PAUSE_INST_ID}/resume" "{}")
        if [ "$(jcode "$RESUME_RES")" = "0" ]; then
            pass_test "恢复工作流实例成功"
        else
            fail_test "恢复工作流实例失败"
        fi
        # 6.5 等待最终完成
        sleep 10
        FINAL_INSTANCE=$(api_get "/instances/${PAUSE_INST_ID}")
        FINAL_STATUS=$(jfield "$FINAL_INSTANCE" "['data']['status']")
        if [ "$FINAL_STATUS" = "SUCCESS" ]; then
            pass_test "恢复后工作流最终执行成功"
        else
            fail_test "恢复后工作流最终状态异常 (status=$FINAL_STATUS)"
        fi
    fi
fi

# 6.6 暂停已完成的实例（应失败）
if [ -n "$PAUSE_INST_ID" ]; then
    BAD_PAUSE=$(api_post "/instances/${PAUSE_INST_ID}/pause" "{}")
    if [ "$(jcode "$BAD_PAUSE")" != "0" ]; then
        pass_test "暂停已完成的实例被拒绝"
    else
        fail_test "暂停已完成的实例未被拒绝"
    fi
fi

# ============================================================
# 7. SSE 日志流
# ============================================================
info "===== 7. SSE 日志流 ====="

# 7.1 使用一个已完成的实例获取日志流
if [ -n "$RI_ID" ] && [ -n "$TI_ID" ]; then
    SSE_HTTP=$(curl -s -o /tmp/sse_out.txt -w "%{http_code}" "$BASE_URL/instances/${RI_ID}/tasks/${TI_ID}/logs/stream" -H "Authorization: Bearer $TOKEN" --max-time 3 2>/dev/null)
    if [ "$SSE_HTTP" = "200" ]; then
        pass_test "SSE 日志流端点返回 200"
    else
        fail_test "SSE 日志流端点异常 (HTTP=$SSE_HTTP)"
    fi
    # 7.2 验证 Content-Type 为 text/event-stream
    SSE_CT=$(curl -s -o /dev/null -w "%{content_type}" "$BASE_URL/instances/${RI_ID}/tasks/${TI_ID}/logs/stream" -H "Authorization: Bearer $TOKEN" --max-time 3 2>/dev/null)
    if echo "$SSE_CT" | grep -q "text/event-stream"; then
        pass_test "SSE 日志流 Content-Type 正确 (text/event-stream)"
    else
        fail_test "SSE 日志流 Content-Type 异常 (ct=$SSE_CT)"
    fi
    # 7.3 验证响应包含 data: 前缀（SSE 格式）
    if grep -q "^data:" /tmp/sse_out.txt 2>/dev/null; then
        pass_test "SSE 响应包含 data: 事件前缀"
    else
        fail_test "SSE 响应未包含 data: 事件前缀"
        info "SSE 输出前 200 字符: $(head -c 200 /tmp/sse_out.txt 2>/dev/null)"
    fi
fi

# ============================================================
# 8. 前端实例列表页缺失
# ============================================================
info "===== 8. 前端实例列表页缺失 ====="

# 8.1 后端有 GET /api/v1/instances 端点
INST_LIST=$(api_get "/instances?page=1&page_size=5")
INST_LIST_CODE=$(jcode "$INST_LIST")
INST_LIST_COUNT=$(jcount "$INST_LIST")
if [ "$INST_LIST_CODE" = "0" ] && [ "$INST_LIST_COUNT" -ge "1" ]; then
    pass_test "后端 GET /api/v1/instances 端点正常 (${INST_LIST_COUNT} 个实例)"
else
    fail_test "后端 GET /api/v1/instances 端点异常 (code=$INST_LIST_CODE, count=$INST_LIST_COUNT)"
fi

# 8.2 前端路由是否有实例列表页
if grep -q "instance-list" "$FRONTEND_DIR/src/router/index.ts" 2>/dev/null; then
    pass_test "前端路由包含实例列表页 (instance-list)"
else
    fail_test "前端路由缺失实例列表页 (instance-list) — 后端有 /api/v1/instances 但前端无对应页面"
fi

# 8.3 前端导航菜单是否有实例入口
if grep -qi "instances" "$FRONTEND_DIR/src/components/layout/AppLayout.vue" 2>/dev/null; then
    pass_test "前端导航菜单包含实例入口"
else
    fail_test "前端导航菜单缺失实例入口 — 用户无法从 UI 浏览全量实例列表"
fi

# ============================================================
# 9. 环境变量与工作目录
# ============================================================
info "===== 9. 环境变量与工作目录 ====="

# 9.1 创建带环境变量的任务
ENV_TASK=$(api_post "/tasks" "{\"name\":\"env-task-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo $MY_VAR\"},\"timeout\":30}")
ENV_TASK_ID=$(jfield "$ENV_TASK" "['data']['id']")
# config_json 中 env_vars 和 working_dir 字段
ENV_TASK2=$(api_post "/tasks" "{\"name\":\"env-task2-${TS}\",\"type\":\"command\",\"config_json\":{\"command\":\"echo var=$MY_VAR wd=$(pwd)\",\"env_vars\":{\"MY_VAR\":\"hello-env\"},\"working_dir\":\"/tmp\"},\"timeout\":30}")
ENV_TASK2_ID=$(jfield "$ENV_TASK2" "['data']['id']")
if [ -n "$ENV_TASK2_ID" ]; then
    pass_test "创建带 env_vars 和 working_dir 的任务成功"
    ENV_WF=$(api_post "/workflows" "{\"name\":\"env-wf-${TS}\",\"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"${ENV_TASK2_ID}\"}],\"edges\":[]}}")
    ENV_WF_ID=$(jfield "$ENV_WF" "['data']['id']")
    ENV_TRIG=$(api_post "/workflows/${ENV_WF_ID}/trigger" "{}")
    ENV_INST_ID=$(jfield "$ENV_TRIG" "['data']['instance_id']")
    if [ -n "$ENV_INST_ID" ]; then
        sleep 5
        ENV_INSTANCE=$(api_get "/instances/${ENV_INST_ID}")
        ENV_STATUS=$(jfield "$ENV_INSTANCE" "['data']['status']")
        if [ "$ENV_STATUS" = "SUCCESS" ]; then
            pass_test "带环境变量任务执行成功"
        else
            fail_test "带环境变量任务执行状态异常 (status=$ENV_STATUS)"
        fi
    fi
fi

# ============================================================
# 10. 边界与异常
# ============================================================
info "===== 10. 边界与异常 ====="

# 10.1 重试 SUCCESS 状态的实例（应失败 - 只有 FAILED/CANCELLED/PAUSED 可重试）
# 使用前面成功的 SQL 实例
if [ -n "$SQL_INST_ID" ]; then
    SQL_INSTANCE=$(api_get "/instances/${SQL_INST_ID}")
    SQL_TI_ID=$(echo "$SQL_INSTANCE" | python3 -c "import sys,json; d=json.load(sys.stdin); ti=d.get('data',{}).get('task_instances',[]); print(ti[0]['id'] if ti else '')" 2>/dev/null)
    if [ -n "$SQL_TI_ID" ]; then
        RETRY_SUCCESS=$(api_post "/instances/${SQL_INST_ID}/tasks/${SQL_TI_ID}/retry" "{}")
        if [ "$(jcode "$RETRY_SUCCESS")" != "0" ]; then
            pass_test "重试 SUCCESS 状态的任务被拒绝"
        else
            fail_test "重试 SUCCESS 状态的任务未被拒绝（应只允许 FAILED/TIMEOUT/UPSTREAM_FAILED）"
        fi
    fi
fi

# 10.2 恢复非 PAUSED 实例（应失败）
if [ -n "$SQL_INST_ID" ]; then
    BAD_RESUME=$(api_post "/instances/${SQL_INST_ID}/resume" "{}")
    if [ "$(jcode "$BAD_RESUME")" != "0" ]; then
        pass_test "恢复非 PAUSED 实例被拒绝"
    else
        fail_test "恢复非 PAUSED 实例未被拒绝"
    fi
fi

# 10.3 取消已完成实例（应失败或无副作用）
if [ -n "$SQL_INST_ID" ]; then
    CANCEL_DONE=$(api_post "/instances/${SQL_INST_ID}/cancel" "{}")
    if [ "$(jcode "$CANCEL_DONE")" != "0" ]; then
        pass_test "取消已完成实例被拒绝"
    else
        fail_test "取消已完成实例未被拒绝"
    fi
fi

# ============================================================
# 汇总
# ============================================================
echo ""
echo "============================================================"
echo "深入验收测试汇总"
echo "============================================================"
echo "通过: $PASS"
echo "失败: $FAIL"
if [ "$FAIL" -gt "0" ]; then
    echo ""
    echo "失败项:"
    for issue in "${ISSUES[@]}"; do
        echo "  - $issue"
    done
fi
echo "============================================================"

# 输出失败项到文件供后续 issue 提交使用
if [ "$FAIL" -gt "0" ]; then
    > /tmp/deep_test_issues.txt
    for issue in "${ISSUES[@]}"; do
        echo "$issue" >> /tmp/deep_test_issues.txt
    done
    echo "失败项已保存到 /tmp/deep_test_issues.txt"
fi

exit 0
