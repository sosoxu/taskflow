#!/bin/bash
# TaskFlow 依赖库本地编译安装脚本
# 用于在没有系统包的沙箱/裸机环境中快速安装所有 C++ 依赖
#
# 用法: ./build-deps.sh [install_prefix]
#   install_prefix: 安装路径，默认 /usr/local
#
# 安装完成后:
#   export LD_LIBRARY_PATH=<prefix>/lib:$LD_LIBRARY_PATH
#   cd /workspace/taskflow/backend && make scheduler

set -e

PREFIX="${1:-/usr/local}"
NPROC=$(nproc)
TMPDIR=$(mktemp -d /tmp/taskflow-deps-XXXXXX)

echo "=== TaskFlow 依赖编译 ==="
echo "安装路径: $PREFIX"
echo "并行数: $NPROC"
echo "临时目录: $TMPDIR"
echo ""

# 安装编译依赖
echo "[1/5] 安装编译工具..."
sudo apt-get update -qq && sudo apt-get install -y -qq --no-install-recommends \
    build-essential cmake git pkg-config \
    libssl-dev libjsoncpp-dev libc-ares-dev libpq-dev libuuid1-dev zlib1g-dev \
    libspdlog-dev nlohmann-json3-dev libyaml-cpp-dev libpqxx-dev \
    > /dev/null

# protobuf
echo "[2/5] 编译 protobuf..."
if pkg-config --exists protobuf 2>/dev/null; then
    echo "  已安装，跳过"
else
    git clone --depth 1 --branch v28.3 https://github.com/protocolbuffers/protobuf.git "$TMPDIR/protobuf"
    cd "$TMPDIR/protobuf" && mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
             -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=ON
    cmake --build . -j"$NPROC" && sudo cmake --install .
    sudo ldconfig
    echo "  protobuf 安装完成"
fi

# gRPC
echo "[3/5] 编译 gRPC..."
if pkg-config --exists grpc++ 2>/dev/null; then
    echo "  已安装，跳过"
else
    git clone --depth 1 --branch v1.65.5 https://github.com/grpc/grpc.git "$TMPDIR/grpc"
    cd "$TMPDIR/grpc" && git submodule update --init --recursive --depth 1
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
             -DgRPC_BUILD_TESTS=OFF \
             -DgRPC_BUILD_CSHARP_EXT=OFF \
             -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF \
             -DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF \
             -DgRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF \
             -DgRPC_BUILD_GRPC_PHP_PLUGIN=OFF \
             -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF \
             -DgRPC_BUILD_GRPC_RUBY_PLUGIN=OFF \
             -DgRPC_PROTOBUF_PROVIDER=package \
             -DgRPC_CA_PROVIDER=package \
             -DgRPC_SSL_PROVIDER=package
    cmake --build . -j"$NPROC" && sudo cmake --install .
    sudo ldconfig
    echo "  gRPC 安装完成"
fi

# Drogon
echo "[4/5] 编译 Drogon..."
if pkg-config --exists drogon 2>/dev/null; then
    echo "  已安装，跳过"
else
    git clone --depth 1 --branch v1.8.7 https://github.com/drogonframework/drogon.git "$TMPDIR/drogon"
    cd "$TMPDIR/drogon" && git submodule update --init --recursive --depth 1
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
             -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DCOZ_EXCLUDED=ON
    cmake --build . -j"$NPROC" && sudo cmake --install .
    sudo ldconfig
    echo "  Drogon 安装完成"
fi

# jwt-cpp
echo "[5/5] 编译 jwt-cpp..."
if [ -f "$PREFIX/include/jwt-cpp/jwt.h" ]; then
    echo "  已安装，跳过"
else
    git clone --depth 1 --branch v0.7.0 https://github.com/Thalhammer/jwt-cpp.git "$TMPDIR/jwt-cpp"
    cd "$TMPDIR/jwt-cpp" && mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
             -DJWT_BUILD_EXAMPLES=OFF -DJWT_BUILD_TESTS=OFF
    cmake --build . -j"$NPROC" && sudo cmake --install .
    echo "  jwt-cpp 安装完成"
fi

# 清理
rm -rf "$TMPDIR"

echo ""
echo "=== 所有依赖安装完成 ==="
echo ""
echo "请确保环境变量已设置:"
echo "  export LD_LIBRARY_PATH=$PREFIX/lib:\$LD_LIBRARY_PATH"
echo ""
echo "然后构建项目:"
echo "  cd /workspace/taskflow/backend && make scheduler"
