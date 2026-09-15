#!/usr/bin/env bash
# Fix #344: 生成自签证书，用于 TLS 部署自测。
#
# 生产请换成正式证书（Let's Encrypt 或已有证书），把 server.crt / server.key
# 放进 certs/ 目录即可——docker-compose.tls.yml 就是这么挂载的。
#
# 用法：
#   bash scripts/gen-self-signed-cert.sh                 # CN/SAN = localhost
#   bash scripts/gen-self-signed-cert.sh taskflow.example.com
#   bash scripts/gen-self-signed-cert.sh 192.168.1.10 example.com
set -euo pipefail

HOSTS=("$@")
if [ ${#HOSTS[@]} -eq 0 ]; then
    HOSTS=(localhost)
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${REPO_ROOT}/certs"
mkdir -p "${OUT_DIR}"

if ! command -v openssl >/dev/null 2>&1; then
    echo "错误: 未找到 openssl，请先安装（或改用正式证书）" >&2
    exit 1
fi

# 纯数字与点的按 IP 处理，其余按 DNS 名处理
san=""
for h in "${HOSTS[@]}"; do
    case "${h}" in
        *[!0-9.]*) san="${san}DNS:${h}," ;;
        *)         san="${san}IP:${h}," ;;
    esac
done
san="${san%,}"

openssl req -x509 -nodes -newkey rsa:2048 -days 365 \
    -keyout "${OUT_DIR}/server.key" \
    -out "${OUT_DIR}/server.crt" \
    -subj "/CN=${HOSTS[0]}" \
    -addext "subjectAltName=${san}" \
    2>/dev/null

chmod 600 "${OUT_DIR}/server.key"
chmod 644 "${OUT_DIR}/server.crt"

echo "已生成自签证书:"
echo "  证书: ${OUT_DIR}/server.crt"
echo "  私钥: ${OUT_DIR}/server.key"
echo "  SAN : ${san}"
echo
echo "启动 TLS 部署:"
echo "  TASKFLOW_HSTS_MAX_AGE=0 docker compose -f docker-compose.yml -f docker-compose.tls.yml up -d"
echo "（自签环境用 0 关闭 HSTS，避免浏览器把该主机永久锁定到 HTTPS）"
