"""Worker 工具组。"""

import json

from app import client, mcp


@mcp.tool()
def list_workers() -> str:
    """列出所有 Worker 及其状态信息。

    Returns:
        Worker 列表，包含名称、地址、状态（online/offline）、CPU/内存使用率、运行任务数等
    """
    data = client.get("/api/v1/workers")
    return json.dumps(data, ensure_ascii=False, indent=2)
