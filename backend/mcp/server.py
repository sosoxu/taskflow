#!/usr/bin/env python3
"""TaskFlow MCP 服务入口。

通过 stdio 传输协议为智能体提供 TaskFlow 任务调度系统的操作能力。
启动时自动导入所有工具模块并注册到 MCP 服务。
"""

import logging
import os
import sys

# 确保当前目录在 sys.path 中（支持直接运行 server.py）
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from app import client, mcp  # noqa: E402
from config import config  # noqa: E402

# 导入所有工具模块，触发 @mcp.tool() 注册
import tools.auth  # noqa: E402, F401
import tools.workflow  # noqa: E402, F401
import tools.task  # noqa: E402, F401
import tools.instance  # noqa: E402, F401
import tools.worker  # noqa: E402, F401
import tools.dashboard  # noqa: E402, F401

logging.basicConfig(
    level=getattr(logging, config.LOG_LEVEL.upper(), logging.INFO),
    stream=sys.stderr,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("taskflow-mcp")


def main():
    """启动 MCP 服务。"""
    # 环境变量预配自动登录
    client.auto_login()
    if client.access_token:
        logger.info("已通过环境变量自动登录: %s", client.user_info)
    else:
        logger.info("未自动登录，智能体需调用 login 工具登录")

    logger.info("TaskFlow MCP 服务启动，API 地址: %s", config.API_URL)
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
