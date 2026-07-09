#!/bin/bash
# TaskFlow 离线构建 - 第三方依赖源码下载脚本
# 在有网络的环境中运行此脚本，将所有依赖下载到 thirdparty/ 目录
# 然后将整个 backend/ 目录拷贝到离线环境即可构建
#
# 用法:
#   ./download-thirdparty.sh              # 下载全部依赖（含测试）
#   ./download-thirdparty.sh --no-test    # 跳过测试依赖 (Catch2)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
THIRDPARTY_DIR="${SCRIPT_DIR}/thirdparty"
mkdir -p "${THIRDPARTY_DIR}"

SKIP_TEST=false
if [ "${1:-}" = "--no-test" ]; then
    SKIP_TEST=true
fi

echo "=== TaskFlow 离线构建依赖下载 ==="
echo "下载目录: ${THIRDPARTY_DIR}"
echo ""

# 下载函数，支持重试和 SHA256 校验
download() {
    local url="$1"
    local output="$2"
    local expected_sha256="${3:-}"

    if [ -f "${THIRDPARTY_DIR}/${output}" ]; then
        # 文件已存在，校验 SHA256
        if [ -n "${expected_sha256}" ]; then
            local actual_sha256=$(sha256sum "${THIRDPARTY_DIR}/${output}" | awk '{print $1}')
            if [ "${actual_sha256}" = "${expected_sha256}" ]; then
                echo "  [SKIP] ${output} 已存在且校验通过"
                return 0
            else
                echo "  [SHA256] ${output} 校验失败，重新下载"
                rm -f "${THIRDPARTY_DIR}/${output}"
            fi
        else
            echo "  [SKIP] ${output} 已存在"
            return 0
        fi
    fi

    echo "  [DOWN] ${output} ..."
    curl -L --retry 3 --retry-delay 5 -o "${THIRDPARTY_DIR}/${output}" "${url}"
    if [ $? -ne 0 ]; then
        echo "  [FAIL] 下载失败: ${url}"
        rm -f "${THIRDPARTY_DIR}/${output}"
        return 1
    fi

    # 下载后校验 SHA256
    if [ -n "${expected_sha256}" ]; then
        local actual_sha256=$(sha256sum "${THIRDPARTY_DIR}/${output}" | awk '{print $1}')
        if [ "${actual_sha256}" != "${expected_sha256}" ]; then
            echo "  [FAIL] SHA256 校验失败: ${output}"
            echo "         期望: ${expected_sha256}"
            echo "         实际: ${actual_sha256}"
            rm -f "${THIRDPARTY_DIR}/${output}"
            return 1
        fi
        echo "  [OK]   SHA256 校验通过"
    fi
}

# ============================================================================
# 从源码构建的依赖（tarball 下载到 thirdparty/）
# ============================================================================

echo "--- 从源码构建的依赖 ---"

echo "[1/9] spdlog v1.14.1"
download https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz spdlog-v1.14.1.tar.gz \
    1586508029a7d0670dfcb2d97575dcdc242d3868a259742b69f100801ab4e16b

echo "[2/9] nlohmann_json v3.11.3"
download https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz json-v3.11.3.tar.gz \
    0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406

echo "[3/9] yaml-cpp 0.8.0"
download https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz yaml-cpp-0.8.0.tar.gz \
    fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16

echo "[4/9] jwt-cpp v0.7.0"
download https://github.com/Thalhammer/jwt-cpp/archive/refs/tags/v0.7.0.tar.gz jwt-cpp-v0.7.0.tar.gz \
    b9eb270e3ba8221e4b2bc38723c9a1cb4fa6c241a42908b9a334daff31137406

echo "[5/9] c-ares v1.30.0"
download https://github.com/c-ares/c-ares/archive/refs/tags/v1.30.0.tar.gz c-ares-v1.30.0.tar.gz \
    2f631573fb6ca91e1dfcafa8b43a24cf6fd6174b0c492220c6d2e72a45001ebe

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

echo "[10/17] gRPC v1.65.5"
download https://github.com/grpc/grpc/archive/refs/tags/v1.65.5.tar.gz grpc-v1.65.5.tar.gz

echo "[11/17] Protobuf v28.3 (gRPC 子模块)"
download https://github.com/protocolbuffers/protobuf/archive/refs/tags/v28.3.tar.gz protobuf-v28.3.tar.gz

echo "[12/17] abseil-cpp 20240116.0 (gRPC 子模块，gRPC v1.65.5 锁定版本)"
download https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.0.tar.gz abseil-cpp-20240116.0.tar.gz

echo "[13/17] re2 (gRPC 子模块，使用 gRPC v1.65.5 锁定的 commit)"
download https://github.com/google/re2/archive/0c5616df9c0aaa44c9440d87422012423d91c7d1.tar.gz re2-0c5616d.tar.gz

echo "[14/17] zlib (gRPC 子模块，使用 gRPC v1.65.5 锁定的 commit)"
download https://github.com/madler/zlib/archive/09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851.tar.gz zlib-09155ea.tar.gz

echo "[15/17] googletest (gRPC 子模块，使用 gRPC v1.65.5 锁定的 commit)"
download https://github.com/google/googletest/archive/2dd1c131950043a8ad5ab0d2dda0e0970596586a.tar.gz googletest-2dd1c13.tar.gz

echo "[16/17] boringssl (可选：仅 -DGRPC_USE_BORINGSSL=ON 时需要，默认用系统 OpenSSL)"
if [ "$DOWNLOAD_BORINGSSL" = true ]; then
    download https://github.com/google/boringssl/archive/16c8d3db1af20fcc04b5190b25242aadcb1fbb30.tar.gz boringssl-16c8d3d.tar.gz
else
    echo "  跳过（默认使用系统 OpenSSL，如需 BoringSSL 设 DOWNLOAD_BORINGSSL=true）"
fi

# ============================================================================
# 测试依赖
# ============================================================================

echo ""
if [ "$SKIP_TEST" = true ]; then
    echo "--- 跳过测试依赖 (使用 --no-test) ---"
else
    echo "--- 测试依赖 ---"
    echo ""
    echo "[17/17] Catch2 v3.5.2"
    download https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz Catch2-v3.5.2.tar.gz
fi

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
echo "在 CentOS/RHEL 离线环境中的安装方式:"
echo "  sudo yum install -y openssl-devel libpq-devel"
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
echo "   Ubuntu/Debian: sudo apt-get install -y libssl-dev libpq-dev"
echo "   CentOS/RHEL:   sudo yum install -y openssl-devel libpq-devel"
echo ""
echo "3. 构建项目 (gRPC 从源码编译):"
echo "   mkdir build && cd build"
echo "   cmake -DOFFLINE_BUILD=ON -DGRPC_FROM_SOURCE=ON .."
echo "   make -j\$(nproc)"
echo ""
echo "4. (可选) 跳过测试构建以加快速度:"
echo "   cmake -DOFFLINE_BUILD=ON -DGRPC_FROM_SOURCE=ON -DBUILD_TESTS=OFF .."
echo ""
echo "5. 如果离线环境已安装 gRPC/Protobuf 系统包，可跳过源码编译:"
echo "   cmake -DOFFLINE_BUILD=ON -DGRPC_FROM_SOURCE=OFF .."
