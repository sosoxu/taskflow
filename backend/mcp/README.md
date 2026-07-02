# TaskFlow MCP 服务

为智能体提供 TaskFlow 任务调度系统的操作能力，通过 MCP (Model Context Protocol) 标准接口暴露 25 个工具。

## 快速开始

### 1. 安装依赖

```bash
cd backend/mcp
pip install -r requirements.txt
```

### 2. 配置智能体

在 Claude Desktop、Cursor 等 MCP 客户端的配置文件中添加：

```json
{
  "mcpServers": {
    "taskflow": {
      "command": "python3",
      "args": ["/path/to/backend/mcp/server.py"],
      "env": {
        "TASKFLOW_API_URL": "http://localhost:8080",
        "TASKFLOW_USERNAME": "admin",
        "TASKFLOW_PASSWORD": "your-password"
      }
    }
  }
}
```

设置环境变量后，MCP 服务启动时会自动登录。也可不设 `TASKFLOW_USERNAME` / `TASKFLOW_PASSWORD`，由智能体调用 `login` 工具登录。

### 3. 环境变量

| 变量 | 必需 | 默认值 | 说明 |
|------|------|--------|------|
| `TASKFLOW_API_URL` | 是 | `http://localhost:8080` | TaskFlow Scheduler API 地址 |
| `TASKFLOW_USERNAME` | 否 | | 自动登录用户名 |
| `TASKFLOW_PASSWORD` | 否 | | 自动登录密码 |
| `TASKFLOW_TIMEOUT` | 否 | `30` | HTTP 请求超时（秒） |
| `TASKFLOW_LOG_LEVEL` | 否 | `INFO` | 日志级别 |

## 工具列表

### 认证（3 个）

| 工具 | 说明 |
|------|------|
| `login` | 登录并缓存 token |
| `logout` | 登出并清除 token |
| `get_current_user` | 获取当前用户信息 |

### 工作流（7 个）

| 工具 | 说明 |
|------|------|
| `create_workflow` | 创建工作流（含 DAG 定义） |
| `list_workflows` | 分页查询工作流 |
| `get_workflow` | 获取工作流详情 |
| `update_workflow` | 更新工作流 |
| `delete_workflow` | 删除工作流 |
| `trigger_workflow` | 触发工作流执行 |
| `list_workflow_instances` | 查询工作流执行历史 |

### 任务（5 个）

| 工具 | 说明 |
|------|------|
| `create_task` | 创建任务（command/script/sql） |
| `list_tasks` | 分页查询任务 |
| `get_task` | 获取任务详情 |
| `update_task` | 更新任务 |
| `delete_task` | 删除任务 |

### 实例（8 个）

| 工具 | 说明 |
|------|------|
| `list_instances` | 分页查询实例 |
| `get_instance` | 获取实例详情（含所有任务实例） |
| `pause_instance` | 暂停实例 |
| `resume_instance` | 恢复实例 |
| `cancel_instance` | 取消实例 |
| `retry_task` | 重试单个任务 |
| `kill_task` | 终止运行中的任务 |
| `get_task_log` | 获取任务日志 |

### Worker & Dashboard（2 个）

| 工具 | 说明 |
|------|------|
| `list_workers` | 列出所有 Worker |
| `get_dashboard_stats` | 获取仪表盘统计 |

## 认证机制

- **环境变量预配**：设置 `TASKFLOW_USERNAME` / `TASKFLOW_PASSWORD` 后自动登录
- **login 工具**：智能体调用 `login(username, password)` 登录
- **自动刷新**：access_token 过期（401）时自动用 refresh_token 刷新
- Token 仅存在内存中，服务重启后需重新登录

## DAG 格式示例

`create_workflow` 的 `dag_json` 参数：

```json
{
  "nodes": [
    {"id": "n1", "task_id": "550e8400-e29b-41d4-a716-446655440000"},
    {"id": "n2", "task_id": "550e8400-e29b-41d4-a716-446655440001"}
  ],
  "edges": [
    {"from": "n1", "to": "n2"}
  ]
}
```

## 文件结构

```
mcp/
├── server.py          # 服务入口
├── app.py             # FastMCP + client 共享实例
├── client.py          # HTTP API 客户端（认证、重试）
├── config.py          # 环境变量配置
├── requirements.txt   # Python 依赖
└── tools/
    ├── auth.py        # 认证工具
    ├── workflow.py    # 工作流工具
    ├── task.py        # 任务工具
    ├── instance.py    # 实例工具
    ├── worker.py      # Worker 工具
    └── dashboard.py   # Dashboard 工具
```
