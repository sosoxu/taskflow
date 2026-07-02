"""TaskFlow HTTP API 客户端，封装认证与请求重试。"""

import json
import logging

import httpx

from config import config

logger = logging.getLogger("taskflow-mcp")


class TaskFlowClient:
    """同步 HTTP 客户端，管理 JWT token 生命周期。"""

    def __init__(self):
        self.base_url = config.API_URL
        self._http = httpx.Client(timeout=config.TIMEOUT)
        self.access_token: str | None = None
        self.refresh_token: str | None = None
        self.user_info: dict | None = None

    # ------------------------------------------------------------------
    # 认证
    # ------------------------------------------------------------------

    def login(self, username: str, password: str) -> dict:
        """登录并缓存 token。"""
        resp = self._http.post(
            f"{self.base_url}/api/v1/auth/login",
            json={"username": username, "password": password},
        )
        resp.raise_for_status()
        body = resp.json()
        if body.get("code") != 0:
            raise RuntimeError(body.get("message", "登录失败"))
        data = body["data"]
        self.access_token = data["access_token"]
        self.refresh_token = data["refresh_token"]
        self.user_info = {
            "user_id": data.get("user_id"),
            "username": data.get("username"),
            "role": data.get("role"),
        }
        logger.info("登录成功: %s", self.user_info)
        return data

    def logout(self) -> None:
        """登出并清除本地 token。"""
        if not self.access_token:
            return
        try:
            self._request("POST", "/api/v1/auth/logout")
        except Exception:
            pass  # 登出失败也清除本地 token
        finally:
            self.access_token = None
            self.refresh_token = None
            self.user_info = None

    def _do_refresh(self) -> None:
        """用 refresh_token 换取新的 access_token。"""
        if not self.refresh_token:
            raise RuntimeError("无 refresh_token，请先调用 login 工具登录")
        resp = self._http.post(
            f"{self.base_url}/api/v1/auth/refresh",
            json={"refresh_token": self.refresh_token},
        )
        resp.raise_for_status()
        body = resp.json()
        if body.get("code") != 0:
            self.access_token = None
            self.refresh_token = None
            raise RuntimeError(body.get("message", "刷新 token 失败"))
        data = body["data"]
        self.access_token = data["access_token"]
        self.refresh_token = data["refresh_token"]
        logger.info("token 已自动刷新")

    def auto_login(self) -> None:
        """环境变量预配自动登录。"""
        if config.USERNAME and config.PASSWORD:
            try:
                self.login(config.USERNAME, config.PASSWORD)
            except Exception as e:
                logger.warning("自动登录失败: %s", e)

    # ------------------------------------------------------------------
    # HTTP 请求封装
    # ------------------------------------------------------------------

    def _request(self, method: str, path: str, **kwargs) -> dict | None:
        """发送已认证请求，401 时自动刷新 token 重试一次。"""
        if not self.access_token:
            raise RuntimeError("未认证，请先调用 login 工具登录")

        headers = kwargs.pop("headers", {})
        headers["Authorization"] = f"Bearer {self.access_token}"

        url = f"{self.base_url}{path}"
        resp = self._http.request(method, url, headers=headers, **kwargs)

        # 401 自动刷新重试
        if resp.status_code == 401:
            self._do_refresh()
            headers["Authorization"] = f"Bearer {self.access_token}"
            resp = self._http.request(method, url, headers=headers, **kwargs)

        resp.raise_for_status()
        body = resp.json()
        if body.get("code") != 0:
            raise RuntimeError(body.get("message", "API 返回错误"))
        return body.get("data")

    def get(self, path: str, params: dict | None = None) -> dict | None:
        return self._request("GET", path, params=params)

    def post(self, path: str, json_body: dict | None = None) -> dict | None:
        return self._request("POST", path, json=json_body or {})

    def put(self, path: str, json_body: dict | None = None) -> dict | None:
        return self._request("PUT", path, json=json_body or {})

    def delete(self, path: str) -> dict | None:
        return self._request("DELETE", path)


# 全局单例
client = TaskFlowClient()
