# TaskFlow 部署配置文档

## 1. 系统要求

| 组件 | 最低版本 |
|------|----------|
| Docker | 20.10+ |
| Docker Compose | 2.0+ |
| PostgreSQL | 15+（Docker 部署时自动提供） |

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
| 8080 | Scheduler HTTP | REST API 接口 |
| 50051 | Scheduler gRPC | Worker 通信端口 |
| 5432 | PostgreSQL | 数据库端口 |

> 注意：Docker Compose 部署时，仅暴露 80（前端）、8080（API）、5432（数据库）端口。Worker gRPC 端口（50052）仅在内部网络通信。

## 4. 配置文件说明

### 4.1 Scheduler 配置（scheduler.yaml）

```yaml
server:
  http_port: 8080          # HTTP 监听端口
  grpc_port: 50051         # gRPC 监听端口
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
  sink_type: "file"         # 日志存储后端：file / elasticsearch
  es_url: ""                # Elasticsearch URL（sink_type=elasticsearch 时）
  es_index: "taskflow-logs" # Elasticsearch 索引名
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

Docker Compose 默认启动 2 个 Scheduler 实例，通过 PostgreSQL Advisory Lock 自动选主：

```yaml
scheduler:
  deploy:
    replicas: 2  # 可根据需要调整
```

- 主节点：负责 DAG 驱动、Cron 触发、心跳检测
- 从节点：处理 API 请求
- 主节点故障时从节点自动接管（约 10 秒内）

### 8.2 Worker 扩容

```yaml
worker:
  deploy:
    replicas: 2  # 可根据需要调整
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

## 10. 日志管理

### 10.1 日志路径

| 日志类型 | 路径 |
|----------|------|
| Scheduler 运行日志 | `logs/scheduler.log` |
| Worker 运行日志 | `logs/worker.log` |
| 任务执行日志 | `logs/tasks/{workflow_instance_id}/{task_instance_id}.log` |

### 10.2 日志清理

Worker 自动清理超过 `retention_days`（默认 30 天）的任务日志，每小时检查一次。

### 10.3 ELK 集成

将 `sink_type` 设为 `elasticsearch` 并配置 `es_url`：

```yaml
task_log:
  sink_type: "elasticsearch"
  es_url: "http://elasticsearch:9200"
  es_index: "taskflow-logs"
```

## 11. 备份与恢复

### 11.1 数据库备份

```bash
pg_dump -h localhost -U taskflow taskflow > backup_$(date +%Y%m%d).sql
```

### 11.2 数据库恢复

```bash
psql -h localhost -U taskflow taskflow < backup_20240101.sql
```

## 12. 故障排查

| 问题 | 排查方法 |
|------|----------|
| Scheduler 启动失败 | 检查数据库连接配置和日志 |
| Worker 注册失败 | 检查 Scheduler gRPC 地址是否可达 |
| 工作流不执行 | 检查是否有主节点（选主是否成功） |
| 任务一直 PENDING | 检查是否有在线 Worker |
| 心跳超时 | 检查网络连通性和心跳间隔配置 |
