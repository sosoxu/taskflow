#!/usr/bin/env python3
"""
TaskFlow 用户权限隔离测试脚本

测试不同用户是否只能查看/操作自己创建的任务和工作流。

测试矩阵：
  用户 alice (operator) 和 bob (operator) 互相隔离
  admin 可看所有

覆盖接口：
  - POST /api/v1/auth/register  注册
  - POST /api/v1/auth/login     登录
  - POST/GET /api/v1/tasks      任务的 create/list/get/update/delete
  - POST/GET /api/v1/workflows  工作流的 create/list/get/update/delete/trigger
  - GET /api/v1/instances       实例的 list/get

前置条件：scheduler 已启动，数据库已初始化（schema.sql），admin/admin123 可登录。

用法：
  python3 test_permission_isolation.py
"""
import json
import os
import sys
import time
import urllib.request
import urllib.error

API = os.environ.get("TASKFLOW_API", "http://localhost:8080")
TS = str(int(time.time()))


def api(method, path, token=None, body=None, expect_status=None):
    """调用 API，返回 (status_code, json_body)。"""
    url = f"{API}{path}"
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, json.loads(resp.read())
    except urllib.error.HTTPError as e:
        body = e.read()
        try:
            return e.code, json.loads(body)
        except Exception:
            return e.code, {"raw": body.decode(errors="replace")}
    except urllib.error.URLError as e:
        return -1, {"error": str(e)}


def register(username, password, role=None):
    """注册用户，返回 access_token。失败则尝试用 admin 创建。"""
    body = {"username": username, "password": password}
    code, data = api("POST", "/api/v1/auth/register", body=body)
    if code != 200 and "already" not in str(data).lower():
        # 尝试用 admin 创建（管理员创建接口）
        admin_token = login("admin", "admin123")
        if admin_token:
            body2 = {"username": username, "password": password, "role": role or "operator"}
            code, data = api("POST", "/api/v1/users", token=admin_token, body=body2)
    return login(username, password)


def login(username, password):
    code, data = api("POST", "/api/v1/auth/login",
                     body={"username": username, "password": password})
    if code == 200:
        return data["data"]["access_token"]
    return None


def create_task(token, name, command="echo hello"):
    code, data = api("POST", "/api/v1/tasks", token=token, body={
        "name": name, "type": "command",
        "config": {"command": command}, "timeout": 60,
        "max_retries": 0, "retry_interval": 0,
    })
    if code == 200:
        return data["data"]["id"]
    return None


def create_workflow(token, name, task_id):
    code, data = api("POST", "/api/v1/workflows", token=token, body={
        "name": name,
        "dag": {"nodes": [{"id": "n1", "task_id": task_id}], "edges": []},
    })
    if code == 200:
        return data["data"]["id"]
    return None


# ============================================================
# 测试用例
# ============================================================

PASS = 0
FAIL = 0
SKIP = 0


def check(name, condition, detail=""):
    global PASS, FAIL
    if condition:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {detail}")


def skip(name, reason=""):
    global SKIP
    SKIP += 1
    print(f"  [SKIP] {name}  {reason}")


def section(title):
    print(f"\n{'='*60}\n{title}\n{'='*60}")


def main():
    # ---------- 前置：服务可用性 ----------
    section("前置检查")
    code, data = api("GET", "/api/v1/health")
    if code != 200:
        print(f"  scheduler 不可用 (HTTP {code}): {data}")
        print("  请先启动 scheduler: cd backend && ./build/bin/scheduler")
        sys.exit(1)
    print("  scheduler 可用")

    admin_token = login("admin", "admin123")
    if not admin_token:
        print("  admin 登录失败，请确认数据库已初始化（admin/admin123）")
        sys.exit(1)
    print("  admin 登录成功")

    # ---------- 创建两个普通用户 ----------
    section("创建测试用户 alice (operator) 和 bob (operator)")
    alice_token = register(f"alice-{TS}", "alice-pwd-123", "operator")
    bob_token = register(f"bob-{TS}", "bob-pwd-123", "operator")

    if not alice_token or not bob_token:
        print("  用户创建/登录失败，终止测试")
        sys.exit(1)
    print(f"  alice 登录成功")
    print(f"  bob   登录成功")

    # ---------- alice 和 bob 各创建一个任务 ----------
    section("各自创建任务")
    alice_task = create_task(alice_token, f"alice-task-{TS}", "echo from-alice")
    bob_task = create_task(bob_token, f"bob-task-{TS}", "echo from-bob")
    check("alice 创建任务成功", alice_task is not None)
    check("bob   创建任务成功", bob_task is not None)
    if not alice_task or not bob_task:
        print("  任务创建失败，终止")
        sys.exit(1)

    # ---------- 测试 1: list 隔离 ----------
    section("测试 1: 任务 list 隔离")
    code, data = api("GET", "/api/v1/tasks", token=alice_token)
    alice_task_ids = [t["id"] for t in data.get("data", {}).get("items", [])]
    check("alice list 看不到 bob 的任务",
          bob_task not in alice_task_ids,
          f"alice 看到了 bob 的任务 {bob_task}")
    check("alice list 能看到自己的任务",
          alice_task in alice_task_ids)

    code, data = api("GET", "/api/v1/tasks", token=bob_token)
    bob_task_ids = [t["id"] for t in data.get("data", {}).get("items", [])]
    check("bob list 看不到 alice 的任务",
          alice_task not in bob_task_ids,
          f"bob 看到了 alice 的任务 {alice_task}")
    check("bob list 能看到自己的任务",
          bob_task in bob_task_ids)

    # admin 能看到所有
    code, data = api("GET", "/api/v1/tasks", token=admin_token)
    admin_task_ids = [t["id"] for t in data.get("data", {}).get("items", [])]
    check("admin list 能看到 alice 的任务", alice_task in admin_task_ids)
    check("admin list 能看到 bob 的任务", bob_task in admin_task_ids)

    # ---------- 测试 2: get 隔离（研究显示这里可能有漏洞） ----------
    section("测试 2: 任务 get 隔离（已知疑似漏洞）")
    code, data = api("GET", f"/api/v1/tasks/{bob_task}", token=alice_token)
    if code == 200:
        check("alice GET bob 的任务 — 越权可读（漏洞）",
              False, f"alice 成功读取了 bob 的任务，HTTP 200")
    else:
        check("alice GET bob 的任务 — 被拒绝（正确）",
              code in (403, 404), f"HTTP {code}")

    code, data = api("GET", f"/api/v1/tasks/{alice_task}", token=bob_token)
    if code == 200:
        check("bob GET alice 的任务 — 越权可读（漏洞）",
              False, f"bob 成功读取了 alice 的任务，HTTP 200")
    else:
        check("bob GET alice 的任务 — 被拒绝（正确）",
              code in (403, 404), f"HTTP {code}")

    # ---------- 测试 3: update 隔离 ----------
    section("测试 3: 任务 update 隔离")
    code, data = api("PUT", f"/api/v1/tasks/{bob_task}", token=alice_token, body={
        "name": f"alice-task-{TS}", "type": "command",
        "config": {"command": "echo hacked"}, "timeout": 60,
        "max_retries": 0, "retry_interval": 0,
    })
    check("alice 不能 update bob 的任务",
          code in (403, 404), f"HTTP {code} {data}")

    code, data = api("PUT", f"/api/v1/tasks/{alice_task}", token=bob_token, body={
        "name": f"bob-task-{TS}", "type": "command",
        "config": {"command": "echo hacked"}, "timeout": 60,
        "max_retries": 0, "retry_interval": 0,
    })
    check("bob 不能 update alice 的任务",
          code in (403, 404), f"HTTP {code} {data}")

    # ---------- 测试 4: delete 隔离 ----------
    section("测试 4: 任务 delete 隔离")
    code, data = api("DELETE", f"/api/v1/tasks/{bob_task}", token=alice_token)
    check("alice 不能 delete bob 的任务",
          code in (403, 404), f"HTTP {code} {data}")

    # ---------- 各自创建工作流 ----------
    section("各自创建工作流")
    alice_wf = create_workflow(alice_token, f"alice-wf-{TS}", alice_task)
    bob_wf = create_workflow(bob_token, f"bob-wf-{TS}", bob_task)
    check("alice 创建工作流成功", alice_wf is not None)
    check("bob   创建工作流成功", bob_wf is not None)
    if not alice_wf or not bob_wf:
        print("  工作流创建失败，终止")
        sys.exit(1)

    # ---------- 测试 5: 工作流 list 隔离 ----------
    section("测试 5: 工作流 list 隔离")
    code, data = api("GET", "/api/v1/workflows", token=alice_token)
    alice_wf_ids = [w["id"] for w in data.get("data", {}).get("items", [])]
    check("alice list 看不到 bob 的工作流",
          bob_wf not in alice_wf_ids,
          f"alice 看到了 bob 的工作流")
    check("alice list 能看到自己的工作流",
          alice_wf in alice_wf_ids)

    code, data = api("GET", "/api/v1/workflows", token=bob_token)
    bob_wf_ids = [w["id"] for w in data.get("data", {}).get("items", [])]
    check("bob list 看不到 alice 的工作流",
          alice_wf not in bob_wf_ids)
    check("bob list 能看到自己的工作流",
          bob_wf in bob_wf_ids)

    # ---------- 测试 6: 工作流 get 隔离 ----------
    section("测试 6: 工作流 get 隔离")
    code, data = api("GET", f"/api/v1/workflows/{bob_wf}", token=alice_token)
    check("alice 不能 get bob 的工作流",
          code in (403, 404), f"HTTP {code}")

    code, data = api("GET", f"/api/v1/workflows/{alice_wf}", token=bob_token)
    check("bob 不能 get alice 的工作流",
          code in (403, 404), f"HTTP {code}")

    # ---------- 测试 7: 工作流 update 隔离 ----------
    section("测试 7: 工作流 update 隔离")
    code, data = api("PUT", f"/api/v1/workflows/{bob_wf}", token=alice_token, body={
        "name": f"hacked-{TS}",
        "dag": {"nodes": [{"id": "n1", "task_id": alice_task}], "edges": []},
    })
    check("alice 不能 update bob 的工作流",
          code in (403, 404), f"HTTP {code}")

    # ---------- 测试 8: 工作流 delete 隔离 ----------
    section("测试 8: 工作流 delete 隔离")
    code, data = api("DELETE", f"/api/v1/workflows/{bob_wf}", token=alice_token)
    check("alice 不能 delete bob 的工作流",
          code in (403, 404), f"HTTP {code}")

    # ---------- 测试 9: 工作流 trigger 隔离 ----------
    section("测试 9: 工作流 trigger 隔离")
    code, data = api("POST", f"/api/v1/workflows/{bob_wf}/trigger",
                     token=alice_token, body={})
    check("alice 不能 trigger bob 的工作流",
          code in (403, 404), f"HTTP {code}")

    # alice 触发自己的，再验证实例隔离
    code, data = api("POST", f"/api/v1/workflows/{alice_wf}/trigger",
                     token=alice_token, body={})
    alice_instance = None
    if code == 200:
        alice_instance = data["data"].get("instance_id") or data["data"].get("id")
    check("alice 能触发自己的工作流", code == 200, f"HTTP {code}")

    code, data = api("POST", f"/api/v1/workflows/{alice_wf}/trigger",
                     token=bob_token, body={})
    check("bob 不能 trigger alice 的工作流",
          code in (403, 404), f"HTTP {code}")

    # ---------- 测试 10: 实例查询隔离 ----------
    if alice_instance:
        section("测试 10: 实例查询隔离")
        # bob 试图看 alice 的实例
        code, data = api("GET", f"/api/v1/instances/{alice_instance}", token=bob_token)
        check("bob 不能 get alice 的实例",
              code in (403, 404), f"HTTP {code}")

        # bob 的全局实例列表不应包含 alice 的实例
        code, data = api("GET", "/api/v1/instances?page=1&page_size=100", token=bob_token)
        bob_inst_ids = [i["id"] for i in data.get("data", {}).get("items", [])]
        check("bob 全局实例列表看不到 alice 的实例",
              alice_instance not in bob_inst_ids, f"bob 看到了 {alice_instance}")

        # alice 的全局实例列表应包含自己的实例
        code, data = api("GET", "/api/v1/instances?page=1&page_size=100", token=alice_token)
        alice_inst_ids = [i["id"] for i in data.get("data", {}).get("items", [])]
        check("alice 全局实例列表能看到自己的实例",
              alice_instance in alice_inst_ids)

    # ---------- 测试 11: viewer 角色只能 GET ----------
    section("测试 11: viewer 角色权限")
    # 公开注册强制创建 operator（防提权），viewer 必须由 admin 创建
    viewer_name = f"viewer-{TS}"
    code, data = api("POST", "/api/v1/users", token=admin_token, body={
        "username": viewer_name, "password": "viewer-pwd-123", "role": "viewer",
    })
    if code == 200:
        viewer_token = login(viewer_name, "viewer-pwd-123")
    else:
        viewer_token = None
    if viewer_token:
        code, data = api("POST", "/api/v1/tasks", token=viewer_token, body={
            "name": f"viewer-task-{TS}", "type": "command",
            "config": {"command": "echo x"}, "timeout": 60,
            "max_retries": 0, "retry_interval": 0,
        })
        check("viewer 不能创建任务（写操作被拒）",
              code in (403, 401), f"HTTP {code}")

        code, data = api("GET", "/api/v1/tasks", token=viewer_token)
        check("viewer 可以 GET 任务列表", code == 200, f"HTTP {code}")
    else:
        skip("viewer 角色测试", "viewer 注册失败")

    # ---------- 测试 12: 越权访问用户管理接口 ----------
    section("测试 12: 用户管理接口仅 admin")
    code, data = api("GET", "/api/v1/users", token=alice_token)
    check("普通用户不能 GET /api/v1/users",
          code in (403, 401), f"HTTP {code}")

    code, data = api("POST", "/api/v1/users", token=alice_token, body={
        "username": f"evil-{TS}", "password": "x", "role": "operator",
    })
    check("普通用户不能创建用户",
          code in (403, 401), f"HTTP {code}")

    # ---------- 汇总 ----------
    section("测试汇总")
    print(f"  PASS: {PASS}   FAIL: {FAIL}   SKIP: {SKIP}")
    if FAIL == 0:
        print("  结论: 权限隔离符合预期")
    else:
        print(f"  结论: 发现 {FAIL} 个权限问题，请检查上方 [FAIL] 项")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
