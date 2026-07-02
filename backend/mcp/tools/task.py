"""任务工具组。"""

import json

from app import client, mcp


def _parse_json_field(value: str) -> dict | str:
    """尝试解析 JSON 字符串，失败则原样返回。"""
    if not value:
        return ""
    try:
        return json.loads(value)
    except (json.JSONDecodeError, TypeError):
        return value


@mcp.tool()
def create_task(
    name: str,
    type: str,
    config_json: str = "",
    description: str = "",
    timeout: int = 3600,
    max_retries: int = 0,
    retry_interval: int = 60,
    resource_tags: str = "",
    parameters_json: str = "",
) -> str:
    """创建任务。

    Args:
        name: 任务名称
        type: 任务类型，可选 command / script / sql
        config_json: 任务配置，JSON 字符串。command 类型如 {"command": "echo hello"}；script 类型如 {"script": "#!/bin/bash\\necho hello", "language": "bash"}；sql 类型如 {"sql": "SELECT 1", "host": "...", "port": 5432, "database": "...", "user": "...", "password": "..."}
        description: 任务描述
        timeout: 超时时间（秒），1-86400，默认 3600
        max_retries: 最大重试次数，0-100，默认 0
        retry_interval: 重试间隔（秒），0-3600，默认 60
        resource_tags: 资源标签，逗号分隔，如 "gpu,high-memory"
        parameters_json: 参数定义，JSON 字符串

    Returns:
        创建的任务信息
    """
    body: dict = {
        "name": name,
        "type": type,
        "timeout": timeout,
        "max_retries": max_retries,
        "retry_interval": retry_interval,
    }
    if config_json:
        body["config"] = _parse_json_field(config_json)
    if description:
        body["description"] = description
    if resource_tags:
        body["resource_tags"] = [t.strip() for t in resource_tags.split(",") if t.strip()]
    if parameters_json:
        body["parameters"] = _parse_json_field(parameters_json)

    data = client.post("/api/v1/tasks", json_body=body)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def list_tasks(
    page: int = 1,
    page_size: int = 20,
    type: str = "",
    keyword: str = "",
    creator_id: str = "",
) -> str:
    """分页查询任务列表。

    Args:
        page: 页码，从 1 开始
        page_size: 每页数量，1-100
        type: 按任务类型过滤，可选 command / script / sql
        keyword: 名称关键词搜索
        creator_id: 按创建者 ID 过滤

    Returns:
        任务分页列表
    """
    params: dict = {"page": page, "page_size": page_size}
    if type:
        params["type"] = type
    if keyword:
        params["keyword"] = keyword
    if creator_id:
        params["creator_id"] = creator_id

    data = client.get("/api/v1/tasks", params=params)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def get_task(task_id: str) -> str:
    """获取任务详情。

    Args:
        task_id: 任务 UUID

    Returns:
        任务详细信息
    """
    data = client.get(f"/api/v1/tasks/{task_id}")
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def update_task(
    task_id: str,
    name: str,
    type: str,
    config_json: str = "",
    description: str = "",
    timeout: int = 3600,
    max_retries: int = 0,
    retry_interval: int = 60,
    resource_tags: str = "",
    parameters_json: str = "",
) -> str:
    """更新任务。

    Args:
        task_id: 任务 UUID
        name: 任务名称
        type: 任务类型，可选 command / script / sql
        config_json: 任务配置，JSON 字符串
        description: 任务描述
        timeout: 超时时间（秒），1-86400
        max_retries: 最大重试次数，0-100
        retry_interval: 重试间隔（秒），0-3600
        resource_tags: 资源标签，逗号分隔
        parameters_json: 参数定义，JSON 字符串

    Returns:
        更新后的任务信息
    """
    body: dict = {
        "name": name,
        "type": type,
        "timeout": timeout,
        "max_retries": max_retries,
        "retry_interval": retry_interval,
    }
    if config_json:
        body["config"] = _parse_json_field(config_json)
    if description:
        body["description"] = description
    if resource_tags:
        body["resource_tags"] = [t.strip() for t in resource_tags.split(",") if t.strip()]
    if parameters_json:
        body["parameters"] = _parse_json_field(parameters_json)

    data = client.put(f"/api/v1/tasks/{task_id}", json_body=body)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def delete_task(task_id: str) -> str:
    """删除任务（软删除）。

    Args:
        task_id: 任务 UUID

    Returns:
        操作结果
    """
    client.delete(f"/api/v1/tasks/{task_id}")
    return f"任务 {task_id} 已删除"
