# TaskFlow 部署配置文档

## 1. 系统要求

| 组件 | 最低版本 |
|------|----------|
| Docker | 20.10+ |
| Docker Compose | **2.23+**（compose 文件使用 `configs.content` 内嵌配置，旧版 v1/v2 早期版本不支持） |
| PostgreSQL | 15+（Docker 部署时自动提供；compose 默认镜像为 postgres:16-alpine） |

> 统一使用 `docker compose`（v2 插件形式）；独立的 `docker-compose` v1 二进制不支持本文件的配置内嵌语法。

## 2. 快速部署（Docker Compose）

### 2.1 一键启动

```bash
cd taskflow
docker-compose up -d
```

### 2.2 验证服务

```bash
# 检查健康状态
curl http://localhost:8080/api/v1/health

# 访问前端
# 浏览器打开 http://localhost
```

### 2.3 停止服务

```bash
docker-compose down
```

### 2.4 查看日志

```bash
# Scheduler 日志
docker-compose logs scheduler

# Worker 日志
docker-compose logs worker

# PostgreSQL 日志
docker-compose logs postgres
```

## 3. 端口说明

| 端口 | 服务 | 说明 |
|------|------|------|
| 80 | Frontend (Nginx) | 前端 Web 界面 |
| 443 | Frontend (Nginx, TLS) | 启用 TLS overlay 后的 HTTPS 入口（见第 10 节） |
| 8080 | Scheduler HTTP | REST API 接口 |
| 50051 | Scheduler gRPC | Worker 通信端口 |
| 5432 | PostgreSQL | 数据库端口 |

> 注意：Docker Compose 部署时，仅暴露 80（前端）、8080（API）、5432（数据库）端口。Worker gRPC 端口（50052）仅在内部网络通信。
> 启用 TLS overlay（第 10 节）后，只有 80/443 会发布到宿主机，scheduler 与 postgres 端口都会收回内网。

> Fix #331 升级注意：scheduler/worker/frontend 容器现以非 root 用户运行，前端容器内监听 8080（宿主机仍映射 80:8080）。从旧版（root 容器）升级时，日志 named volume 属主为 root 会导致启动失败，需删除日志卷后重建：`docker compose down && docker volume rm <project>_scheduler-logs <project>_worker-logs <project>_worker-task-logs && docker compose up -d`（仅丢失历史日志）。

## 4. 配置文件说明

### 4.1 Scheduler 配置（scheduler.yaml）

```yaml
server:
  http_port: 8080          # HTTP 监听端口
  grpc_port: 50051         # gRPC 监听端口
  # Fix #326: gRPC 内部认证 token。生产部署必须设置为随机值，且与所有
  # worker 的 worker.yaml（server.grpc_auth_token）保持一致，否则 worker
  # 注册/心跳与任务派发会被拒绝。留空仅限本地开发。
  grpc_auth_token: "change-me-to-a-random-token"
  tls:                     # gRPC TLS 配置（可选）
    enabled: false
    cert_path: ""
    key_path: ""
    ca_path: ""

database:
  host: localhost           # PostgreSQL 主机
  port: 5432               # PostgreSQL 端口
  name: taskflow            # 数据库名
  user: taskflow            # 数据库用户
  password: "your-password" # 数据库密码
  min_connections: 5        # 最小连接数
  max_connections: 20       # 最大连接数

auth:
  jwt_secret: "your-jwt-secret-must-be-at-least-32-characters"  # JWT 签名密钥（≥32字符）
  access_token_ttl: 86400   # Access Token 有效期（秒），默认 24 小时
  refresh_token_ttl: 604800 # Refresh Token 有效期（秒），默认 7 天

encryption:
  aes_key: "your-aes-key-must-be-32-characters"  # AES-256 加密密钥（必须 32 字符）

log:
  level: info               # 日志级别：trace/debug/info/warn/error
  file_path: logs/scheduler.log  # 日志文件路径

schedule:
  dag_drive_interval: 2          # DAG 驱动循环间隔（秒）
  heartbeat_check_interval: 10   # 心跳检查间隔（秒）
  heartbeat_timeout: 30          # 心跳超时阈值（秒）
  timeout_check_interval: 10     # 任务超时检查间隔（秒）
  leader_lease_interval: 5       # 选主续约间隔（秒）
```

### 4.2 Worker 配置（worker.yaml）

```yaml
server:
  grpc_port: 50052          # gRPC 监听端口
  # Fix #326: 与 scheduler.yaml 的 server.grpc_auth_token 保持一致。
  # 同时作为访问 scheduler 的注册/心跳凭证与本机 gRPC 服务的校验凭证。
  grpc_auth_token: "change-me-to-a-random-token"
  # Docker 多 Worker 部署使用 "auto"，让每个副本注册自己的容器 IP。
  # 远程主机部署时填写 Scheduler 可访问的 host:port。
  # advertise_address: "auto"
  tls:                     # gRPC TLS 配置（可选）
    enabled: false
    cert_path: ""
    key_path: ""
    ca_path: ""

scheduler:
  address: "localhost:50051"  # Scheduler gRPC 地址
  tls:                       # 连接 Scheduler 的 TLS 配置（可选）
    enabled: false
    cert_path: ""
    key_path: ""
    ca_path: ""

worker:
  name: ""                  # Worker 名称（留空自动生成）
  max_tasks: 10             # 最大并发任务数
  resource_tags: []         # 资源标签列表，如 ["gpu","high-mem"]

log:
  level: info               # 日志级别
  file_path: logs/worker.log  # Worker 运行日志路径

task_log:
  dir: logs/tasks           # 任务日志目录
  retention_days: 30        # 日志保留天数
  # 以下 sink_type/es_url/es_index 为预留配置（当前版本仅实现 file 存储），
  # 配置 elasticsearch 不会生效：
  # sink_type: "file"
  # es_url: ""
  # es_index: "taskflow-logs"
```

## 5. 环境变量

Docker Compose 部署时支持以下环境变量：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `TZ` | `Asia/Shanghai` | 容器时区 |

> 配置主要通过 YAML 文件管理，环境变量仅用于容器时区等基础设置。

## 6. 数据库初始化

### 6.1 Docker Compose 自动初始化

Docker Compose 启动时自动执行 `sql/schema.sql` 初始化数据库。

### 6.2 手动初始化

```bash
psql -h localhost -U taskflow -d taskflow -f sql/schema.sql
```

### 6.3 增量迁移

```bash
psql -h localhost -U taskflow -d taskflow -f sql/migrate_v1.sql
```

## 7. 初始管理员账户

数据库初始化后自动创建管理员账户：

| 字段 | 值 |
|------|-----|
| 用户名 | `admin` |
| 密码 | `admin123` |
| 角色 | `admin` |

> **重要**：首次登录后请立即修改默认密码。

## 8. 多实例部署

### 8.1 调度器高可用

Scheduler 通过 PostgreSQL Advisory Lock 自动选主。**注意：非 Swarm 模式下 `deploy.replicas` 会被忽略**（compose 文件内有注释说明），且 scheduler 绑定固定端口 8080/50051，单机多副本需修改端口映射。生产建议：

- 单机验证：`docker compose up -d --scale worker=2` 只对 worker 有效；scheduler 多实例请用 Docker Swarm 或多台主机部署
- 多实例时为每套环境配置不同的 `schedule.advisory_lock_id`（共用同一 PostgreSQL 时避免互抢主）

- 主节点：负责 DAG 驱动、Cron 触发、心跳检测
- 从节点：处理 API 请求
- 主节点故障时从节点自动接管（约一个续约周期内）

### 8.2 Worker 扩容

```bash
docker compose up -d --scale worker=2
```

Worker 可动态增减，无需重启 Scheduler。新增 Worker 自动注册，停止后 30 秒内被标记离线。

## 9. gRPC TLS 配置

生产环境建议启用 gRPC TLS：

```yaml
# scheduler.yaml
server:
  tls:
    enabled: true
    cert_path: /etc/tls/server.crt
    key_path: /etc/tls/server.key
    ca_path: /etc/tls/ca.crt

# worker.yaml
scheduler:
  tls:
    enabled: true
    cert_path: /etc/tls/client.crt
    key_path: /etc/tls/client.key
    ca_path: /etc/tls/ca.crt
```

需要在 Docker Compose 中挂载证书文件：

```yaml
worker:
  volumes:
    - ./tls:/etc/tls:ro
```

## 10. HTTP TLS 部署（方案 B：nginx 前置终止 TLS）

Scheduler 自身不提供 HTTPS 监听（配置里的 `server.tls` 只作用于 gRPC，见第 9 节）。
生产环境的标准形态是让前端 nginx 终止 TLS，scheduler 只在内网提供明文 HTTP：

```
浏览器 ── HTTPS(443) ──> nginx ── HTTP(内网 8080) ──> scheduler
```

登录密码、JWT、以及 Worker 部署请求里的 SSH 密码因此都在 TLS 通道内传输，
明文端口不出内网。

### 10.1 快速启用（自签证书试跑）

```bash
# 1) 生成自签证书到 ./certs（把 localhost 换成你的域名或 IP）
bash scripts/gen-self-signed-cert.sh localhost

# 2) 自签环境关闭 HSTS 启动，避免浏览器把该主机永久锁定到 HTTPS
TASKFLOW_HSTS_MAX_AGE=0 docker compose -f docker-compose.yml -f docker-compose.tls.yml up -d

# 3) 验证
curl -k https://localhost/api/v1/health     # 期望 200
curl -I http://localhost                    # 期望 301 跳转到 https
```

### 10.2 正式证书

按下面的文件名放进 `certs/`（或用 `TLS_CERT_FILE` / `TLS_KEY_FILE` 指定其它文件名）：

| 文件 | 说明 |
|---|---|
| `certs/server.crt` | 服务器证书（含中间证书链） |
| `certs/server.key` | 私钥 |

`certs/`、`*.pem`、`*.key` 都在 `.gitignore` 中，私钥不会入库。用 Let's Encrypt 时，
`certbot certonly --webroot` 签发后把 `fullchain.pem` / `privkey.pem` 复制或软链到上述路径即可。

### 10.3 安全默认值

| 项 | 值 | 说明 |
|---|---|---|
| TLS 版本 | 1.2 / 1.3 | 1.0、1.1 已禁用 |
| HSTS | `TASKFLOW_HSTS_MAX_AGE`（默认 31536000） | 设 0 关闭；建议证书可信后再开启 |
| 明文入口 | 80 端口 301 跳转 | 不承载业务 |
| scheduler | 8080 / 50051 不发布到宿主机 | 只在 compose 内网可达 |
| postgres | 5432 不发布到宿主机 | 同上 |

overlay 用 `ports: !reset []` 去掉基础 compose 中 scheduler 与 postgres 的端口发布，
用 `!override` 把 frontend 的端口整体换成 80 与 443。需要更严格的绑定（例如
hostNetwork、扁平网络）时，在 `scheduler.yaml` 里指定内网地址：

```yaml
server:
  http_bind_address: "172.18.0.2"   # 默认 0.0.0.0；设为回环/内网地址后明文端口不出本机
```

### 10.4 注意事项

- overlay 依赖 Docker Compose v2.24+（`!reset` / `!override` 语法）。
- 跨主机部署 Worker 时，gRPC 通道同样不要直接暴露，参见第 9 节的 gRPC TLS。
- 负载均衡/反代的健康检查请探 `https://<主机>/api/v1/health`（容器内监听 8443，
  映射到宿主机 443），不要探 80 端口——那里只会返回 301。

## 11. 日志管理

### 10.1 日志路径

| 日志类型 | 路径 |
|----------|------|
| Scheduler 运行日志 | `logs/scheduler.log` |
| Worker 运行日志 | `logs/worker.log` |
| 任务执行日志 | `logs/tasks/{workflow_instance_id}/{task_instance_id}.log` |

### 10.2 日志清理

Worker 自动清理超过 `retention_days`（默认 30 天）的任务日志，每小时检查一次。

### 10.3 ELK 集成（未实现，预留）

`worker.yaml` 中的 `sink_type: elasticsearch` / `es_url` / `es_index` 为**预留配置项，当前版本未实现**——配置后不会生效，任务日志仍写入本地文件（`task_log.dir`）。如需集中式日志，当前可用方案：挂载 worker 日志目录到宿主机后由 Filebeat/Fluentd 采集。

## 12. 备份与恢复

### 11.1 数据库备份

```bash
pg_dump -h localhost -U taskflow taskflow > backup_$(date +%Y%m%d).sql
```

### 11.2 数据库恢复

```bash
psql -h localhost -U taskflow taskflow < backup_20240101.sql
```

## 13. 故障排查

| 问题 | 排查方法 |
|------|----------|
| Scheduler 启动失败 | 检查数据库连接配置和日志 |
| Worker 注册失败 | 检查 Scheduler gRPC 地址是否可达 |
| 工作流不执行 | 检查是否有主节点（选主是否成功） |
| 任务一直 PENDING | 检查是否有在线 Worker |
| 心跳超时 | 检查网络连通性和心跳间隔配置 |
