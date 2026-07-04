#!/usr/bin/env python3
"""测试 scheduler/worker 异常退出后的恢复行为。"""

import json
import os
import subprocess
import sys
import time
import urllib.request

API = "http://localhost:8080"
TOKEN = None
BACKEND = "/workspace/taskflow/backend"


def api(method, path, body=None):
    url = f"{API}{path}"
    data = json.dumps(body).encode() if body else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if TOKEN:
        req.add_header("Authorization", f"Bearer {TOKEN}")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        return json.loads(e.read())


def login():
    global TOKEN
    r = api("POST", "/api/v1/auth/login", {"username": "admin", "password": "admin123"})
    TOKEN = r["data"]["access_token"]
    print(f"[OK] 登录成功")


def create_task(name, command, timeout=60, max_retries=0):
    r = api("POST", "/api/v1/tasks", {
        "name": name, "type": "command",
        "config": {"command": command},
        "timeout": timeout, "max_retries": max_retries,
    })
    tid = r.get("data", {}).get("id", "")
    print(f"[OK] 创建任务 '{name}': {tid[:8]}... timeout={timeout}s retries={max_retries}")
    return tid


def create_workflow(name, task_id):
    dag = {"nodes": [{"id": "n1", "task_id": task_id}], "edges": []}
    r = api("POST", "/api/v1/workflows", {"name": name, "dag": dag})
    wid = r.get("data", {}).get("id", "")
    print(f"[OK] 创建工作流 '{name}': {wid[:8]}...")
    return wid


def trigger(wid):
    r = api("POST", f"/api/v1/workflows/{wid}/trigger")
    iid = r.get("data", {}).get("instance_id") or r.get("data", {}).get("id", "")
    print(f"[OK] 触发工作流: instance={iid[:8]}...")
    return iid


def get_instance(iid):
    r = api("GET", f"/api/v1/instances/{iid}")
    return r.get("data", {})


def get_task_instance(iid):
    """获取实例中第一个 task_instance 的状态。"""
    data = get_instance(iid)
    tis = data.get("task_instances", [])
    if not tis:
        return None
    return tis[0]


def wait_status(iid, target_statuses, timeout=30):
    """轮询等待 task_instance 到达目标状态。"""
    start = time.time()
    while time.time() - start < timeout:
        ti = get_task_instance(iid)
        if ti and ti.get("status") in target_statuses:
            return ti
        time.sleep(1)
    return get_task_instance(iid)


def test_worker_crash_with_running_task():
    """测试 1: worker 崩溃后，正在运行的长任务命运。"""
    print("\n" + "=" * 70)
    print("测试 1: Worker 崩溃 (kill -9) 后正在运行的任务")
    print("=" * 70)

    # 创建一个 sleep 300 秒的长任务
    task_id = create_task("recovery-test-long", "sleep 300", timeout=3600)
    wf_id = create_workflow("recovery-test-wf-long", task_id)
    instance_id = trigger(wf_id)

    # 等待任务进入 RUNNING 状态
    print("\n等待任务进入 RUNNING 状态...")
    ti = wait_status(instance_id, {"RUNNING"}, timeout=15)
    if ti and ti.get("status") == "RUNNING":
        print(f"[OK] 任务已 RUNNING, worker={ti.get('worker_id', '')[:8]}...")
    else:
        print(f"[FAIL] 任务未进入 RUNNING，当前状态: {ti.get('status') if ti else 'None'}")
        return

    # kill -9 worker 进程
    print("\n--- kill -9 worker 进程 ---")
    os.system("pkill -9 -f './build/bin/worker'")
    print("[OK] worker 已被 kill -9")

    # 等待 35 秒（心跳超时 30s + 余量）
    print("等待 35 秒（心跳超时 30s + 余量）...")
    time.sleep(35)

    # 检查 task_instance 状态
    ti = get_task_instance(instance_id)
    status = ti.get("status") if ti else "None"
    print(f"\n任务状态: {status}")
    if status == "NODE_OFFLINE":
        print("[OK] scheduler 正确检测到 worker 离线，任务标记为 NODE_OFFLINE")
    else:
        print(f"[注意] 预期 NODE_OFFLINE，实际 {status}")

    # 检查 worker 状态
    r = api("GET", "/api/v1/workers")
    workers = r.get("data", {}).get("items", [])
    for w in workers:
        print(f"  Worker: {w.get('name', '')} status={w.get('status', '')}")

    # 检查孤儿子进程是否还在运行
    print("\n检查孤儿子进程 (sleep 300)...")
    result = subprocess.run(["pgrep", "-f", "sleep 300"], capture_output=True, text=True)
    if result.stdout.strip():
        print(f"[BUG] 孤儿子进程仍在运行! PID: {result.stdout.strip()}")
        os.system("pkill -f 'sleep 300'")  # 清理
    else:
        print("[OK] 无孤儿子进程")

    # 检查 NODE_OFFLINE 是否会自动重试
    if status == "NODE_OFFLINE" and ti.get("max_retries", 0) > 0:
        print("等待 10 秒看是否自动重试...")
        time.sleep(10)
        ti2 = get_task_instance(instance_id)
        s2 = ti2.get("status") if ti2 else "None"
        if s2 == "PENDING" or s2 == "DISPATCHED":
            print(f"[OK] 任务已自动重试: {s2}")
        else:
            print(f"[BUG] NODE_OFFLINE 未自动重试，仍为 {s2}")

    return instance_id, status


def test_scheduler_crash_with_running_task(instance_id_1):
    """测试 2: scheduler 崩溃后恢复。"""
    print("\n" + "=" * 70)
    print("测试 2: Scheduler 崩溃 (kill -9) 后恢复")
    print("=" * 70)

    # 先重启 worker（如果还没运行）
    if not subprocess.run(["pgrep", "-f", "./build/bin/worker"], capture_output=True).stdout:
        subprocess.Popen(["./build/bin/worker"], cwd=BACKEND,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(3)
        print("[OK] worker 已重启")

    # 创建一个快速完成的任务
    task_id = create_task("recovery-test-fast", "echo scheduler-recovery-ok", timeout=60)
    wf_id = create_workflow("recovery-test-wf-fast", task_id)
    instance_id = trigger(wf_id)

    # 等待任务进入 RUNNING
    print("\n等待任务进入 RUNNING...")
    ti = wait_status(instance_id, {"RUNNING"}, timeout=10)
    if ti and ti.get("status") == "RUNNING":
        print(f"[OK] 任务已 RUNNING")
    else:
        s = ti.get("status") if ti else "None"
        print(f"[INFO] 任务状态: {s}（可能已完成）")

    # kill -9 scheduler
    print("\n--- kill -9 scheduler 进程 ---")
    os.system("pkill -9 -f './build/bin/scheduler'")
    print("[OK] scheduler 已被 kill -9")

    # 等几秒让进程完全退出
    time.sleep(3)

    # 验证 API 不可用
    try:
        api("GET", "/api/v1/health")
        print("[FAIL] scheduler API 仍然可用")
    except Exception:
        print("[OK] scheduler API 已不可用")

    # 重启 scheduler
    print("\n--- 重启 scheduler ---")
    subprocess.Popen(["./build/bin/scheduler"], cwd=BACKEND,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(4)

    # 验证 API 恢复
    try:
        r = api("GET", "/api/v1/health")
        if r.get("data", {}).get("status") == "ok":
            print("[OK] scheduler API 已恢复")
        else:
            print(f"[FAIL] scheduler 恢复异常: {r}")
    except Exception as e:
        print(f"[FAIL] scheduler 未恢复: {e}")
        return

    # 重新登录（token 可能还在）
    login()

    # 检查之前的实例状态
    print("\n检查重启前触发的实例状态...")
    ti = get_task_instance(instance_id)
    if ti:
        s = ti.get("status", "")
        print(f"  快速任务实例状态: {s}")
        if s in ("SUCCESS", "FAILED", "TIMEOUT"):
            print(f"[OK] 任务最终状态: {s}（scheduler 重启后 DagDriver 继续推进）")
        elif s in ("RUNNING", "DISPATCHED"):
            print(f"[INFO] 任务仍在 {s}，等待 10 秒...")
            time.sleep(10)
            ti2 = get_task_instance(instance_id)
            print(f"  10 秒后状态: {ti2.get('status', '') if ti2 else 'None'}")

    # 检查测试 1 的实例（NODE_OFFLINE 的那个）
    if instance_id_1:
        ti1 = get_task_instance(instance_id_1)
        if ti1:
            s1 = ti1.get("status", "")
            print(f"  测试1的长任务实例状态: {s1}（scheduler 重启后不变）")


def test_worker_graceful_shutdown():
    """测试 3: worker 优雅退出 (SIGTERM)。"""
    print("\n" + "=" * 70)
    print("测试 3: Worker 优雅退出 (SIGTERM) 后正在运行的任务")
    print("=" * 70)

    # 确保两个进程都在运行
    if not subprocess.run(["pgrep", "-f", "./build/bin/scheduler"], capture_output=True).stdout:
        subprocess.Popen(["./build/bin/scheduler"], cwd=BACKEND,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(3)
    if not subprocess.run(["pgrep", "-f", "./build/bin/worker"], capture_output=True).stdout:
        subprocess.Popen(["./build/bin/worker"], cwd=BACKEND,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(3)
    login()

    # 创建一个 sleep 60 的任务（会被 worker shutdown 时的 cancel 杀掉）
    task_id = create_task("recovery-test-graceful", "sleep 60", timeout=3600)
    wf_id = create_workflow("recovery-test-wf-graceful", task_id)
    instance_id = trigger(wf_id)

    # 等待 RUNNING
    print("\n等待任务进入 RUNNING...")
    ti = wait_status(instance_id, {"RUNNING"}, timeout=15)
    if ti and ti.get("status") == "RUNNING":
        print(f"[OK] 任务已 RUNNING")
    else:
        print(f"[INFO] 任务状态: {ti.get('status') if ti else 'None'}")

    # 发送 SIGTERM 给 worker
    print("\n--- kill -TERM worker 进程 ---")
    os.system("pkill -TERM -f './build/bin/worker'")
    print("[OK] 已发送 SIGTERM")

    # 等待 worker 优雅退出（最多 35s drain + cancel）
    print("等待 40 秒（worker 优雅退出 drain 30s + cancel）...")
    time.sleep(40)

    # 检查任务状态
    ti = get_task_instance(instance_id)
    s = ti.get("status") if ti else "None"
    print(f"\n任务状态: {s}")
    if s in ("FAILED", "CANCELLED", "TIMEOUT"):
        print(f"[OK] 优雅退出后任务正确终止: {s}")
    elif s == "RUNNING":
        print(f"[BUG] 任务仍为 RUNNING（worker 上报失败？）")
    else:
        print(f"[INFO] 任务状态: {s}")

    # 检查 worker 是否已 deregister（状态 offline）
    r = api("GET", "/api/v1/workers")
    workers = r.get("data", {}).get("items", [])
    for w in workers:
        print(f"  Worker: {w.get('name', '')} status={w.get('status', '')}")


def main():
    print("=" * 70)
    print("TaskFlow 异常恢复机制测试")
    print("=" * 70)

    # 等待服务就绪
    for _ in range(10):
        try:
            r = api("GET", "/api/v1/health")
            if r.get("data", {}).get("status") == "ok":
                break
        except Exception:
            pass
        time.sleep(1)

    login()

    # 测试 1: worker 崩溃
    result_1 = test_worker_crash_with_running_task()
    instance_id_1 = result_1[0] if result_1 else None

    # 测试 2: scheduler 崩溃后恢复
    test_scheduler_crash_with_running_task(instance_id_1)

    # 测试 3: worker 优雅退出
    test_worker_graceful_shutdown()

    print("\n" + "=" * 70)
    print("测试完成")
    print("=" * 70)

    # 清理孤立进程
    os.system("pkill -f 'sleep 300' 2>/dev/null")
    os.system("pkill -f 'sleep 60' 2>/dev/null")


if __name__ == "__main__":
    main()
