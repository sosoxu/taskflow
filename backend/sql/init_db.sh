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
# Fix #356: 经 psql 变量（stdin 插值）替代字符串拼接——密码含引号/
# 反斜杠时拼接会产生坏 SQL 或注入面。注意 psql 仅对 stdin/-f 输入做
# 变量插值，-c 不插值。
echo ">>> 创建用户 ${DB_USER} ..."
USER_EXISTS=$(psql -v db_user="$DB_USER" -Atq <<'SQL'
SELECT 1 FROM pg_roles WHERE rolname = :'db_user';
SQL
)
if [ "$USER_EXISTS" != "1" ]; then
    psql -v db_user="$DB_USER" -v db_pass="$DB_PASS" <<'SQL'
CREATE USER :"db_user" WITH LOGIN PASSWORD :'db_pass';
SQL
fi
echo "    用户已存在或创建成功"

# 2. 创建数据库
echo ">>> 创建数据库 ${DB_NAME} ..."
DB_EXISTS=$(psql -v db_name="$DB_NAME" -Atq <<'SQL'
SELECT 1 FROM pg_database WHERE datname = :'db_name';
SQL
)
if [ "$DB_EXISTS" != "1" ]; then
    psql -v db_user="$DB_USER" -v db_name="$DB_NAME" <<'SQL'
CREATE DATABASE :"db_name" OWNER :"db_user";
SQL
fi
echo "    数据库已存在或创建成功"

# 3. 授权
echo ">>> 授权 ..."
psql -v db_user="$DB_USER" -v db_name="$DB_NAME" <<'SQL'
GRANT ALL PRIVILEGES ON DATABASE :"db_name" TO :"db_user";
SQL
psql -v db_user="$DB_USER" -d "${DB_NAME}" <<'SQL'
GRANT ALL PRIVILEGES ON SCHEMA public TO :"db_user";
SQL
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
