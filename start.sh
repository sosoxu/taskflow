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
        echo "构建 TaskFlow 镜像..."
        docker compose build
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
        echo "用法: $0 {start|stop|restart|build|logs|status|clean}"
        echo ""
        echo "  start    - 启动所有服务"
        echo "  stop     - 停止所有服务"
        echo "  restart  - 重启所有服务"
        echo "  build    - 构建镜像"
        echo "  logs     - 查看日志 (可指定服务名)"
        echo "  status   - 查看服务状态"
        echo "  clean    - 清理所有容器和数据卷"
        exit 1
        ;;
esac
