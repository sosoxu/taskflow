#!/bin/bash
# TaskFlow 离线构建 - 第三方依赖源码下载脚本
# 在有网络的环境中运行此脚本，将所有依赖下载到 thirdparty/ 目录
# 然后将整个 backend/ 目录拷贝到离线环境即可构建
#
# 用法: ./download-thirdparty.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
THIRDPARTY_DIR="${SCRIPT_DIR}/thirdparty"
mkdir -p "${THIRDPARTY_DIR}"

echo "=== TaskFlow 离线构建依赖下载 ==="
echo "下载目录: ${THIRDPARTY_DIR}"
echo ""

# 下载函数，支持重试
download() {
    local url="$1"
    local output="$2"
    if [ -f "${THIRDPARTY_DIR}/${output}" ]; then
        echo "  [SKIP] ${output} 已存在"
        return 0
    fi
    echo "  [DOWN] ${output} ..."
    curl -L --retry 3 --retry-delay 5 -o "${THIRDPARTY_DIR}/${output}" "${url}"
    if [ $? -ne 0 ]; then
        echo "  [FAIL] 下载失败: ${url}"
        rm -f "${THIRDPARTY_DIR}/${output}"
        return 1
    fi
}

# ============================================================================
# 从源码构建的依赖（tarball 下载到 thirdparty/）
# ============================================================================

echo "--- 从源码构建的依赖 ---"

echo "[1/9] spdlog v1.14.1"
download https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz spdlog-v1.14.1.tar.gz

echo "[2/9] nlohmann_json v3.11.3"
download https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz json-v3.11.3.tar.gz

echo "[3/9] yaml-cpp 0.8.0"
download https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz yaml-cpp-0.8.0.tar.gz

echo "[4/9] jwt-cpp v0.7.0"
download https://github.com/Thalhammer/jwt-cpp/archive/refs/tags/v0.7.0.tar.gz jwt-cpp-v0.7.0.tar.gz

echo "[5/9] c-ares v1.30.0"
download https://github.com/c-ares/c-ares/archive/refs/tags/v1.30.0.tar.gz c-ares-v1.30.0.tar.gz

echo "[6/9] Drogon v1.8.7"
download https://github.com/drogonframework/drogon/archive/refs/tags/v1.8.7.tar.gz drogon-v1.8.7.tar.gz

echo "[7/9] trantor v1.5.20 (Drogon 依赖)"
download https://github.com/an-tao/trantor/archive/refs/tags/v1.5.20.tar.gz trantor-v1.5.20.tar.gz

echo "[8/9] jsoncpp 1.9.5 (Drogon 依赖)"
download https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/1.9.5.tar.gz jsoncpp-1.9.5.tar.gz

echo "[9/9] libpqxx 7.9.2"
download https://github.com/jtv/libpqxx/archive/refs/tags/7.9.2.tar.gz libpqxx-7.9.2.tar.gz

# ============================================================================
# gRPC 及其子模块依赖（离线编译 gRPC 所需）
# gRPC tarball 不包含子模块源码，需要单独下载每个子模块
# ============================================================================

echo ""
echo "--- gRPC 及其子模块依赖 ---"
echo ""

echo "[10/16] gRPC v1.65.5"
download https://github.com/grpc/grpc/archive/refs/tags/v1.65.5.tar.gz grpc-v1.65.5.tar.gz

echo "[11/16] Protobuf v28.3 (gRPC 子模块)"
download https://github.com/protocolbuffers/protobuf/archive/refs/tags/v28.3.tar.gz protobuf-v28.3.tar.gz

echo "[12/16] abseil-cpp 20240116.0 (gRPC 子模块，gRPC v1.65.5 锁定版本)"
download https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.0.tar.gz abseil-cpp-20240116.0.tar.gz

echo "[13/16] re2 (gRPC 子模块，使用 gRPC v1.65.5 锁定的 commit)"
download https://github.com/google/re2/archive/0c5616df9c0aaa44c9440d87422012423d91c7d1.tar.gz re2-0c5616d.tar.gz

echo "[14/16] zlib (gRPC 子模块，使用 gRPC v1.65.5 锁定的 commit)"
download https://github.com/madler/zlib/archive/09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851.tar.gz zlib-09155ea.tar.gz

echo "[15/16] googletest (gRPC 子模块，使用 gRPC v1.65.5 锁定的 commit)"
download https://github.com/google/googletest/archive/2dd1c131950043a8ad5ab0d2dda0e0970596586a.tar.gz googletest-2dd1c13.tar.gz

echo "[16/16] boringssl (gRPC 子模块，使用 gRPC v1.65.5 锁定的 commit)"
download https://github.com/google/boringssl/archive/16c8d3db1af20fcc04b5190b25242aadcb1fbb30.tar.gz boringssl-16c8d3d.tar.gz

# ============================================================================
# 需要系统预装的依赖
# ============================================================================

echo ""
echo "--- 需要系统预装的依赖 ---"
echo ""
echo "以下依赖需在离线环境中预装系统包:"
echo ""
echo "  1. OpenSSL (开发库)"
echo "  2. PostgreSQL (libpq 开发库)"
echo ""
echo "在 Ubuntu/Debian 离线环境中的安装方式:"
echo "  sudo apt-get install -y libssl-dev libpq-dev"
echo ""
echo "在 macOS 离线环境中的安装方式:"
echo "  brew install openssl postgresql@18"
echo ""
echo "注意: gRPC 及其子模块 (abseil-cpp, re2, boringssl, protobuf 等)"
echo "      已包含在 thirdparty/ 中，可从源码离线编译。"
echo ""

# ============================================================================
# 可选依赖（仅测试需要）
# ============================================================================

echo "--- 可选依赖（仅 BUILD_TESTS=ON 需要）---"
echo ""
echo "Catch2 v3.5.2 (如需构建测试，请下载):"
echo "  download https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz Catch2-v3.5.2.tar.gz"
echo ""

# ============================================================================
# 汇总
# ============================================================================

echo "=== 下载完成 ==="
echo ""
echo "依赖包列表:"
ls -lh "${THIRDPARTY_DIR}/"*.tar.gz 2>/dev/null
echo ""
echo "总大小:"
du -sh "${THIRDPARTY_DIR}/"
echo ""
echo "============================================"
echo "离线构建步骤:"
echo "============================================"
echo ""
echo "1. 将整个 backend/ 目录拷贝到离线环境"
echo ""
echo "2. 在离线环境中安装系统依赖 (OpenSSL, PostgreSQL):"
echo "   Ubuntu: sudo apt-get install -y libssl-dev libpq-dev"
echo "   macOS:  brew install openssl postgresql@18"
echo ""
echo "3. 构建项目 (gRPC 从源码编译):"
echo "   mkdir build && cd build"
echo "   cmake -DOFFLINE_BUILD=ON .."
echo "   make -j\$(nproc)"
echo ""
echo "4. (可选) 跳过测试构建以加快速度:"
echo "   cmake -DOFFLINE_BUILD=ON -DBUILD_TESTS=OFF .."
echo ""
echo "5. 如果离线环境已安装 gRPC/Protobuf 系统包，可跳过源码编译:"
echo "   cmake -DOFFLINE_BUILD=ON -DGRPC_FROM_SOURCE=OFF .."
