# TaskFlow - 分布式任务调度系统

TaskFlow 是一个基于 C++ + Vue 3 构建的分布式任务调度系统，支持通过 Web 界面可视化编排工作流（DAG），将任务以随机、负载均衡或指定节点的方式分发到多个执行节点运行，并提供执行状态追踪、暂停/取消/重试、Cron 定时触发、日志实时跟踪等能力。

---

## 目录

- [功能特性](#功能特性)
- [系统架构](#系统架构)
- [技术栈](#技术栈)
- [项目结构](#项目结构)
- [快速开始](#快速开始)
- [本地开发](#本地开发)
- [离线构建](#离线构建)
- [配置说明](#配置说明)
- [默认账户](#默认账户)
- [测试](#测试)
- [部署](#部署)
- [文档](#文档)
- [许可证](#许可证)

---

## 功能特性

### 任务管理
- **三种任务类型**：Command（命令行）、Script（Shell 脚本）、SQL（数据库执行）
- **任务属性**：超时时间、重试策略（最大重试次数 + 重试间隔）、资源标签、版本号（每次编辑自增）
- **任务操作**：创建 / 编辑（生成新版本）/ 软删除 / 详情查看 / 分页搜索筛选
- **环境变量编辑器**：支持键值对增删改，重命名时校验重复 key

### 工作流编排
- **可视化 DAG 编辑器**：基于 Vue Flow，拖拽任务节点、连线建立依赖关系
- **环检测**：连线时实时检测是否会产生循环依赖
- **节点参数覆盖**：可为工作流中的单个节点覆盖 timeout / max_retries / retry_interval
- **调度策略**：random（随机）、load_balance（负载均衡）、specified（指定 Worker）
- **Cron 定时触发**：支持标准 5 字段 Cron 表达式

### 执行与调度
- **DAG 引擎**：拓扑排序驱动节点执行，上游失败自动标记下游为 UPSTREAM_FAILED
- **任务分发**：Scheduler 通过 gRPC 将任务派发到 Worker，支持资源标签匹配
- **执行控制**：暂停 / 恢复 / 取消 / 任务级重试 / 任务级终止（kill）
- **超时检测**：后台定时扫描超时任务，标记为 TIMEOUT
- **重试机制**：失败任务按配置自动重试

### 高可用与扩展
- **Scheduler 多实例选主**：基于 PostgreSQL Advisory Lock，主节点故障时从节点自动接管
- **Worker 动态扩缩容**：新增 Worker 自动注册，停止后 30 秒内标记离线
- **连接池**：DatabaseManager 管理最小/最大连接数，死连接自动剔除重建

### 安全与权限
- **JWT 认证**：accessToken（24h）+ refreshToken（7d），登出加入 Token 黑名单
- **RBAC 三级角色**：admin（全部权限）、operator（读写）、viewer（只读）
- **资源级权限**：非 admin 用户只能操作自己创建的资源
- **密码加密**：bcrypt $2b$ cost=10
- **敏感字段加密**：SQL 任务数据库密码使用 AES-256 加密存储
- **gRPC TLS**：支持双向 TLS 配置

### 可观测性
- **仪表盘**：任务/工作流总数、运行中实例、在线 Worker、今日执行数、成功率、最近实例
- **DAG 可视化**：实例详情页按节点状态着色（PENDING 灰 / RUNNING 蓝 / SUCCESS 绿 / FAILED 红 / UPSTREAM_FAILED 橙）
- **日志实时跟踪**：基于 SSE（Server-Sent Events）的日志流，支持自动重连（2s/4s/6s，最多 3 次）
- **轮询刷新**：运行中实例自动 5 秒轮询，标签页隐藏时暂停
- **日志轮转**：rotating_file_sink（100MB/文件，10 个文件）

---

## 系统架构

```
┌──────────────┐      REST/JSON       ┌──────────────────┐      gRPC/Protobuf      ┌──────────────────┐
│              │                      │                  │                         │                  │
│  Vue 3 前端   │ ◄──────────────────► │  调度器集群        │ ◄─────────────────────► │  执行节点集群      │
│  (Nginx)     │                      │  (Scheduler)     │                         │  (Worker)        │
└──────────────┘                      └────────┬─────────┘                         └──────────────────┘
                                               │
                                      ┌────────▼─────────┐
                                      │                  │
                                      │   PostgreSQL     │
                                      │                  │
                                      └──────────────────┘
```

| 组件 | 职责 | 部署方式 |
|------|------|----------|
| **前端 (Web UI)** | 用户交互、工作流可视化编排、执行监控 | Nginx 托管静态资源 |
| **调度器 (Scheduler)** | REST API、DAG 解析与执行、Cron 触发、任务分发、选主 | 多实例，PostgreSQL Advisory Lock 选主 |
| **执行节点 (Worker)** | 接收并执行任务、上报状态与心跳 | 多实例分布式部署 |
| **PostgreSQL** | 任务/工作流/实例/用户/Cron 持久化 | 独立部署 |

| 通道 | 协议 | 说明 |
|------|------|------|
| 前端 ↔ 调度器 | REST (JSON) | 基于 Drogon HTTP 框架 |
| 调度器 ↔ Worker | gRPC (Protobuf) | 高性能内部通信，GetTaskLog 使用 server-streaming |
| 调度器 ↔ PostgreSQL | libpqxx | 连接池管理 |

---

## 技术栈

### 后端（C++17）
- **HTTP 框架**：[Drogon](https://github.com/drogonframework/drogon) v1.8.7
- **RPC 框架**：[gRPC](https://grpc.io/) v1.65.5 + Protobuf v28.3
- **数据库驱动**：libpqxx（PostgreSQL 连接池）
- **日志**：[spdlog](https://github.com/gabime/spdlog) v1.14.1（rotating file sink）
- **JSON**：[nlohmann/json](https://github.com/nlohmann/json) v3.11.3
- **配置**：[yaml-cpp](https://github.com/jbeder/yaml-cpp) 0.8.0
- **认证**：[jwt-cpp](https://github.com/Thalhammer/jwt-cpp) v0.7.0
- **加密**：OpenSSL（AES-256）、bcrypt
- **构建**：CMake 3.20+、ccache
- **测试**：[Catch2](https://github.com/catchorg/Catch2) v3.5.2

### 前端
- **框架**：Vue 3.5 + TypeScript 6
- **构建**：Vite 8
- **UI 库**：Element Plus 2.14
- **状态管理**：Pinia 3
- **路由**：Vue Router 4
- **DAG 编辑器**：[Vue Flow](https://vueflow.dev/) 1.48
- **代码编辑器**：CodeMirror 6（SQL / JavaScript 语法高亮）
- **HTTP 客户端**：Axios

### 基础设施
- **数据库**：PostgreSQL 15+
- **容器化**：Docker + Docker Compose
- **Web 服务器**：Nginx（前端托管 + `/api/` 反向代理）

---

## 项目结构

```
taskflow/
├── backend/                      # 后端 C++ 项目
│   ├── CMakeLists.txt            # 顶层 CMake 配置
│   ├── Makefile                  # 快捷构建脚本 (make scheduler/worker/test)
│   ├── build-deps.sh             # 依赖库本地编译安装脚本
│   ├── scheduler.yaml.example    # Scheduler 配置示例
│   ├── worker.yaml.example       # Worker 配置示例
│   ├── sql/
│   │   ├── schema.sql            # 数据库初始化 schema
│   │   └── migrate_v1.sql        # 增量迁移脚本
│   ├── src/
│   │   ├── common/               # 公共库（模型/工具/数据库/配置）
│   │   │   ├── config/           # SchedulerConfig / WorkerConfig
│   │   │   ├── database/         # DatabaseManager 连接池
│   │   │   ├── models/           # 数据模型 (Task/Workflow/Instance/User...)
│   │   │   ├── result/           # Result<T> 错误处理模板
│   │   │   └── util/             # JWT/加密/UUID/密码工具
│   │   ├── scheduler/            # 调度器
│   │   │   ├── api/              # HTTP Controller (REST API)
│   │   │   ├── service/          # 业务逻辑层
│   │   │   ├── dao/              # 数据访问层
│   │   │   ├── engine/           # DAG 引擎 / Cron 调度器
│   │   │   ├── grpc/             # gRPC 服务 / 心跳检测 / 选主
│   │   │   ├── middleware/       # 认证 / 角色 / CORS 中间件
│   │   │   └── main.cpp
│   │   ├── worker/               # 执行节点
│   │   │   ├── executor/         # Command/Script/SQL 执行器 + 日志 sink
│   │   │   ├── grpc/             # Worker gRPC 客户端
│   │   │   └── main.cpp
│   │   └── proto/                # Protobuf 定义 (taskflow.proto)
│   ├── tests/                    # Catch2 单元测试 + 集成测试
│   ├── Dockerfile.deps           # 依赖基础镜像
│   ├── Dockerfile.scheduler      # Scheduler 镜像
│   └── Dockerfile.worker         # Worker 镜像
├── frontend/                     # 前端 Vue 3 项目
│   ├── src/
│   │   ├── api/                  # Axios API 封装
│   │   ├── views/                # 页面 (Dashboard/Task/Workflow/Instance/Worker/User)
│   │   ├── components/layout/    # AppLayout 布局
│   │   ├── stores/               # Pinia 状态管理
│   │   ├── router/               # Vue Router 路由
│   │   ├── types/                # TypeScript 类型定义
│   │   └── utils/                # 工具函数 (request/format/auth)
│   ├── Dockerfile                # 前端镜像 (Node 构建 + Nginx 运行)
│   ├── nginx.conf                # Nginx 配置
│   └── package.json
├── tests/                        # 端到端 / 压力测试脚本
├── docker-compose.yml            # 一键部署编排
├── start.sh                      # 启动管理脚本
├── DEPLOYMENT.md                 # 部署配置文档
└── README.md                     # 本文档
```

---

## 快速开始

### 方式一：Docker Compose 一键部署（推荐）

**前置要求**：Docker 20.10+、Docker Compose 2.0+

```bash
cd taskflow

# 一键启动（自动构建依赖镜像）
./start.sh start

# 或直接使用 docker compose
docker compose up -d
```

启动后访问：

| 服务 | 地址 |
|------|------|
| 前端 Web UI | http://localhost |
| REST API | http://localhost:8080 |
| gRPC | localhost:50051 |
| PostgreSQL | localhost:5432 |

**默认管理员账户**：`admin` / `admin123`（首次登录后请立即修改密码）

### 方式二：多 Worker 扩容

```bash
# 启动 3 个 Worker 实例
./start.sh scale 3

# 或
docker compose up -d --scale worker=3
```

### 常用管理命令

```bash
./start.sh status    # 查看服务状态
./start.sh logs      # 查看所有服务日志
./start.sh logs scheduler   # 查看 Scheduler 日志
./start.sh restart   # 重启所有服务
./start.sh stop      # 停止所有服务
./start.sh clean     # 清理所有容器和数据卷（慎用）
```

---

## 本地开发

### 后端构建

**前置依赖**：gcc 12+、CMake 3.20+、PostgreSQL 15+ 开发库

#### 安装依赖

在没有系统包的环境中，使用 `build-deps.sh` 一键编译安装所有 C++ 依赖：

```bash
cd backend
./build-deps.sh /usr/local
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

#### 构建与运行

```bash
cd backend

# 增量构建 scheduler 和 worker
make

# 或单独构建
make scheduler
make worker

# 构建并运行 scheduler
make run

# 构建并运行测试
make test
```

#### 手动 CMake 构建

```bash
cd backend
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# 运行 scheduler
./src/scheduler/scheduler --config ../scheduler.yaml

# 运行 worker
./src/worker/worker --config ../worker.yaml
```

### 前端构建

**前置要求**：Node.js 20.19+ 或 22.12+

```bash
cd frontend

# 安装依赖
npm install

# 开发模式（热重载）
npm run dev

# 类型检查 + 生产构建
npm run build

# 预览构建产物
npm run preview
```

开发模式下默认监听 `http://localhost:5173`，API 请求通过 Vite 代理转发到 Scheduler（默认 `http://localhost:8080`）。

---

## 离线构建

在无网络环境中从源码编译后端服务，所有依赖均从本地 tarball 构建。

### 1. 下载依赖（有网络的机器上）

```bash
cd backend
bash download-thirdparty.sh
```

脚本会下载 17 个 tarball 到 `thirdparty/` 目录，包括 gRPC 及其子模块（abseil-cpp、protobuf、boringssl、re2 等）。

### 2. 离线环境构建

离线环境需预装：CMake 3.20+、C++17 编译器、OpenSSL 开发库、PostgreSQL 开发库。

```bash
mkdir build && cd build

# 方式 A：gRPC 从源码编译（离线环境无需预装 gRPC/Protobuf）
cmake -DOFFLINE_BUILD=ON -DGRPC_FROM_SOURCE=ON -DBUILD_TESTS=OFF ..
make -j$(nproc)

# 方式 B：使用系统预装的 gRPC/Protobuf
cmake -DOFFLINE_BUILD=ON -DBUILD_TESTS=OFF ..
make -j$(nproc)
```

### 3. 安装到指定目录

```bash
cmake --install . --prefix /opt/taskflow --component taskflow
```

安装目录结构：

```
/opt/taskflow/
├── bin/scheduler
├── bin/worker
├── etc/scheduler.yaml.example
├── etc/worker.yaml.example
└── sql/
    ├── schema.sql
    └── migrate_v1.sql
```

### 数据库初始化

```bash
# 创建数据库
createdb taskflow

# 初始化 schema
psql -h localhost -U taskflow -d taskflow -f backend/sql/schema.sql

# 增量迁移（如需要）
psql -h localhost -U taskflow -d taskflow -f backend/sql/migrate_v1.sql
```

---

## 配置说明

### Scheduler 配置（scheduler.yaml）

```yaml
server:
  http_port: 8080          # HTTP 监听端口
  grpc_port: 50051         # gRPC 监听端口
  tls:                     # gRPC TLS 配置（可选）
    enabled: false

database:
  host: localhost
  port: 5432
  name: taskflow
  user: taskflow
  password: "your-password"
  min_connections: 5
  max_connections: 20

auth:
  jwt_secret: "your-jwt-secret-must-be-at-least-32-characters"  # ≥32 字符
  access_token_ttl: 86400   # 24 小时
  refresh_token_ttl: 604800 # 7 天

encryption:
  aes_key: "your-aes-key-must-be-32-characters"  # 必须 32 字符

log:
  level: info
  file_path: logs/scheduler.log

schedule:
  dag_drive_interval: 2          # DAG 驱动循环间隔（秒）
  heartbeat_check_interval: 10   # 心跳检查间隔（秒）
  heartbeat_timeout: 30          # 心跳超时阈值（秒）
  timeout_check_interval: 10     # 任务超时检查间隔（秒）
  leader_lease_interval: 5       # 选主续约间隔（秒）
```

### Worker 配置（worker.yaml）

```yaml
server:
  grpc_port: 50052
  advertise_address: "worker:50052"  # 对外可达地址（Docker 中用服务名）

scheduler:
  address: "localhost:50051"

worker:
  name: ""                  # 留空自动生成
  max_tasks: 10             # 最大并发任务数
  resource_tags: []         # 资源标签，如 ["gpu","high-mem"]

log:
  level: info
  file_path: logs/worker.log

task_log:
  dir: logs/tasks           # 任务日志目录
  retention_days: 30        # 日志保留天数
  sink_type: "file"         # file（默认）或 elasticsearch
  es_url: ""                # Elasticsearch URL（sink_type=elasticsearch 时）
  es_index: "taskflow-logs"
```

> 完整配置说明详见 [DEPLOYMENT.md](DEPLOYMENT.md)。

---

## 默认账户

数据库初始化后自动创建管理员账户：

| 字段 | 值 |
|------|-----|
| 用户名 | `admin` |
| 密码 | `admin123` |
| 角色 | `admin` |

> **安全提示**：首次登录后请立即修改默认密码。

---

## 测试

### 后端单元测试

基于 Catch2 框架，覆盖 DAG 引擎、Cron 解析、调度器、执行器、模型、安全工具等模块。

```bash
cd backend
make test
# 或
cd build && ctest --output-on-failure -j$(nproc)
```

当前测试规模：**170 个测试用例，1570 个断言**。

### 集成测试与压力测试

```bash
# 端到端测试
bash tests/e2e_test.sh

# 压力测试
bash tests/stress_test.sh

# 后端集成测试
bash backend/tests/integration_test.sh
```

### 前端类型检查

```bash
cd frontend
npm run type-check
```

---

## 部署

### 端口说明

| 端口 | 服务 | 说明 |
|------|------|------|
| 80 | Frontend (Nginx) | 前端 Web 界面 |
| 8080 | Scheduler HTTP | REST API 接口 |
| 50051 | Scheduler gRPC | Worker 通信端口 |
| 5432 | PostgreSQL | 数据库 |

> Docker Compose 部署时，Worker gRPC 端口（50052）仅在内部网络通信，不对外暴露。

### 高可用部署

- **Scheduler 高可用**：多实例通过 PostgreSQL Advisory Lock 自动选主，主节点故障时从节点约 10 秒内接管
- **Worker 扩容**：`docker compose up -d --scale worker=N`，Worker 动态增减无需重启 Scheduler

### 生产环境建议

1. 修改默认的 `jwt_secret`、`aes_key`、数据库密码
2. 启用 gRPC TLS（详见 [DEPLOYMENT.md](DEPLOYMENT.md) 第 9 节）
3. 修改默认管理员密码
4. 配置日志保留策略与 ELK 集成（可选）
5. 配置数据库定期备份

### 日志路径

| 日志类型 | 路径 |
|----------|------|
| Scheduler 运行日志 | `logs/scheduler.log` |
| Worker 运行日志 | `logs/worker.log` |
| 任务执行日志 | `logs/tasks/{workflow_instance_id}/{task_instance_id}.log` |

Worker 自动清理超过 `retention_days`（默认 30 天）的任务日志，每小时检查一次。

---

## 文档

- [DEPLOYMENT.md](DEPLOYMENT.md) — 部署配置、多实例、TLS、备份恢复、故障排查
- [API 文档 (OpenAPI/Swagger)](docs/openapi.yaml) — 符合 OpenAPI 3.0 标准的 REST API 规范
- 需求与设计文档（位于 `docs/` 目录）：
  - `requirements.md` — 需求规格
  - `implementation-plan.md` — 实现计划
  - `completed-features.md` — 已完成功能与验收指标
  - `constraints.md` — 架构与编码约束

### API 概览

所有 API 基础路径为 `/api/v1`，认证接口外的请求需携带 `Authorization: Bearer <token>` 头。

| 模块 | 方法 | 路径 | 说明 |
|------|------|------|------|
| **Auth** | POST | /auth/register | 用户注册 |
| | POST | /auth/login | 用户登录 |
| | POST | /auth/refresh | 刷新令牌 |
| | POST | /auth/logout | 用户登出 |
| **Tasks** | POST | /tasks | 创建任务 |
| | GET | /tasks | 获取任务列表 |
| | GET | /tasks/{id} | 获取任务详情 |
| | PUT | /tasks/{id} | 更新任务 |
| | DELETE | /tasks/{id} | 删除任务 |
| **Workflows** | POST | /workflows | 创建工作流 |
| | GET | /workflows | 获取工作流列表 |
| | GET | /workflows/{id} | 获取工作流详情 |
| | PUT | /workflows/{id} | 更新工作流 |
| | DELETE | /workflows/{id} | 删除工作流 |
| | POST | /workflows/{id}/trigger | 手动触发工作流 |
| **Instances** | GET | /instances | 获取所有实例 |
| | GET | /workflows/{id}/instances | 获取工作流实例列表 |
| | GET | /instances/{id} | 获取实例详情 |
| | POST | /instances/{id}/pause | 暂停实例 |
| | POST | /instances/{id}/resume | 恢复实例 |
| | POST | /instances/{id}/cancel | 取消实例 |
| | POST | /instances/{id}/tasks/{tid}/retry | 重试任务 |
| | POST | /instances/{id}/tasks/{tid}/kill | 终止任务 |
| | GET | /instances/{id}/tasks/{tid}/logs | 获取任务日志 |
| | GET | /instances/{id}/tasks/{tid}/logs/stream | SSE 日志流 |
| **Users** | GET | /users/me | 获取当前用户 |
| | GET | /users | 获取用户列表 |
| | POST | /users | 创建用户 |
| | PUT | /users/{id}/role | 更新用户角色 |
| | DELETE | /users/{id} | 删除用户 |
| **Workers** | GET | /workers | 获取 Worker 列表 |
| **Dashboard** | GET | /dashboard/stats | 获取仪表盘统计 |
| **Health** | GET | /health | 健康检查 |

> 完整的请求/响应格式、字段说明和示例请查看 [openapi.yaml](docs/openapi.yaml)，可导入 [Swagger Editor](https://editor.swagger.io/) 在线预览。

---

## 许可证

本项目仅供学习与内部使用。
