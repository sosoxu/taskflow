"""实例工具组。"""

import json

from app import client, mcp


@mcp.tool()
def list_instances(
    page: int = 1,
    page_size: int = 20,
    workflow_id: str = "",
    task_id: str = "",
) -> str:
    """分页查询工作流实例列表。

    Args:
        page: 页码，从 1 开始
        page_size: 每页数量，1-100
        workflow_id: 按工作流 ID 过滤
        task_id: 按任务 ID 过滤（查询包含该任务的实例）

    Returns:
        实例分页列表
    """
    params: dict = {"page": page, "page_size": page_size}
    if workflow_id:
        params["workflow_id"] = workflow_id
    if task_id:
        params["task_id"] = task_id

    data = client.get("/api/v1/instances", params=params)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def get_instance(instance_id: str) -> str:
    """获取工作流实例详情，包含所有任务实例及其状态。

    Args:
        instance_id: 工作流实例 UUID

    Returns:
        实例详细信息，含 task_instances 列表
    """
    data = client.get(f"/api/v1/instances/{instance_id}")
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def pause_instance(instance_id: str) -> str:
    """暂停工作流实例执行。

    Args:
        instance_id: 工作流实例 UUID

    Returns:
        操作结果
    """
    client.post(f"/api/v1/instances/{instance_id}/pause")
    return f"实例 {instance_id} 已暂停"


@mcp.tool()
def resume_instance(instance_id: str) -> str:
    """恢复暂停的工作流实例执行。

    Args:
        instance_id: 工作流实例 UUID

    Returns:
        操作结果
    """
    client.post(f"/api/v1/instances/{instance_id}/resume")
    return f"实例 {instance_id} 已恢复"


@mcp.tool()
def cancel_instance(instance_id: str) -> str:
    """取消工作流实例执行。

    Args:
        instance_id: 工作流实例 UUID

    Returns:
        操作结果
    """
    client.post(f"/api/v1/instances/{instance_id}/cancel")
    return f"实例 {instance_id} 已取消"


@mcp.tool()
def retry_task(instance_id: str, task_instance_id: str) -> str:
    """重试实例中指定的任务。

    Args:
        instance_id: 工作流实例 UUID
        task_instance_id: 任务实例 UUID

    Returns:
        操作结果
    """
    client.post(f"/api/v1/instances/{instance_id}/tasks/{task_instance_id}/retry")
    return f"任务实例 {task_instance_id} 已重新派发"


@mcp.tool()
def kill_task(instance_id: str, task_instance_id: str) -> str:
    """终止实例中正在运行的任务。

    Args:
        instance_id: 工作流实例 UUID
        task_instance_id: 任务实例 UUID

    Returns:
        操作结果
    """
    client.post(f"/api/v1/instances/{instance_id}/tasks/{task_instance_id}/kill")
    return f"任务实例 {task_instance_id} 已终止"


@mcp.tool()
def get_task_log(instance_id: str, task_instance_id: str) -> str:
    """获取任务的执行日志。

    Args:
        instance_id: 工作流实例 UUID
        task_instance_id: 任务实例 UUID

    Returns:
        任务日志内容
    """
    data = client.get(f"/api/v1/instances/{instance_id}/tasks/{task_instance_id}/logs")
    return json.dumps(data, ensure_ascii=False, indent=2)
