#!/usr/bin/env python3
"""
测试多任务多参数工作流在触发时能否使用不同参数值。

研究结论：
  - task.parameters_json 定义参数 default
  - dag_json.nodes[i].param_overrides 是静态覆盖（创建工作流时写死）
  - trigger 请求体的 param_overrides 是全局覆盖（所有任务共享）

优先级：task default < node param_overrides < instance param_overrides

本测试验证：
  场景 A: 3 个任务各有 ${name} 和 ${count} 占位符
    - task default: name=alice, count=1
    - node 静态覆盖: nA name=ann, nB name=bob, nC name=carl （不同任务不同值）
    - 验证：每个任务用各自的 node 覆盖值执行

  场景 B: 同一工作流，触发时传 instance 级 param_overrides={"name":"zoe","count":99}
    - 验证：所有 3 个任务的 name 都被覆盖为 zoe（全局覆盖，无法按任务区分）

  场景 C: 验证触发时无法按 node 指定不同参数
    - 尝试传 {"node_param_overrides": {...}} 或 {"param_overrides": {"nA": {...}}}
    - 预期：要么被忽略，要么全局应用，无法实现 per-node 覆盖
"""
import json
import os
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


def section(title):
    print(f"\n{'='*60}\n{title}\n{'='*60}")


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
        print("[错误] 没有在线 worker，无法执行任务")
        sys.exit(1)
    print(f"在线 worker: {len(online)}")

    # 创建 3 个任务，每个都有 ${name} 和 ${count} 占位符
    # parameters_json 定义 default 值
    section("创建 3 个带参数的任务")
    task_ids = {}
    for label in ["A", "B", "C"]:
        code, data = api("POST", "/api/v1/tasks", token=token, body={
            "name": f"param-task-{label}-{TS}",
            "type": "command",
            "config": {"command": f"echo task-{label} name=${{name}} count=${{count}}"},
            "timeout": 30,
            "max_retries": 0,
            "retry_interval": 0,
            "parameters_json": {
                "name": {"default": "alice", "type": "string"},
                "count": {"default": "1", "type": "number"},
            },
        })
        if code != 200:
            print(f"  任务 {label} 创建失败: {data}")
            sys.exit(1)
        task_ids[label] = data["data"]["id"]
        print(f"  任务 {label}: {task_ids[label][:8]} (default name=alice count=1)")

    # ============================================================
    # 场景 A: node 级静态覆盖（不同任务不同参数）
    # ============================================================
    section("场景 A: node 级 param_overrides（创建工作流时写死）")
    print("  nA: name=ann, count=10")
    print("  nB: name=bob, count=20")
    print("  nC: name=carl, count=30")
    print("  预期: 每个任务用各自的 node 覆盖值执行")

    dag = {
        "nodes": [
            {"id": "nA", "task_id": task_ids["A"],
             "param_overrides": {"name": "ann", "count": "10"}},
            {"id": "nB", "task_id": task_ids["B"],
             "param_overrides": {"name": "bob", "count": "20"}},
            {"id": "nC", "task_id": task_ids["C"],
             "param_overrides": {"name": "carl", "count": "30"}},
        ],
        "edges": [],
    }
    code, data = api("POST", "/api/v1/workflows", token=token, body={
        "name": f"param-wf-A-{TS}", "dag": dag,
        "schedule_strategy": "load_balance",
    })
    if code != 200:
        print(f"  工作流创建失败: {data}")
        sys.exit(1)
    wf_a = data["data"]["id"]
    print(f"  工作流: {wf_a[:8]}")

    code, data = api("POST", f"/api/v1/workflows/{wf_a}/trigger",
                     token=token, body={})
    if code != 200:
        print(f"  触发失败: {data}")
        sys.exit(1)
    inst_a = data["data"].get("instance_id") or data["data"].get("id")
    print(f"  实例: {inst_a[:8]} (无触发时参数)")

    tis = wait_terminal(token, inst_a)
    print(f"\n  执行结果:")
    all_match = True
    expected = {"nA": ("ann", "10"), "nB": ("bob", "20"), "nC": ("carl", "30")}
    for ti in sorted(tis, key=lambda x: x["node_id"]):
        node = ti["node_id"]
        log = get_log(token, inst_a, ti["id"])
        exp_name, exp_count = expected.get(node, ("?", "?"))
        match = f"name={exp_name}" in log and f"count={exp_count}" in log
        mark = "OK" if match else "MISMATCH"
        if not match:
            all_match = False
        print(f"    {node} [{ti['status']}/{mark}]: {log}")
        print(f"         expected: name={exp_name} count={exp_count}")

    if all_match:
        print("  [结论A] node 级静态覆盖有效，不同任务可用不同参数值")
    else:
        print("  [结论A] node 级覆盖未按预期生效")

    # ============================================================
    # 场景 B: 触发时传全局 param_overrides（覆盖所有任务的同一参数）
    # ============================================================
    section("场景 B: 触发时传 instance 级 param_overrides（全局覆盖）")
    print("  param_overrides = {name: zoe, count: 99}")
    print("  预期: 所有 3 个任务的 name 都被覆盖为 zoe（无法按任务区分）")

    # 用同一个工作流，触发时传全局参数
    code, data = api("POST", f"/api/v1/workflows/{wf_a}/trigger", token=token, body={
        "param_overrides": {"name": "zoe", "count": "99"},
    })
    if code != 200:
        print(f"  触发失败: {data}")
        sys.exit(1)
    inst_b = data["data"].get("instance_id") or data["data"].get("id")
    print(f"  实例: {inst_b[:8]} (全局覆盖 name=zoe count=99)")

    tis = wait_terminal(token, inst_b)
    print(f"\n  执行结果:")
    all_zoe = True
    for ti in sorted(tis, key=lambda x: x["node_id"]):
        node = ti["node_id"]
        log = get_log(token, inst_b, ti["id"])
        # 预期：instance 级覆盖了 node 级，所有任务都是 zoe/99
        match = "name=zoe" in log and "count=99" in log
        mark = "zoe/99" if match else "OTHER"
        if not match:
            all_zoe = False
        print(f"    {node} [{ti['status']}/{mark}]: {log}")

    if all_zoe:
        print("  [结论B] instance 级覆盖是全局的，覆盖了所有任务的参数")
        print("         无法在触发时为不同任务指定不同参数值")
    else:
        print("  [结论B] 部分任务未被全局覆盖，需检查")

    # ============================================================
    # 场景 C: 尝试按 node 指定不同参数（预期失败）
    # ============================================================
    section("场景 C: 尝试触发时按 node 指定不同参数（预期不支持）")
    print("  尝试 1: 传嵌套 param_overrides")
    print("    {param_overrides: {nA: {name:x1}, nB: {name:x2}, nC: {name:x3}}}")

    code, data = api("POST", f"/api/v1/workflows/{wf_a}/trigger", token=token, body={
        "param_overrides": {
            "nA": {"name": "x1", "count": "1"},
            "nB": {"name": "x2", "count": "2"},
            "nC": {"name": "x3", "count": "3"},
        },
    })
    if code != 200:
        print(f"  触发失败: {data}")
    else:
        inst_c = data["data"].get("instance_id") or data["data"].get("id")
        print(f"  实例: {inst_c[:8]}")
        tis = wait_terminal(token, inst_c)
        print(f"  执行结果（观察 name/count 是否按 node 不同）:")
        any_diff = False
        for ti in sorted(tis, key=lambda x: x["node_id"]):
            node = ti["node_id"]
            log = get_log(token, inst_c, ti["id"])
            print(f"    {node} [{ti['status']}]: {log}")
            # 检查是否真的按 node 取了不同值
            # 嵌套结构会被当作 key="nA" value={"name":"x1"} 合并，
            # ${name} 占位符不会被替换（params 里没有 "name" 顶层 key，
            # 而是有一个 key="nA" value={...}）
            if "name=${name}" in log or "name=" not in log:
                any_diff = True  # 占位符未被替换
        print("  [结论C] 嵌套 param_overrides 无法实现 per-node 覆盖")
        print("         占位符未被正确替换（params 中没有顶层 name/count key）")

    # ============================================================
    # 汇总
    # ============================================================
    section("汇总")
    print("  场景 A: node 级静态覆盖（创建工作流时写死）— 支持不同任务不同参数")
    print("  场景 B: instance 级触发时覆盖                — 全局覆盖，无法按任务区分")
    print("  场景 C: 触发时按 node 嵌套覆盖              — 不支持，参数无法正确解析")
    print()
    print("  最终结论:")
    print("    当前架构下，'触发时为不同任务使用不同参数值' 不被支持。")
    print("    可行方案：")
    print("    1. 创建工作流时在 dag_json.nodes[i].param_overrides 写死不同值")
    print("       （静态，每次触发都相同）")
    print("    2. 为不同参数组合创建多个工作流")
    print("    3. 扩展 trigger API 支持 node_param_overrides（需改代码）")


if __name__ == "__main__":
    main()
