"""Dashboard 工具组。"""

import json

from app import client, mcp


@mcp.tool()
def get_dashboard_stats() -> str:
    """获取仪表盘统计数据，包括任务总数、工作流总数、运行中实例数、在线 Worker 数、今日执行数、成功率等。

    Returns:
        仪表盘统计数据
    """
    data = client.get("/api/v1/dashboard/stats")
    return json.dumps(data, ensure_ascii=False, indent=2)
