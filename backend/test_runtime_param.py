#!/usr/bin/env python3
"""
验证用户场景：工作流三个任务 A/B/C，各自有独立参数 pa/pb/pc，
触发时由用户输入三个参数的值，运行时按输入值执行。

关键点：三个参数名互不冲突（pa/pb/pc），触发时传一个全局 param_overrides
包含全部三个值即可。每个任务的 command 只引用自己的占位符，互不干扰。
"""
import json
import sys
import time
import urllib.request
import urllib.error

API = "http://localhost:8080"
TS = str(int(time.time()))


def api(method, path, token=None, body=None):
    url = f"{API}{path}"
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return resp.status, json.loads(resp.read())
    except urllib.error.HTTPError as e:
        body = e.read()
        try:
            return e.code, json.loads(body)
        except Exception:
            return e.code, {"raw": body.decode(errors="replace")}
    except urllib.error.URLError as e:
        return -1, {"error": str(e)}


def wait_terminal(token, instance_id, timeout=60):
    deadline = time.time() + timeout
    terminal = {"SUCCESS", "FAILED", "TIMEOUT", "CANCELLED",
                "NODE_OFFLINE", "UPSTREAM_FAILED"}
    while time.time() < deadline:
        code, data = api("GET", f"/api/v1/instances/{instance_id}", token=token)
        if code == 200:
            tis = data["data"].get("task_instances", [])
            if tis and all(t["status"] in terminal for t in tis):
                return tis
        time.sleep(2)
    return []


def get_log(token, instance_id, ti_id):
    code, data = api("GET",
                     f"/api/v1/instances/{instance_id}/tasks/{ti_id}/logs",
                     token=token)
    if code == 200:
        return data["data"].get("log", "").strip()
    return f"<log error {code}>"


def main():
    code, data = api("POST", "/api/v1/auth/login",
                     body={"username": "admin", "password": "admin123"})
    if code != 200:
        print(f"admin 登录失败: {data}")
        sys.exit(1)
    token = data["data"]["access_token"]
    print("admin 登录成功")

    # 检查 worker
    code, data = api("GET", "/api/v1/workers", token=token)
    online = [w for w in data["data"]["items"] if w["status"] == "online"]
    if not online:
        print("[错误] 没有在线 worker")
        sys.exit(1)
    print(f"在线 worker: {len(online)}")

    # ============================================================
    # 步骤 1: 创建 3 个任务，每个任务引用自己的参数占位符
    #   任务 A 引用 ${pa}
    #   任务 B 引用 ${pb}
    #   任务 C 引用 ${pc}
    # 并在 parameters_json 中声明参数元数据（含 default）
    # ============================================================
    print(f"\n{'='*60}")
    print("步骤 1: 创建 3 个任务，各自引用独立参数 pa/pb/pc")
    print(f"{'='*60}")

    task_specs = [
        ("A", "pa", "default-a"),
        ("B", "pb", "default-b"),
        ("C", "pc", "default-c"),
    ]
    task_ids = {}
    for label, param_name, default_val in task_specs:
        code, data = api("POST", "/api/v1/tasks", token=token, body={
            "name": f"runtime-param-{label}-{TS}",
            "type": "command",
            "config": {"command": f"echo task-{label} param-{param_name}=${{{param_name}}}"},
            "timeout": 30,
            "max_retries": 0,
            "retry_interval": 0,
            "parameters_json": {
                param_name: {"default": default_val, "type": "string"},
            },
        })
        if code != 200:
            print(f"  任务 {label} 创建失败: {data}")
            sys.exit(1)
        task_ids[label] = data["data"]["id"]
        print(f"  任务 {label} ({task_ids[label][:8]}): "
              f"command='echo task-{label} param-{param_name}=${{{param_name}}}', "
              f"default {param_name}={default_val}")

    # ============================================================
    # 步骤 2: 创建工作流，DAG 中 3 个并行节点（不写 param_overrides）
    # ============================================================
    print(f"\n{'='*60}")
    print("步骤 2: 创建工作流（DAG 不写 param_overrides，留给触发时指定）")
    print(f"{'='*60}")

    dag = {
        "nodes": [
            {"id": "nA", "task_id": task_ids["A"]},
            {"id": "nB", "task_id": task_ids["B"]},
            {"id": "nC", "task_id": task_ids["C"]},
        ],
        "edges": [],
    }
    code, data = api("POST", "/api/v1/workflows", token=token, body={
        "name": f"runtime-param-wf-{TS}", "dag": dag,
        "schedule_strategy": "load_balance",
    })
    if code != 200:
        print(f"  工作流创建失败: {data}")
        sys.exit(1)
    wf_id = data["data"]["id"]
    print(f"  工作流: {wf_id[:8]} (3 个并行任务，无静态参数覆盖)")

    # ============================================================
    # 步骤 3: 第一次触发 — 用户输入运行时参数值
    #   pa=user-val-A, pb=user-val-B, pc=user-val-C
    # ============================================================
    print(f"\n{'='*60}")
    print("步骤 3: 第一次触发，运行时指定参数")
    print("  param_overrides = {pa: user-val-A, pb: user-val-B, pc: user-val-C}")
    print(f"{'='*60}")

    user_params_1 = {"pa": "user-val-A", "pb": "user-val-B", "pc": "user-val-C"}
    code, data = api("POST", f"/api/v1/workflows/{wf_id}/trigger",
                     token=token, body={"param_overrides": user_params_1})
    if code != 200:
        print(f"  触发失败: {data}")
        sys.exit(1)
    inst_1 = data["data"].get("instance_id") or data["data"].get("id")
    print(f"  实例 1: {inst_1[:8]}")

    tis = wait_terminal(token, inst_1)
    print(f"\n  执行结果:")
    expected_1 = {"nA": "user-val-A", "nB": "user-val-B", "nC": "user-val-C"}
    all_match_1 = True
    for ti in sorted(tis, key=lambda x: x["node_id"]):
        node = ti["node_id"]
        log = get_log(token, inst_1, ti["id"])
        exp = expected_1.get(node, "?")
        match = f"param-{['pa','pb','pc'][['nA','nB','nC'].index(node)]}={exp}" in log
        mark = "OK" if match else "MISMATCH"
        if not match:
            all_match_1 = False
        print(f"    {node} [{ti['status']}/{mark}]: {log}")
    print(f"  [结论] {'✓ 运行时参数生效，三任务用了不同值' if all_match_1 else '✗ 未生效'}")

    # ============================================================
    # 步骤 4: 第二次触发 — 用不同的参数值（证明不是固定的）
    #   pa=second-A, pb=second-B, pc=second-C
    # ============================================================
    print(f"\n{'='*60}")
    print("步骤 4: 第二次触发，用另一组参数值（证明参数不是固定的）")
    print("  param_overrides = {pa: second-A, pb: second-B, pc: second-C}")
    print(f"{'='*60}")

    user_params_2 = {"pa": "second-A", "pb": "second-B", "pc": "second-C"}
    code, data = api("POST", f"/api/v1/workflows/{wf_id}/trigger",
                     token=token, body={"param_overrides": user_params_2})
    if code != 200:
        print(f"  触发失败: {data}")
        sys.exit(1)
    inst_2 = data["data"].get("instance_id") or data["data"].get("id")
    print(f"  实例 2: {inst_2[:8]}")

    tis = wait_terminal(token, inst_2)
    print(f"\n  执行结果:")
    expected_2 = {"nA": "second-A", "nB": "second-B", "nC": "second-C"}
    all_match_2 = True
    for ti in sorted(tis, key=lambda x: x["node_id"]):
        node = ti["node_id"]
        log = get_log(token, inst_2, ti["id"])
        exp = expected_2.get(node, "?")
        match = f"param-{['pa','pb','pc'][['nA','nB','nC'].index(node)]}={exp}" in log
        mark = "OK" if match else "MISMATCH"
        if not match:
            all_match_2 = False
        print(f"    {node} [{ti['status']}/{mark}]: {log}")
    print(f"  [结论] {'✓ 第二次触发的参数值与第一次不同，参数是运行时的' if all_match_2 else '✗ 未生效'}")

    # ============================================================
    # 步骤 5: 第三次触发 — 不传参数，验证回退到 default
    # ============================================================
    print(f"\n{'='*60}")
    print("步骤 5: 第三次触发，不传参数（验证回退到 task default）")
    print(f"{'='*60}")

    code, data = api("POST", f"/api/v1/workflows/{wf_id}/trigger",
                     token=token, body={})
    if code != 200:
        print(f"  触发失败: {data}")
        sys.exit(1)
    inst_3 = data["data"].get("instance_id") or data["data"].get("id")
    print(f"  实例 3: {inst_3[:8]}")

    tis = wait_terminal(token, inst_3)
    print(f"\n  执行结果:")
    expected_3 = {"nA": "default-a", "nB": "default-b", "nC": "default-c"}
    all_match_3 = True
    for ti in sorted(tis, key=lambda x: x["node_id"]):
        node = ti["node_id"]
        log = get_log(token, inst_3, ti["id"])
        exp = expected_3.get(node, "?")
        match = f"param-{['pa','pb','pc'][['nA','nB','nC'].index(node)]}={exp}" in log
        mark = "OK" if match else "MISMATCH"
        if not match:
            all_match_3 = False
        print(f"    {node} [{ti['status']}/{mark}]: {log}")
    print(f"  [结论] {'✓ 不传参数时回退到 task default' if all_match_3 else '✗ 未回退'}")

    # ============================================================
    # 汇总
    # ============================================================
    print(f"\n{'='*60}")
    print("汇总")
    print(f"{'='*60}")
    print(f"  场景 1 (运行时参数 pa=user-val-A, pb=user-val-B, pc=user-val-C): "
          f"{'通过' if all_match_1 else '失败'}")
    print(f"  场景 2 (运行时参数 pa=second-A, pb=second-B, pc=second-C):     "
          f"{'通过' if all_match_2 else '失败'}")
    print(f"  场景 3 (不传参数，回退 default):                                "
          f"{'通过' if all_match_3 else '失败'}")
    print()
    if all_match_1 and all_match_2 and all_match_3:
        print("  最终结论: 需求完全满足！")
        print("  - 工作流 3 个任务各自有独立参数 pa/pb/pc")
        print("  - 触发时用户输入三个参数值，运行时按输入值执行")
        print("  - 每次触发可用不同值，参数不是固定的")
        print("  - 不传参数时回退到 task 定义的 default")
        print()
        print("  实现方式: 触发时传全局 param_overrides 包含所有任务的参数：")
        print('    POST /api/v1/workflows/{id}/trigger')
        print('    {"param_overrides": {"pa": "值A", "pb": "值B", "pc": "值C"}}')
        print()
        print("  原理: 每个任务 command 只引用自己的 ${pa}/${pb}/${pc}，")
        print("        全局 param_overrides 对所有任务合并，但占位符只替换各自引用的，")
        print("        互不干扰，等效于 per-task 参数。")
    else:
        print("  最终结论: 部分场景未通过，请检查上方输出")


if __name__ == "__main__":
    main()
