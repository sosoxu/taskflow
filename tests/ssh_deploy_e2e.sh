#!/bin/bash
# SSH 远程部署 worker + 任务派发执行 的端到端测试
#
# 覆盖链路（自己起调度器/数据库/带 sshd 的远程节点，全部用容器）：
#   1. POST /workers/deploy 通过 SSH 在远程节点拉起 worker（6 步全部成功）
#   2. worker 注册上线，地址为自动分配端口
#   3. 建任务 + 建工作流 + 触发，任务真的在远程节点上执行
#   4. 任务日志接口能看到远程节点的输出
#   5. worker 收到 SIGTERM 后优雅注销，节点立即离线
#   6. 重新部署后恢复在线并能继续执行
#   7. 重复部署：先停掉旧 worker 再启动新的（端口自动分配，不残留双进程）
#
# 使用方式:
#   ./tests/ssh_deploy_e2e.sh
# 可选环境变量:
#   SCHEDULER_IMAGE（默认 taskflow-scheduler:latest）
#   WORKER_IMAGE   （默认 taskflow-worker:latest，用于取出 worker 二进制）
#   DEPS_IMAGE     （默认 taskflow-deps:latest，远程节点基础镜像）
#   HTTP_PORT      （默认 18099，调度器 API 映射到宿主机）
#
# 依赖: docker、curl、python3

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCHEDULER_IMAGE="${SCHEDULER_IMAGE:-taskflow-scheduler:latest}"
WORKER_IMAGE="${WORKER_IMAGE:-taskflow-worker:latest}"
DEPS_IMAGE="${DEPS_IMAGE:-taskflow-deps:latest}"
PG_IMAGE="${PG_IMAGE:-postgres:16-alpine}"
HTTP_PORT="${HTTP_PORT:-18099}"

PREFIX="tf-ssh-e2e"
NET="${PREFIX}-net"
NODE="${PREFIX}-node"
SCHED="${PREFIX}-sched"
PG="${PREFIX}-pg"
DB_NAME="tf_ssh_e2e"
DB_USER="taskflow"
DB_PASS="taskflow123"
GRPC_TOKEN="e2e-internal-grpc-token"
SSH_USER="deploy"
SSH_PASS="deploypass"
MARKER="ssh-deploy-e2e-ok"
WORKDIR="$(mktemp -d)"
BASE="http://127.0.0.1:${HTTP_PORT}/api/v1"

PASS=0
FAIL=0
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
log_info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

cleanup() {
    docker rm -f "$SCHED" "$NODE" "$PG" >/dev/null 2>&1 || true
    docker network rm "$NET" >/dev/null 2>&1 || true
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

api_post() {
    local path="$1" body="${2:-}"
    [ -n "$body" ] || body='{}'
    curl -s -X POST -H "Content-Type: application/json" -H "Authorization: Bearer $TOKEN" "$BASE$path" -d "$body"
}
api_get()  { curl -s -H "Authorization: Bearer $TOKEN" "$BASE$1"; }

# 从 JSON 里取字段（stdin）
jget() { python3 -c "import json,sys; d=json.load(sys.stdin); print($1)" 2>/dev/null || true; }

# 等待某个条件成立：wait_for <超时秒> <描述> <命令...>
wait_for() {
    local timeout="$1" desc="$2"; shift 2
    local waited=0
    while [ "$waited" -lt "$timeout" ]; do
        if "$@" >/dev/null 2>&1; then
            return 0
        fi
        sleep 2
        waited=$((waited + 2))
    done
    echo -e "${RED}[FAIL]${NC} 等待超时(${timeout}s): $desc" >&2
    return 1
}

# ---------------------------------------------------------------------------
log_info "准备环境（镜像: $SCHEDULER_IMAGE / $DEPS_IMAGE）"
docker rm -f "$SCHED" "$NODE" "$PG" >/dev/null 2>&1 || true
docker network rm "$NET" >/dev/null 2>&1 || true
docker network create "$NET" >/dev/null

# 远程节点镜像：在 deps 镜像上装 sshd，并放入从 worker 镜像取出的二进制
mkdir -p "$WORKDIR/node"
bin_container="$(docker create "$WORKER_IMAGE")"
docker cp "$bin_container:/app/worker" "$WORKDIR/node/worker" >/dev/null
docker rm "$bin_container" >/dev/null
cat > "$WORKDIR/node/Dockerfile" <<DOCKERFILE
FROM ${DEPS_IMAGE}
RUN apt-get update -qq \\
    && apt-get install -y --no-install-recommends openssh-server >/dev/null \\
    && rm -rf /var/lib/apt/lists/* \\
    && mkdir -p /run/sshd \\
    && ssh-keygen -A >/dev/null \\
    && useradd -m -s /bin/bash ${SSH_USER} \\
    && echo '${SSH_USER}:${SSH_PASS}' | chpasswd \\
    && sed -i 's/^#\\?PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config
COPY worker /opt/taskflow/worker
RUN chmod 755 /opt/taskflow/worker
CMD ["/usr/sbin/sshd", "-D", "-e"]
DOCKERFILE
docker build -q -t "${PREFIX}-node" "$WORKDIR/node" >/dev/null

# 数据库 + schema
docker run -d --name "$PG" --network "$NET" \
    -e POSTGRES_DB="$DB_NAME" -e POSTGRES_USER="$DB_USER" -e POSTGRES_PASSWORD="$DB_PASS" \
    "$PG_IMAGE" >/dev/null
wait_for 60 "postgres 就绪" docker exec "$PG" pg_isready -U "$DB_USER" -d "$DB_NAME" \
    || { log_fail "postgres 未就绪"; exit 1; }
docker exec -i "$PG" psql -U "$DB_USER" -d "$DB_NAME" -q < "$REPO_ROOT/backend/sql/schema_all.sql" >/dev/null 2>&1

# 调度器配置（字段与 scheduler.yaml.example 对齐）
cat > "$WORKDIR/scheduler.yaml" <<YAML
server:
  http_port: 8080
  grpc_port: 50051
  grpc_auth_token: "${GRPC_TOKEN}"
database:
  host: ${PG}
  port: 5432
  name: ${DB_NAME}
  user: ${DB_USER}
  password: "${DB_PASS}"
  min_connections: 2
  max_connections: 10
auth:
  jwt_secret: "e2e-jwt-secret-at-least-32-characters-long"
  access_token_ttl: 86400
  refresh_token_ttl: 604800
encryption:
  aes_key: "TaskFlowE2eAesKey_0123456789abcd"
log:
  level: info
  file_path: logs/scheduler.log
schedule:
  dag_drive_interval: 1
  heartbeat_check_interval: 5
  heartbeat_timeout: 15
  timeout_check_interval: 5
  leader_lease_interval: 5
  advisory_lock_id: 771001
YAML

docker run -d --name "$SCHED" --network "$NET" -p "${HTTP_PORT}:8080" \
    -v "$WORKDIR/scheduler.yaml:/app/scheduler.yaml:ro" "$SCHEDULER_IMAGE" >/dev/null
docker run -d --name "$NODE" --network "$NET" --hostname e2e-node "${PREFIX}-node" >/dev/null

wait_for 60 "调度器健康检查" bash -c "curl -sf $BASE/health >/dev/null" \
    || { log_fail "调度器未就绪"; docker logs --tail 20 "$SCHED"; exit 1; }

# ---------------------------------------------------------------------------
log_info "步骤 1: 登录 + SSH 部署 worker（端口自动分配）"
TOKEN="$(curl -s -X POST -H 'Content-Type: application/json' "$BASE/auth/login" \
    -d "{\"username\":\"admin\",\"password\":\"admin123\"}" | jget 'd["data"]["access_token"]')"
[ -n "$TOKEN" ] && log_pass "登录成功" || { log_fail "登录失败"; exit 1; }

DEPLOY_BODY="{\"name\":\"e2e-node-1\",\"host\":\"$NODE\",\"ssh_port\":22,
  \"ssh_username\":\"$SSH_USER\",\"ssh_password\":\"$SSH_PASS\",
  \"worker_binary_path\":\"/opt/taskflow/worker\",\"remote_dir\":\"/home/$SSH_USER/taskflow-worker\",
  \"scheduler_address\":\"$SCHED:50051\",\"max_tasks\":10,\"resource_tags\":[\"e2e\"],\"log_level\":\"info\"}"

DEPLOY="$(api_post /workers/deploy "$DEPLOY_BODY")"
ok="$(echo "$DEPLOY" | jget 'd["data"]["success"]')"
steps="$(echo "$DEPLOY" | jget 'len([s for s in d["data"]["steps"] if s["success"]])')"
total="$(echo "$DEPLOY" | jget 'len(d["data"]["steps"])')"
if [ "$ok" = "True" ] && [ "$steps" = "$total" ]; then
    log_pass "SSH 部署成功（$steps/$total 步）"
else
    log_fail "SSH 部署失败: $(echo "$DEPLOY" | head -c 400)"
fi
docker exec "$NODE" bash -c "ps -ef | grep -q '[w]orker --config'" \
    && log_pass "远程节点上 worker 进程在运行" || log_fail "远程节点上没有 worker 进程"

# ---------------------------------------------------------------------------
log_info "步骤 2: 节点注册与端口自动分配"
addr_of() { api_get /workers | jget 'next((w["address"] for w in (d["data"].get("items") or d["data"]) if w["name"]=="e2e-node-1"), "")'; }
status_of() { api_get /workers | jget 'next((w["status"] for w in (d["data"].get("items") or d["data"]) if w["name"]=="e2e-node-1"), "")'; }

wait_for 30 "节点上线" bash -c "[ \"\$(curl -s -H 'Authorization: Bearer $TOKEN' $BASE/workers | python3 -c \"import json,sys; d=json.load(sys.stdin)['data']; items=d.get('items') or d; print(next((w['status'] for w in items if w['name']=='e2e-node-1'), ''))\")\" = online ]" \
    || log_fail "节点未在超时内上线"
[ "$(status_of)" = "online" ] && log_pass "节点已上线" || log_fail "节点状态不是 online"

ADDR1="$(addr_of)"
PORT1="${ADDR1##*:}"
if [ -n "$PORT1" ] && [ "$PORT1" -gt 0 ] 2>/dev/null; then
    log_pass "注册地址为自动分配端口: $ADDR1"
else
    log_fail "注册地址异常: '$ADDR1'"
fi

# ---------------------------------------------------------------------------
log_info "步骤 3: 建任务/工作流并触发执行"
TASK_ID="$(api_post /tasks "{\"name\":\"e2e-echo\",\"type\":\"command\",\"description\":\"e2e\",
  \"config\":{\"command\":\"echo $MARKER && hostname && id -un\"},
  \"timeout\":60,\"max_retries\":0,\"retry_interval\":60,\"resource_tags\":[],\"parameters\":{}}" | jget 'd["data"]["id"]')"
[ -n "$TASK_ID" ] && log_pass "任务创建成功" || log_fail "任务创建失败"

WF_ID="$(api_post /workflows "{\"name\":\"e2e-flow\",\"description\":\"e2e\",\"schedule_strategy\":\"random\",
  \"dag_json\":{\"nodes\":[{\"id\":\"n1\",\"task_id\":\"$TASK_ID\",\"x\":0,\"y\":0}],\"edges\":[]}}" | jget 'd["data"]["id"]')"
[ -n "$WF_ID" ] && log_pass "工作流创建成功" || log_fail "工作流创建失败"

TRIGGER_RESP="$(api_post "/workflows/$WF_ID/trigger" '{}')"
INST_ID="$(echo "$TRIGGER_RESP" | jget 'd["data"]["instance_id"]')"
if [ -n "$INST_ID" ]; then
    log_pass "工作流已触发: $INST_ID"
else
    log_fail "触发工作流失败: $(echo "$TRIGGER_RESP" | head -c 300)"
fi

inst_status() { api_get "/instances/$INST_ID" | jget '(d.get("data") or {}).get("status","")'; }
wait_for 60 "实例执行完成" bash -c "[ \"\$(curl -s -H 'Authorization: Bearer $TOKEN' $BASE/instances/$INST_ID | python3 -c \"import json,sys; print((json.load(sys.stdin).get('data') or {}).get('status',''))\")\" = SUCCESS ]" \
    || log_fail "实例未在超时内成功（当前: $(inst_status)）"
[ "$(inst_status)" = "SUCCESS" ] && log_pass "实例执行 SUCCESS" || log_fail "实例状态: $(inst_status)"

TI_ID="$(docker exec "$PG" psql -U "$DB_USER" -d "$DB_NAME" -tAc \
    "select id from task_instances where workflow_instance_id='$INST_ID' limit 1" | tr -d ' ')"
LOGC="$(api_get "/instances/$INST_ID/tasks/$TI_ID/logs" | jget '(d.get("data") or {}).get("log","")')"
echo "$LOGC" | grep -q "$MARKER" && log_pass "任务日志包含命令输出" || log_fail "任务日志缺少 $MARKER: $LOGC"
echo "$LOGC" | grep -q "e2e-node" && log_pass "命令确实在远程节点上执行（hostname=e2e-node）" \
    || log_fail "命令不是在远程节点上执行的: $LOGC"
echo "$LOGC" | grep -q "$SSH_USER" && log_pass "以部署账号 $SSH_USER 执行" || log_fail "执行用户不是 $SSH_USER"

# 远程节点上也应留下任务日志文件
docker exec "$NODE" bash -c "grep -q '$MARKER' /home/$SSH_USER/taskflow-worker/logs/tasks/*/*.log" \
    && log_pass "远程节点本地日志文件存在" || log_fail "远程节点本地日志缺失"

# ---------------------------------------------------------------------------
log_info "步骤 4: 重复部署（替换旧进程，端口自动分配）"
PID1="$(docker exec "$NODE" bash -c "ps -ef | awk '/[w]orker --config/ {print \$2}' | head -1")"
DEPLOY2="$(api_post /workers/deploy "$DEPLOY_BODY")"
STOP_STEP="$(echo "$DEPLOY2" | jget 'next((s["message"] for s in d["data"]["steps"] if s["step"]=="停止旧进程"), "")')"
[ "$(echo "$DEPLOY2" | jget 'd["data"]["success"]')" = "True" ] \
    && log_pass "重复部署成功（停止旧进程: ${STOP_STEP:-无此步骤}）" || log_fail "重复部署失败: $(echo "$DEPLOY2" | head -c 300)"

procs="$(docker exec "$NODE" bash -c "ps -ef | grep -c '[w]orker --config'")"
[ "$procs" = "1" ] && log_pass "远程节点上只剩 1 个 worker 进程" \
    || log_fail "远程节点上有 $procs 个 worker 进程（应替换旧进程）"
if [ -n "$PID1" ] && docker exec "$NODE" bash -c "kill -0 $PID1 2>/dev/null"; then
    log_fail "旧 worker 进程 (pid=$PID1) 仍在运行"
else
    log_pass "旧 worker 进程 (pid=$PID1) 已被停止"
fi
wait_for 30 "重新注册上线" bash -c "[ \"\$(curl -s -H 'Authorization: Bearer $TOKEN' $BASE/workers | python3 -c \"import json,sys; d=json.load(sys.stdin)['data']; items=d.get('items') or d; print(next((w['status'] for w in items if w['name']=='e2e-node-1'), ''))\")\" = online ]" \
    || log_fail "重复部署后节点未上线"

ADDR2="$(addr_of)"
PORT2="${ADDR2##*:}"
if [ -n "$PORT2" ] && [ "$PORT2" -gt 0 ] 2>/dev/null; then
    log_pass "重新部署后注册地址: $ADDR2"
else
    log_fail "注册地址异常: '$ADDR2'"
fi
docker exec "$NODE" bash -c "grep -q 'gRPC 服务启动失败' /home/$SSH_USER/taskflow-worker/logs/worker.log" \
    && log_fail "worker 日志出现 gRPC 启动失败" || log_pass "worker 日志无 gRPC 启动失败"

# ---------------------------------------------------------------------------
log_info "步骤 5: worker 优雅下线与恢复"
docker exec "$NODE" bash -c "pkill -f 'worker --config'" || true
wait_for 30 "节点离线" bash -c "[ \"\$(curl -s -H 'Authorization: Bearer $TOKEN' $BASE/workers | python3 -c \"import json,sys; d=json.load(sys.stdin)['data']; items=d.get('items') or d; print(next((w['status'] for w in items if w['name']=='e2e-node-1'), ''))\")\" = offline ]" \
    || log_fail "节点未在超时内离线"
[ "$(status_of)" = "offline" ] && log_pass "SIGTERM 后节点已离线（优雅注销）" || log_fail "节点仍为 $(status_of)"

api_post /workers/deploy "$DEPLOY_BODY" >/dev/null
wait_for 30 "节点恢复上线" bash -c "[ \"\$(curl -s -H 'Authorization: Bearer $TOKEN' $BASE/workers | python3 -c \"import json,sys; d=json.load(sys.stdin)['data']; items=d.get('items') or d; print(next((w['status'] for w in items if w['name']=='e2e-node-1'), ''))\")\" = online ]" \
    || log_fail "节点未在超时内恢复"
[ "$(status_of)" = "online" ] && log_pass "重新部署后节点恢复在线" || log_fail "恢复失败"

INST2="$(api_post "/workflows/$WF_ID/trigger" '{}' | jget 'd["data"]["instance_id"]')"
wait_for 60 "恢复后再次执行成功" bash -c "[ \"\$(curl -s -H 'Authorization: Bearer $TOKEN' $BASE/instances/$INST2 | python3 -c \"import json,sys; print((json.load(sys.stdin).get('data') or {}).get('status',''))\")\" = SUCCESS ]" \
    || log_fail "恢复后执行未成功"
log_pass "恢复后可继续执行任务"

# ---------------------------------------------------------------------------
echo
echo "=========================================="
echo -e "通过: ${GREEN}${PASS}${NC}  失败: ${RED}${FAIL}${NC}"
echo "=========================================="
[ "$FAIL" -eq 0 ]
