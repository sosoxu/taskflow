#!/usr/bin/env python3
"""
验证多任务工作流的分发行为与日志查看。

测试场景：
  - 启动 2 个 worker（50052, 50053）
  - 创建一个含 3 个并行任务（A, B, C 无依赖）的工作流，策略 load_balance
  - 触发后观察每个任务被分发到哪个 worker（验证是否分散）
  - 创建一个含串行依赖 A→B→C 的工作流，策略 random
  - 观察任务分布（验证依赖是否影响 worker 选择）
  - 对完成的任务调用日志接口，验证日志按 workflow_instance 子目录组织
"""
import json
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error

API = "http://localhost:8080"
BACKEND = "/workspace/taskflow/backend"
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


def wait_status(token, instance_id, expect_all_terminal=True, timeout=60):
    """轮询实例状态，返回 task_instances 列表。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        code, data = api("GET", f"/api/v1/instances/{instance_id}", token=token)
        if code != 200:
            time.sleep(2)
            continue
        tis = data["data"].get("task_instances", [])
        if not tis:
            time.sleep(2)
            continue
        if expect_all_terminal:
            terminal = {"SUCCESS", "FAILED", "TIMEOUT", "CANCELLED",
                        "NODE_OFFLINE", "UPSTREAM_FAILED"}
            if all(t["status"] in terminal for t in tis):
                return tis
        time.sleep(2)
    return tis


def main():
    # 登录
    code, data = api("POST", "/api/v1/auth/login",
                     body={"username": "admin", "password": "admin123"})
    if code != 200:
        print(f"admin 登录失败: {data}")
        sys.exit(1)
    token = data["data"]["access_token"]
    print("admin 登录成功")

    # 检查 worker
    code, data = api("GET", "/api/v1/workers", token=token)
    workers = data["data"]["items"]
    online = [w for w in workers if w["status"] == "online"]
    print(f"在线 worker 数: {len(online)}")
    for w in online:
        print(f"  {w['name']} id={w['id'][:8]} addr={w['address']}")

    if len(online) < 2:
        print("\n[警告] 在线 worker 不足 2 个，仍可测试但无法充分验证分散性")

    # ---------- 场景 1: 并行任务 + load_balance ----------
    print(f"\n{'='*60}\n场景 1: 3 个并行任务（无依赖），策略 load_balance\n{'='*60}")

    # 创建 3 个任务
    task_ids = []
    for label in ["A", "B", "C"]:
        code, data = api("POST", "/api/v1/tasks", token=token, body={
            "name": f"multi-par-{label}-{TS}", "type": "command",
            "config": {"command": f"echo 'task {label} on worker'; sleep 2"},
            "timeout": 30, "max_retries": 0, "retry_interval": 0,
        })
        if code == 200:
            task_ids.append((label, data["data"]["id"]))
            print(f"  任务 {label}: {data['data']['id'][:8]}")
        else:
            print(f"  任务 {label} 创建失败: {data}")
            sys.exit(1)

    # 创建工作流（3 个并行节点，无 edges）
    dag = {
        "nodes": [
            {"id": "nA", "task_id": task_ids[0][1]},
            {"id": "nB", "task_id": task_ids[1][1]},
            {"id": "nC", "task_id": task_ids[2][1]},
        ],
        "edges": [],
    }
    code, data = api("POST", "/api/v1/workflows", token=token, body={
        "name": f"multi-par-wf-{TS}", "dag": dag,
        "schedule_strategy": "load_balance",
    })
    if code != 200:
        print(f"  工作流创建失败: {data}")
        sys.exit(1)
    wf_id = data["data"]["id"]
    print(f"  工作流: {wf_id[:8]} (策略 load_balance)")

    # 触发
    code, data = api("POST", f"/api/v1/workflows/{wf_id}/trigger", token=token, body={})
    if code != 200:
        print(f"  触发失败: {data}")
        sys.exit(1)
    instance_id = data["data"].get("instance_id") or data["data"].get("id")
    print(f"  实例: {instance_id[:8]}")

    # 等待完成
    print("  等待任务完成...")
    tis = wait_status(token, instance_id, timeout=60)
    worker_set = set()
    print(f"\n  任务分布:")
    for ti in sorted(tis, key=lambda x: x["node_id"]):
        wid = ti.get("worker_id", "") or "(无)"
        print(f"    {ti['node_id']}: status={ti['status']} worker={wid[:8]} task={ti['task_name']}")
        if wid and wid != "(无)":
            worker_set.add(wid[:8])

    if len(worker_set) > 1:
        print(f"  [结论] 3 个并行任务分散到 {len(worker_set)} 个 worker: {worker_set}")
    elif len(worker_set) == 1:
        print(f"  [结论] 3 个并行任务都在同一 worker: {worker_set}（可能在线 worker 只有 1 个）")
    else:
        print(f"  [结论] 未观察到 worker 分配")

    # ---------- 场景 2: 串行依赖 A→B→C + random ----------
    print(f"\n{'='*60}\n场景 2: 串行依赖 A→B→C，策略 random\n{'='*60}")

    task_ids2 = []
    for label in ["A", "B", "C"]:
        code, data = api("POST", "/api/v1/tasks", token=token, body={
            "name": f"multi-seq-{label}-{TS}", "type": "command",
            "config": {"command": f"echo 'seq task {label}'"},
            "timeout": 30, "max_retries": 0, "retry_interval": 0,
        })
        task_ids2.append((label, data["data"]["id"]))

    dag2 = {
        "nodes": [
            {"id": "nA", "task_id": task_ids2[0][1]},
            {"id": "nB", "task_id": task_ids2[1][1]},
            {"id": "nC", "task_id": task_ids2[2][1]},
        ],
        "edges": [
            {"source": "nA", "target": "nB"},
            {"source": "nB", "target": "nC"},
        ],
    }
    code, data = api("POST", "/api/v1/workflows", token=token, body={
        "name": f"multi-seq-wf-{TS}", "dag": dag2,
        "schedule_strategy": "random",
    })
    wf_id2 = data["data"]["id"]
    print(f"  工作流: {wf_id2[:8]} (策略 random, A→B→C)")

    code, data = api("POST", f"/api/v1/workflows/{wf_id2}/trigger", token=token, body={})
    instance_id2 = data["data"].get("instance_id") or data["data"].get("id")
    print(f"  实例: {instance_id2[:8]}")

    tis2 = wait_status(token, instance_id2, timeout=60)
    worker_set2 = {}
    print(f"\n  任务分布（按执行顺序）:")
    # 按 started_at 排序
    tis2_sorted = sorted(tis2, key=lambda x: x.get("started_at") or "9999")
    for ti in tis2_sorted:
        wid = ti.get("worker_id", "") or "(无)"
        print(f"    {ti['node_id']}: status={ti['status']} worker={wid[:8]} started={ti.get('started_at','')[:19]}")
        if wid and wid != "(无)":
            worker_set2[ti["node_id"]] = wid[:8]

    distinct_workers = set(worker_set2.values())
    print(f"  [结论] 串行任务用到 {len(distinct_workers)} 个 worker: {distinct_workers}")
    print(f"  [说明] DAG 依赖只控制时序（B 等 A 完成），不约束位置（B 可能在不同 worker）")

    # ---------- 场景 3: 日志查看 ----------
    print(f"\n{'='*60}\n场景 3: 日志查看验证\n{'='*60}")

    # 对场景 1 的每个任务查日志
    print(f"  工作流实例: {instance_id[:8]}")
    for ti in sorted(tis, key=lambda x: x["node_id"]):
        ti_id = ti["id"]
        node = ti["node_id"]
        code, data = api("GET",
                         f"/api/v1/instances/{instance_id}/tasks/{ti_id}/logs",
                         token=token)
        if code == 200:
            log_text = data["data"].get("log", "")
            log_lines = log_text.strip().split("\n") if log_text.strip() else []
            print(f"  {node} ({ti_id[:8]}) 日志 ({len(log_lines)} 行):")
            for line in log_lines[:3]:
                print(f"      {line}")
        else:
            print(f"  {node} ({ti_id[:8]}) 日志获取失败: HTTP {code} {data}")

    # 验证 worker 端日志目录结构
    print(f"\n  Worker 端日志目录结构 (logs/tasks/{instance_id[:8]}*/):")
    for worker_dir in ["logs/tasks", "/workspace/taskflow/backend/logs/tasks"]:
        if os.path.exists(worker_dir):
            result = subprocess.run(
                ["find", worker_dir, "-name", "*.log", "-type", "f"],
                capture_output=True, text=True, timeout=5)
            for line in result.stdout.strip().split("\n")[:6]:
                if line:
                    print(f"    {line}")
            break

    print(f"\n{'='*60}\n测试完成\n{'='*60}")


if __name__ == "__main__":
    main()
