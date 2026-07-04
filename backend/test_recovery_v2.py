#!/usr/bin/env python3
"""精确测试 worker 崩溃后的心跳检测和 NODE_OFFLINE 自动重试。"""

import json
import os
import signal
import subprocess
import time
import urllib.request

TS = str(int(time.time()))

API = "http://localhost:8080"
BACKEND = "/workspace/taskflow/backend"
TOKEN = None


def api(method, path, body=None):
    url = f"{API}{path}"
    data = json.dumps(body).encode() if body else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if TOKEN:
        req.add_header("Authorization", f"Bearer {TOKEN}")
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


def main():
    global TOKEN

    # 确保服务运行
    if not subprocess.run(["pgrep", "-f", "./build/bin/scheduler"], capture_output=True).stdout:
        subprocess.Popen(["./build/bin/scheduler"], cwd=BACKEND,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(3)

    # 重启 worker
    os.system("pkill -9 -x worker 2>/dev/null")
    time.sleep(2)
    worker_proc = subprocess.Popen(["./build/bin/worker"], cwd=BACKEND,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    print(f"Worker PID: {worker_proc.pid}")

    # 登录
    r = api("POST", "/api/v1/auth/login", {"username": "admin", "password": "admin123"})
    TOKEN = r["data"]["access_token"]

    # 验证 worker online
    r = api("GET", "/api/v1/workers")
    for w in r["data"].get("items", []):
        print(f"Worker: {w['name']} status={w['status']}")

    # 创建带 max_retries=3 的长任务
    r = api("POST", "/api/v1/tasks", {
        "name": f"node-offline-retry-test-{TS}", "type": "command",
        "config": {"command": "sleep 300"}, "timeout": 3600,
        "max_retries": 3, "retry_interval": 5,
    })
    task_id = r["data"]["id"]
    print(f"\nTask created: {task_id[:8]}... max_retries=3 retry_interval=5")

    # 创建工作流并触发
    r = api("POST", "/api/v1/workflows", {
        "name": f"node-offline-wf-{TS}",
        "dag": {"nodes": [{"id": "n1", "task_id": task_id}], "edges": []},
    })
    wf_id = r["data"]["id"]
    r = api("POST", f"/api/v1/workflows/{wf_id}/trigger")
    instance_id = r["data"].get("instance_id") or r["data"].get("id")
    print(f"Instance: {instance_id[:8]}...")

    # 等待 RUNNING
    print("\nWaiting for RUNNING...")
    for i in range(15):
        time.sleep(1)
        r = api("GET", f"/api/v1/instances/{instance_id}")
        tis = r["data"].get("task_instances", [])
        status = tis[0]["status"] if tis else "NONE"
        if status == "RUNNING":
            print(f"  {i}s: RUNNING (worker={tis[0].get('worker_id','')[:8]}...)")
            break
        print(f"  {i}s: {status}")
    else:
        print(f"FAIL: task not RUNNING, status={status}")
        return

    # kill -9 worker (用 PID 精确 kill，避免 pkill -f 误杀 watchdog)
    print(f"\n--- kill -9 worker (PID {worker_proc.pid}) at {time.strftime('%H:%M:%S')} ---")
    os.kill(worker_proc.pid, signal.SIGKILL)

    # 检查孤儿子进程（等 3 秒给 watchdog 足够时间清理）
    time.sleep(3)
    result = subprocess.run(["pgrep", "-f", "sleep 300"], capture_output=True, text=True)
    # 过滤掉 bash/grep 自身
    pids = [p for p in result.stdout.strip().split('\n') if p and 'bash' not in p and 'grep' not in p and 'pgrep' not in p]
    if pids:
        print(f"[BUG] 孤儿子进程仍在运行! PID: {pids}")
        os.system("pkill -f 'sleep 300' 2>/dev/null")
    else:
        print("[OK] 无孤儿子进程（watchdog 已清理）")

    # 监控 50 秒
    print("\nMonitoring task & worker status for 50 seconds...")
    for i in range(5, 51, 5):
        time.sleep(5)
        r = api("GET", f"/api/v1/instances/{instance_id}")
        tis = r["data"].get("task_instances", [])
        ti_status = tis[0]["status"] if tis else "NONE"
        ti_retry = tis[0].get("retry_count", 0) if tis else 0

        r2 = api("GET", "/api/v1/workers")
        workers = r2["data"].get("items", [])
        wk_status = workers[0]["status"] if workers else "NONE"

        print(f"  {i:2d}s: task={ti_status} retry={ti_retry} worker={wk_status}")

        if ti_status == "NODE_OFFLINE":
            print(f"  >>> NODE_OFFLINE detected at {i}s!")

    # 再等 20 秒看是否自动重试
    print("\nWaiting 20s to check if NODE_OFFLINE auto-retries...")
    time.sleep(20)
    r = api("GET", f"/api/v1/instances/{instance_id}")
    tis = r["data"].get("task_instances", [])
    if tis:
        ti = tis[0]
        print(f"  Final: status={ti['status']} retry_count={ti.get('retry_count',0)}")
        if ti["status"] == "NODE_OFFLINE":
            print("  [BUG] NODE_OFFLINE 未自动重试（dag_driver.cpp retry 判断遗漏 NODE_OFFLINE）")
        elif ti["status"] in ("PENDING", "DISPATCHED", "RUNNING"):
            print("  [OK] NODE_OFFLINE 已自动重试")
        elif ti["status"] in ("FAILED", "SUCCESS"):
            print(f"  [INFO] 任务最终状态: {ti['status']}")

    # 清理
    os.system("pkill -f 'sleep 300' 2>/dev/null")


if __name__ == "__main__":
    main()
