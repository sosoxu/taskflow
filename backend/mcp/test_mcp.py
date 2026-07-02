#!/usr/bin/env python3
"""MCP 服务测试脚本：通过 stdio 协议与 server.py 交互，验证所有工具。"""

import json
import os
import subprocess
import sys
import time

MCP_DIR = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(MCP_DIR, "server.py")


class MCPClient:
    """简单的 MCP stdio 客户端。"""

    def __init__(self, env: dict | None = None):
        self.env = env or {}
        self.proc: subprocess.Popen | None = None
        self._id = 0

    def start(self):
        full_env = os.environ.copy()
        full_env.update(self.env)
        self.proc = subprocess.Popen(
            [sys.executable, SERVER],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=full_env,
            text=True,
            bufsize=1,
        )
        # 初始化握手
        self._send("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "test", "version": "0.1"},
        })
        self._notify("notifications/initialized")

    def stop(self):
        if self.proc:
            self.proc.terminate()
            self.proc.wait(timeout=5)

    def _send(self, method: str, params: dict) -> dict:
        self._id += 1
        msg = {"jsonrpc": "2.0", "id": self._id, "method": method, "params": params}
        self.proc.stdin.write(json.dumps(msg) + "\n")
        self.proc.stdin.flush()
        return self._recv()

    def _notify(self, method: str):
        msg = {"jsonrpc": "2.0", "method": method}
        self.proc.stdin.write(json.dumps(msg) + "\n")
        self.proc.stdin.flush()

    def _recv(self) -> dict:
        while True:
            line = self.proc.stdout.readline()
            if not line:
                stderr = self.proc.stderr.read()
                raise RuntimeError(f"MCP server closed. stderr: {stderr}")
            line = line.strip()
            if not line:
                continue
            msg = json.loads(line)
            if "id" in msg:
                return msg

    def call_tool(self, name: str, arguments: dict = None) -> dict:
        return self._send("tools/call", {
            "name": name,
            "arguments": arguments or {},
        })

    def list_tools(self) -> list:
        resp = self._send("tools/list", {})
        return resp["result"]["tools"]


def check(resp: dict, label: str):
    """检查工具调用结果，打印通过/失败。"""
    if "error" in resp:
        print(f"  FAIL  {label}: {resp['error'].get('message', 'unknown')}")
        return False
    result = resp.get("result", {})
    content = result.get("content", [])
    text = content[0]["text"] if content else ""
    is_error = result.get("isError", False)
    if is_error:
        print(f"  FAIL  {label}: {text[:200]}")
        return False
    # 截断显示
    display = text[:120].replace("\n", " ") + ("..." if len(text) > 120 else "")
    print(f"  PASS  {label}: {display}")
    return True


def main():
    print("=" * 70)
    print("TaskFlow MCP 服务测试")
    print("=" * 70)

    # ---- 测试 1: 环境变量预配自动登录 ----
    print("\n[1] 环境变量预配自动登录")
    client = MCPClient(env={
        "TASKFLOW_API_URL": "http://localhost:8080",
        "TASKFLOW_USERNAME": "admin",
        "TASKFLOW_PASSWORD": "admin123",
    })
    client.start()
    # 等待自动登录完成
    time.sleep(1)

    # 验证已自动登录 - 调用 get_current_user
    resp = client.call_tool("get_current_user")
    check(resp, "get_current_user (自动登录后)")

    # ---- 测试 2: tools/list ----
    print("\n[2] tools/list")
    tools = client.list_tools()
    print(f"  PASS  工具数量: {len(tools)}")
    for t in sorted(tools, key=lambda x: x["name"]):
        print(f"       - {t['name']}")

    # ---- 测试 3: login 工具 ----
    print("\n[3] login 工具")
    resp = client.call_tool("login", {"username": "admin", "password": "admin123"})
    check(resp, "login")

    # ---- 测试 4: dashboard ----
    print("\n[4] get_dashboard_stats")
    resp = client.call_tool("get_dashboard_stats")
    check(resp, "get_dashboard_stats")

    # ---- 测试 5: list_workers ----
    print("\n[5] list_workers")
    resp = client.call_tool("list_workers")
    check(resp, "list_workers")

    # ---- 测试 6: create_task ----
    print("\n[6] create_task")
    ts = str(int(time.time()))
    resp = client.call_tool("create_task", {
        "name": f"mcp-test-task-{ts}",
        "type": "command",
        "config_json": '{"command": "echo hello-from-mcp"}',
        "description": "MCP 测试任务",
        "timeout": 60,
    })
    ok = check(resp, "create_task")
    task_id = ""
    if ok:
        try:
            data = json.loads(resp["result"]["content"][0]["text"])
            task_id = data.get("id", "")
        except Exception:
            pass

    # ---- 测试 7: list_tasks ----
    print("\n[7] list_tasks")
    resp = client.call_tool("list_tasks", {"page": 1, "page_size": 5})
    check(resp, "list_tasks")

    # ---- 测试 8: get_task ----
    print("\n[8] get_task")
    if task_id:
        resp = client.call_tool("get_task", {"task_id": task_id})
        check(resp, f"get_task ({task_id[:8]}...)")
    else:
        print("  SKIP  get_task (无 task_id)")

    # ---- 测试 9: create_workflow ----
    print("\n[9] create_workflow")
    dag = json.dumps({
        "nodes": [{"id": "n1", "task_id": task_id}],
        "edges": [],
    })
    resp = client.call_tool("create_workflow", {
        "name": f"mcp-test-workflow-{ts}",
        "dag_json": dag,
        "description": "MCP 测试工作流",
    })
    ok = check(resp, "create_workflow")
    workflow_id = ""
    if ok:
        try:
            data = json.loads(resp["result"]["content"][0]["text"])
            workflow_id = data.get("id", "")
        except Exception:
            pass

    # ---- 测试 10: list_workflows ----
    print("\n[10] list_workflows")
    resp = client.call_tool("list_workflows", {"page": 1, "page_size": 5})
    check(resp, "list_workflows")

    # ---- 测试 11: get_workflow ----
    print("\n[11] get_workflow")
    if workflow_id:
        resp = client.call_tool("get_workflow", {"workflow_id": workflow_id})
        check(resp, f"get_workflow ({workflow_id[:8]}...)")
    else:
        print("  SKIP  get_workflow (无 workflow_id)")

    # ---- 测试 12: trigger_workflow ----
    print("\n[12] trigger_workflow")
    if workflow_id:
        resp = client.call_tool("trigger_workflow", {"workflow_id": workflow_id})
        ok = check(resp, "trigger_workflow")
        instance_id = ""
        if ok:
            try:
                data = json.loads(resp["result"]["content"][0]["text"])
                instance_id = data.get("instance_id", "") or data.get("id", "")
            except Exception:
                pass
    else:
        print("  SKIP  trigger_workflow (无 workflow_id)")
        instance_id = ""

    # ---- 测试 13: list_instances ----
    print("\n[13] list_instances")
    resp = client.call_tool("list_instances", {"page": 1, "page_size": 5})
    check(resp, "list_instances")

    # ---- 测试 14: get_instance ----
    print("\n[14] get_instance")
    if instance_id:
        resp = client.call_tool("get_instance", {"instance_id": instance_id})
        check(resp, f"get_instance ({instance_id[:8]}...)")
    else:
        print("  SKIP  get_instance (无 instance_id)")

    # ---- 测试 15: list_workflow_instances ----
    print("\n[15] list_workflow_instances")
    if workflow_id:
        resp = client.call_tool("list_workflow_instances", {
            "workflow_id": workflow_id, "page": 1, "page_size": 5,
        })
        check(resp, "list_workflow_instances")
    else:
        print("  SKIP  list_workflow_instances (无 workflow_id)")

    # ---- 测试 16: 未登录调用应报错 ----
    print("\n[16] 未登录调用报错测试")
    client.stop()
    client2 = MCPClient(env={
        "TASKFLOW_API_URL": "http://localhost:8080",
    })
    client2.start()
    time.sleep(0.5)
    resp = client2.call_tool("list_workflows")
    if "error" in resp or resp.get("result", {}).get("isError"):
        print("  PASS  未登录调用正确返回错误")
    else:
        print(f"  FAIL  未登录调用未返回错误: {resp}")
    client2.stop()

    # ---- 测试 17: logout ----
    print("\n[17] logout")
    client3 = MCPClient(env={
        "TASKFLOW_API_URL": "http://localhost:8080",
        "TASKFLOW_USERNAME": "admin",
        "TASKFLOW_PASSWORD": "admin123",
    })
    client3.start()
    time.sleep(1)
    resp = client3.call_tool("logout")
    check(resp, "logout")
    client3.stop()

    print("\n" + "=" * 70)
    print("测试完成")
    print("=" * 70)


if __name__ == "__main__":
    main()
