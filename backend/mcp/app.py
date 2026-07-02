"""共享实例：FastMCP 服务 + TaskFlow 客户端。"""

from mcp.server.fastmcp import FastMCP

from client import client

mcp = FastMCP("taskflow")
