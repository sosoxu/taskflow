#!/bin/bash
# TaskFlow 一键启动脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== TaskFlow 分布式任务调度系统 ==="
echo ""

# 检查 Docker 和 Docker Compose
if ! command -v docker &> /dev/null; then
    echo "错误: 未安装 Docker"
    exit 1
fi

if ! docker compose version &> /dev/null; then
    echo "错误: 未安装 Docker Compose V2"
    exit 1
fi

case "${1:-}" in
    start)
        # Fix #142: Auto-build the deps base image if missing so
        # `./start.sh start` works without a manual `./start.sh deps` first.
        if ! docker image inspect taskflow-deps:latest &> /dev/null; then
            echo "依赖基础镜像不存在，自动构建 taskflow-deps:latest ..."
            docker build -f backend/Dockerfile.deps -t taskflow-deps:latest backend/
        fi
        echo "启动 TaskFlow 服务..."
        docker compose up -d
        echo ""
        echo "服务已启动:"
        echo "  前端:     http://localhost"
        echo "  API:      http://localhost:8080"
        echo "  gRPC:     localhost:50051"
        echo "  数据库:   localhost:5432"
        echo ""
        echo "默认管理员: admin / admin123"
        echo ""
        echo "多 Worker 扩展: docker compose up -d --scale worker=2"
        ;;
    stop)
        echo "停止 TaskFlow 服务..."
        docker compose down
        ;;
    restart)
        echo "重启 TaskFlow 服务..."
        docker compose restart
        ;;
    build)
        # Fix #142: Auto-build deps before building project images.
        if ! docker image inspect taskflow-deps:latest &> /dev/null; then
            echo "依赖基础镜像不存在，自动构建 taskflow-deps:latest ..."
            docker build -f backend/Dockerfile.deps -t taskflow-deps:latest backend/
        fi
        echo "构建 TaskFlow 镜像..."
        docker compose build
        ;;
    deps)
        echo "构建依赖基础镜像（只需执行一次）..."
        docker build -f backend/Dockerfile.deps -t taskflow-deps:latest backend/
        echo ""
        echo "依赖基础镜像构建完成: taskflow-deps:latest"
        echo "现在可以运行 ./start.sh build 构建项目镜像"
        ;;
    scale)
        # Fix #142: Multi-worker scaling helper (non-swarm).
        WORKER_COUNT="${2:-2}"
        # Fix #142: Auto-build the deps base image if missing.
        if ! docker image inspect taskflow-deps:latest &> /dev/null; then
            echo "依赖基础镜像不存在，自动构建 taskflow-deps:latest ..."
            docker build -f backend/Dockerfile.deps -t taskflow-deps:latest backend/
        fi
        echo "启动 TaskFlow 服务 (worker=${WORKER_COUNT})..."
        docker compose up -d --scale worker="${WORKER_COUNT}"
        echo ""
        echo "服务已启动，Worker 实例数: ${WORKER_COUNT}"
        ;;
    logs)
        docker compose logs -f ${2:-}
        ;;
    status)
        docker compose ps
        ;;
    clean)
        echo "清理所有容器和数据..."
        docker compose down -v
        ;;
    *)
        echo "用法: $0 {start|stop|restart|build|deps|scale|logs|status|clean}"
        echo ""
        echo "  start    - 启动所有服务（自动构建依赖镜像）"
        echo "  stop     - 停止所有服务"
        echo "  restart  - 重启所有服务"
        echo "  build    - 构建项目镜像（自动构建依赖镜像）"
        echo "  deps     - 单独构建依赖基础镜像（只需执行一次）"
        echo "  scale N  - 启动 N 个 Worker 实例（默认 2）"
        echo "  logs     - 查看日志 (可指定服务名)"
        echo "  status   - 查看服务状态"
        echo "  clean    - 清理所有容器和数据卷"
        exit 1
        ;;
esac
