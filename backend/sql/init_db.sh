#!/bin/bash
# TaskFlow 数据库初始化脚本
# 用于在新环境中创建 PostgreSQL 用户、数据库并执行建表
# 用法: sudo -u postgres bash init_db.sh

set -e

# psql 遇到已存在对象时的 ERROR 不会导致脚本中断
# schema_all.sql 中使用 CREATE TABLE IF NOT EXISTS 确保可重复执行

DB_USER="${DB_USER:-taskflow}"
DB_PASS="${DB_PASS:-taskflow123}"
DB_NAME="${DB_NAME:-taskflow}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== TaskFlow 数据库初始化 ==="
echo "用户: ${DB_USER}"
echo "数据库: ${DB_NAME}"
echo ""

# 检查是否以 postgres 用户运行
if [ "$(whoami)" != "postgres" ]; then
    echo "错误: 请以 postgres 用户运行此脚本"
    echo "  sudo -u postgres bash $0"
    exit 1
fi

# 1. 创建用户
echo ">>> 创建用户 ${DB_USER} ..."
psql -c "SELECT 1 FROM pg_roles WHERE rolname = '${DB_USER}'" | grep -q 1 || \
    psql -c "CREATE USER ${DB_USER} WITH LOGIN PASSWORD '${DB_PASS}';"
echo "    用户已存在或创建成功"

# 2. 创建数据库
echo ">>> 创建数据库 ${DB_NAME} ..."
psql -c "SELECT 1 FROM pg_database WHERE datname = '${DB_NAME}'" | grep -q 1 || \
    psql -c "CREATE DATABASE ${DB_NAME} OWNER ${DB_USER};"
echo "    数据库已存在或创建成功"

# 3. 授权
echo ">>> 授权 ..."
psql -d "${DB_NAME}" -c "GRANT ALL PRIVILEGES ON DATABASE ${DB_NAME} TO ${DB_USER};"
psql -d "${DB_NAME}" -c "GRANT ALL PRIVILEGES ON SCHEMA public TO ${DB_USER};"
echo "    授权完成"

# 4. 执行建表
echo ">>> 执行建表脚本 ..."
if [ -f "${SCRIPT_DIR}/schema_all.sql" ]; then
    psql -d "${DB_NAME}" -f "${SCRIPT_DIR}/schema_all.sql"
    echo "    建表完成"
else
    echo "    警告: 未找到 schema_all.sql，请手动执行建表"
fi

echo ""
echo "=== 初始化完成 ==="
echo "连接信息:"
echo "  Host:     localhost"
echo "  Port:     5432"
echo "  Database: ${DB_NAME}"
echo "  User:     ${DB_USER}"
echo "  Password: ${DB_PASS}"
