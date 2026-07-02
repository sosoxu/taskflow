"""MCP 服务配置，通过环境变量加载。"""

import os


class Config:
    API_URL = os.environ.get("TASKFLOW_API_URL", "http://localhost:8080").rstrip("/")
    USERNAME = os.environ.get("TASKFLOW_USERNAME", "")
    PASSWORD = os.environ.get("TASKFLOW_PASSWORD", "")
    TIMEOUT = int(os.environ.get("TASKFLOW_TIMEOUT", "30"))
    LOG_LEVEL = os.environ.get("TASKFLOW_LOG_LEVEL", "INFO")


config = Config()
