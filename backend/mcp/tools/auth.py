"""认证工具组。"""

import json

from app import client, mcp


@mcp.tool()
def login(username: str, password: str) -> str:
    """登录 TaskFlow 系统，获取并缓存认证 token。

    Args:
        username: 用户名（3-32 字符）
        password: 密码（至少 8 字符）

    Returns:
        登录信息，包含 access_token、用户 ID、角色等
    """
    data = client.login(username, password)
    return json.dumps(data, ensure_ascii=False, indent=2)


@mcp.tool()
def logout() -> str:
    """登出 TaskFlow 系统，清除本地缓存的 token。"""
    client.logout()
    return "已成功登出"


@mcp.tool()
def get_current_user() -> str:
    """获取当前登录用户的详细信息。"""
    data = client.get("/api/v1/users/me")
    return json.dumps(data, ensure_ascii=False, indent=2)
