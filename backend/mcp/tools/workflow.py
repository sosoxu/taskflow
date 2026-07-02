"""工作流工具组。"""

import json

from app import client, mcp


def _parse_json_field(value: str, field_name: str) -> dict | str:
    """尝试解析 JSON 字符串，失败则原样返回。"""
    if not value:
        return ""
    try:
        return json.loads(value)
    except (json.JSONDecodeError, TypeError):
        return value


@mcp.tool()
def create_workflow(
    name: str,
    dag_json: str,
    description: str = "",
    schedule_strategy: str = "random",
    target_worker_id: str = "",
    cron_expression: str = "",
    cron_enabled: bool = False,
) -> str:
    """创建工作流。

    Args:
        name: 工作流名称
        dag_json: DAG 定义，JSON 字符串，格式如 {"nodes": [{"id": "n1", "task_id": "uuid"}], "edges": [{"from": "n1", "to": "n2"}]}
        description: 工作流描述
        schedule_strategy: 调度策略，可选 random / load_balance / specified
        target_worker_id: 指定 worker ID（仅 specified 策略时有效）
        cron_expression: cron 表达式（如 "0 2 * * *" 表示每天凌晨 2 点）
        cron_enabled: 是否启用定时触发

    Returns:
        创建的工作流信息
    """
    body: dict = {
        "name": name,
        "dag": _parse_json_field(dag_json, "dag"),
        "schedule_strategy": schedule_strategy,
        "cron_enabled": cron_enabled,
    }
    if description:
        body["description"] = description
    if target_worker_id:
        body["target_worker_id"] = target_worker_id
    if cron_expression:
        body["cron_expression"] = cron_expression

    data = client.post("/api/v1/workflows", json_body=body)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def list_workflows(
    page: int = 1,
    page_size: int = 20,
    keyword: str = "",
    creator_id: str = "",
) -> str:
    """分页查询工作流列表。

    Args:
        page: 页码，从 1 开始
        page_size: 每页数量，1-100
        keyword: 名称关键词搜索
        creator_id: 按创建者 ID 过滤

    Returns:
        工作流分页列表
    """
    params: dict = {"page": page, "page_size": page_size}
    if keyword:
        params["keyword"] = keyword
    if creator_id:
        params["creator_id"] = creator_id

    data = client.get("/api/v1/workflows", params=params)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def get_workflow(workflow_id: str) -> str:
    """获取工作流详情。

    Args:
        workflow_id: 工作流 UUID

    Returns:
        工作流详细信息
    """
    data = client.get(f"/api/v1/workflows/{workflow_id}")
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def update_workflow(
    workflow_id: str,
    name: str,
    dag_json: str,
    description: str = "",
    schedule_strategy: str = "random",
    target_worker_id: str = "",
    cron_expression: str = "",
    cron_enabled: bool = False,
) -> str:
    """更新工作流。

    Args:
        workflow_id: 工作流 UUID
        name: 工作流名称
        dag_json: DAG 定义，JSON 字符串
        description: 工作流描述
        schedule_strategy: 调度策略，可选 random / load_balance / specified
        target_worker_id: 指定 worker ID（仅 specified 策略时有效）
        cron_expression: cron 表达式
        cron_enabled: 是否启用定时触发

    Returns:
        更新后的工作流信息
    """
    body: dict = {
        "name": name,
        "dag": _parse_json_field(dag_json, "dag"),
        "schedule_strategy": schedule_strategy,
        "cron_enabled": cron_enabled,
    }
    if description:
        body["description"] = description
    if target_worker_id:
        body["target_worker_id"] = target_worker_id
    if cron_expression:
        body["cron_expression"] = cron_expression

    data = client.put(f"/api/v1/workflows/{workflow_id}", json_body=body)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def delete_workflow(workflow_id: str) -> str:
    """删除工作流（软删除）。

    Args:
        workflow_id: 工作流 UUID

    Returns:
        操作结果
    """
    client.delete(f"/api/v1/workflows/{workflow_id}")
    return f"工作流 {workflow_id} 已删除"


@mcp.tool()
def trigger_workflow(workflow_id: str, param_overrides: str = "") -> str:
    """触发工作流执行。

    Args:
        workflow_id: 工作流 UUID
        param_overrides: 参数覆盖，JSON 字符串，如 {"key": "value"}

    Returns:
        创建的工作流实例信息
    """
    body: dict = {}
    if param_overrides:
        body["param_overrides"] = _parse_json_field(param_overrides, "param_overrides")

    data = client.post(f"/api/v1/workflows/{workflow_id}/trigger", json_body=body)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def list_workflow_instances(
    workflow_id: str,
    page: int = 1,
    page_size: int = 20,
) -> str:
    """查询指定工作流的执行实例历史。

    Args:
        workflow_id: 工作流 UUID
        page: 页码，从 1 开始
        page_size: 每页数量，1-100

    Returns:
        工作流实例分页列表
    """
    params = {"page": page, "page_size": page_size}
    data = client.get(f"/api/v1/workflows/{workflow_id}/instances", params=params)
    return json.dumps(data, ensure_ascii=False, indent=2)
