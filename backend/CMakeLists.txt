cmake_minimum_required(VERSION 3.20)

# 兼容旧版第三方库的 cmake_minimum_required 要求
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

project(taskflow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# 离线构建选项
# OFFLINE_BUILD=ON: 所有依赖从 thirdparty/ 本地 tarball 构建，不访问网络
# OFFLINE_BUILD=OFF (默认): 优先使用系统包，回退到 deps/ 本地 tarball 或 GIT 下载
# ============================================================================
option(OFFLINE_BUILD "Build with all dependencies from local thirdparty/ tarballs (no network)" OFF)
option(GRPC_FROM_SOURCE "Build gRPC and Protobuf from source tarballs (offline gRPC build)" OFF)

# ccache 加速重编译
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
endif()

# 编译选项（-Werror 仅用于项目源码，通过 target 设置，不影响 FetchContent 第三方库）
add_compile_options(-Wall -Wextra)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
else()
    add_compile_options(-O2)
endif()

# 第三方依赖
include(FetchContent)

# FetchContent 优化：断开远程更新检查，使用本地缓存
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Don't check for updates on FetchContent repos" FORCE)
if(OFFLINE_BUILD)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps_cache" CACHE PATH "FetchContent cache directory" FORCE)
    set(TP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
    message(STATUS "=== 离线构建模式 ===")
    message(STATUS "依赖源码目录: ${TP_DIR}")
else()
    set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps_cache" CACHE PATH "FetchContent cache directory" FORCE)
    set(TP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps")
endif()

# ============================================================================
# spdlog
# ============================================================================
if(OFFLINE_BUILD)
    FetchContent_Declare(spdlog URL ${TP_DIR}/spdlog-v1.14.1.tar.gz)
    FetchContent_MakeAvailable(spdlog)
else()
    find_package(spdlog CONFIG QUIET)
    if(NOT spdlog_FOUND)
        FetchContent_Declare(spdlog URL ${TP_DIR}/spdlog-v1.14.1.tar.gz)
        FetchContent_MakeAvailable(spdlog)
    endif()
endif()

# ============================================================================
# nlohmann_json
# ============================================================================
if(OFFLINE_BUILD)
    FetchContent_Declare(nlohmann_json URL ${TP_DIR}/json-v3.11.3.tar.gz)
    FetchContent_MakeAvailable(nlohmann_json)
else()
    find_package(nlohmann_json CONFIG QUIET)
    if(NOT nlohmann_json_FOUND)
        FetchContent_Declare(nlohmann_json URL ${TP_DIR}/json-v3.11.3.tar.gz)
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endif()

# ============================================================================
# yaml-cpp
# ============================================================================
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(OFFLINE_BUILD)
    FetchContent_Declare(yaml_cpp URL ${TP_DIR}/yaml-cpp-0.8.0.tar.gz)
    FetchContent_MakeAvailable(yaml_cpp)
else()
    find_package(yaml-cpp CONFIG QUIET)
    if(NOT yaml-cpp_FOUND)
        FetchContent_Declare(yaml_cpp URL ${TP_DIR}/yaml-cpp-0.8.0.tar.gz)
        FetchContent_MakeAvailable(yaml_cpp)
    endif()
endif()

# ============================================================================
# jwt-cpp
# ============================================================================
if(OFFLINE_BUILD)
    FetchContent_Declare(jwt_cpp URL ${TP_DIR}/jwt-cpp-v0.7.0.tar.gz)
    FetchContent_MakeAvailable(jwt_cpp)
else()
    find_package(jwt-cpp CONFIG QUIET)
    if(NOT jwt-cpp_FOUND)
        FetchContent_Declare(jwt_cpp URL ${TP_DIR}/jwt-cpp-v0.7.0.tar.gz)
        FetchContent_MakeAvailable(jwt_cpp)
    endif()
endif()

# ============================================================================
# c-ares
# GRPC_FROM_SOURCE=ON 时，c-ares 由 gRPC 的 module 模式构建
# 不在此处单独构建，避免与 gRPC 子模块构建冲突
# ============================================================================
set(CARES_STATIC ON CACHE BOOL "" FORCE)
set(CARES_SHARED OFF CACHE BOOL "" FORCE)
set(CARES_INSTALL OFF CACHE BOOL "" FORCE)
if(GRPC_FROM_SOURCE)
    # c-ares 将由 gRPC 的 module 模式自动构建
    # 只需在系统已安装时使用系统包
    find_package(c-ares CONFIG QUIET)
elseif(OFFLINE_BUILD)
    find_package(c-ares CONFIG QUIET)
    if(NOT c-ares_FOUND)
        FetchContent_Declare(c_ares URL ${TP_DIR}/c-ares-v1.30.0.tar.gz)
        FetchContent_MakeAvailable(c_ares)
        if(NOT TARGET c-ares::c-ares)
            add_library(c-ares::c-ares ALIAS c-ares)
        endif()
    endif()
else()
    find_package(c-ares CONFIG QUIET)
    if(NOT c-ares_FOUND)
        FetchContent_Declare(c_ares URL ${TP_DIR}/c-ares-v1.30.0.tar.gz)
        FetchContent_MakeAvailable(c_ares)
        if(NOT TARGET c-ares::c-ares)
            add_library(c-ares::c-ares ALIAS c-ares)
        endif()
    endif()
endif()

# ============================================================================
# Protobuf
# GRPC_FROM_SOURCE=ON 时，protobuf 和 abseil-cpp 由 gRPC 的 module 模式构建
# 不在此处单独构建，避免与 gRPC 子模块构建冲突
# ============================================================================
if(GRPC_FROM_SOURCE)
    # Protobuf 和 abseil-cpp 将由 gRPC 的 module 模式自动构建
    # 只需设置一些构建选项
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
else()
    if(OFFLINE_BUILD)
        find_package(Protobuf CONFIG REQUIRED)
        message(STATUS "离线构建: 使用系统 Protobuf ${Protobuf_VERSION}")
    else()
        find_package(Protobuf CONFIG QUIET)
        if(NOT Protobuf_FOUND)
            # 回退到 FindModule 模式
            find_package(Protobuf QUIET)
        endif()
        if(NOT Protobuf_FOUND)
            set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
            set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
            set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
            set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)

            FetchContent_Declare(protobuf URL ${TP_DIR}/protobuf-v28.3.tar.gz)
            FetchContent_MakeAvailable(protobuf)

            if(NOT TARGET protobuf::protoc)
                add_executable(protobuf::protoc ALIAS protoc)
            endif()
            set(Protobuf_FOUND ON CACHE BOOL "" FORCE)
            set(Protobuf_PROTOC_EXECUTABLE protobuf::protoc CACHE FILEPATH "" FORCE)
            set(PROTOBUF_PROTOC_EXECUTABLE protobuf::protoc CACHE FILEPATH "" FORCE)
        endif()
    endif()
endif()

# ============================================================================
# gRPC
# GRPC_FROM_SOURCE=ON: 从 thirdparty/ tarball 构建 gRPC
#   - 填充所有子模块到 gRPC 源码目录
#   - 使用 gRPC_*_PROVIDER=module 让 gRPC 从 third_party/ 子目录构建所有依赖
#   - abseil-cpp 和 protobuf 由 gRPC 的 module 模式自动构建
# GRPC_FROM_SOURCE=OFF: 使用系统 gRPC 包
# ============================================================================
if(GRPC_FROM_SOURCE)
    # 提取 gRPC 源码（不构建，先填充子模块）
    FetchContent_Declare(grpc URL ${TP_DIR}/grpc-v1.65.5.tar.gz)
    FetchContent_Populate(grpc)

    # 填充 abseil-cpp 子模块
    FetchContent_Declare(abseil_cpp URL ${TP_DIR}/abseil-cpp-20240116.0.tar.gz)
    FetchContent_Populate(abseil_cpp)
    if(EXISTS "${abseil_cpp_SOURCE_DIR}/CMakeLists.txt")
        file(COPY ${abseil_cpp_SOURCE_DIR}/ DESTINATION ${grpc_SOURCE_DIR}/third_party/abseil-cpp)
    endif()

    # 填充 protobuf 子模块
    FetchContent_Declare(protobuf_grpc URL ${TP_DIR}/protobuf-v28.3.tar.gz)
    FetchContent_Populate(protobuf_grpc)
    if(EXISTS "${protobuf_grpc_SOURCE_DIR}/CMakeLists.txt")
        file(COPY ${protobuf_grpc_SOURCE_DIR}/ DESTINATION ${grpc_SOURCE_DIR}/third_party/protobuf)
    endif()

    # 填充 re2 子模块
    FetchContent_Declare(re2_grpc URL ${TP_DIR}/re2-0c5616d.tar.gz)
    FetchContent_Populate(re2_grpc)
    if(EXISTS "${re2_grpc_SOURCE_DIR}/CMakeLists.txt")
        file(COPY ${re2_grpc_SOURCE_DIR}/ DESTINATION ${grpc_SOURCE_DIR}/third_party/re2)
    endif()

    # 填充 c-ares 子模块
    FetchContent_Declare(cares_grpc URL ${TP_DIR}/c-ares-v1.30.0.tar.gz)
    FetchContent_Populate(cares_grpc)
    if(EXISTS "${cares_grpc_SOURCE_DIR}/CMakeLists.txt")
        file(COPY ${cares_grpc_SOURCE_DIR}/ DESTINATION ${grpc_SOURCE_DIR}/third_party/cares/cares)
    endif()

    # 填充 zlib 子模块
    FetchContent_Declare(zlib_grpc_src URL ${TP_DIR}/zlib-09155ea.tar.gz)
    FetchContent_Populate(zlib_grpc_src)
    if(EXISTS "${zlib_grpc_src_SOURCE_DIR}/CMakeLists.txt")
        file(COPY ${zlib_grpc_src_SOURCE_DIR}/ DESTINATION ${grpc_SOURCE_DIR}/third_party/zlib)
    endif()

    # 填充 boringssl 子模块
    FetchContent_Declare(boringssl URL ${TP_DIR}/boringssl-16c8d3d.tar.gz)
    FetchContent_Populate(boringssl)
    if(EXISTS "${boringssl_SOURCE_DIR}/CMakeLists.txt")
        file(COPY ${boringssl_SOURCE_DIR}/ DESTINATION ${grpc_SOURCE_DIR}/third_party/boringssl-with-bazel)
    endif()

    # 填充 googletest 子模块
    FetchContent_Declare(googletest_grpc URL ${TP_DIR}/googletest-2dd1c13.tar.gz)
    FetchContent_Populate(googletest_grpc)
    if(EXISTS "${googletest_grpc_SOURCE_DIR}/CMakeLists.txt")
        file(COPY ${googletest_grpc_SOURCE_DIR}/ DESTINATION ${grpc_SOURCE_DIR}/third_party/googletest)
    endif()

    # 创建空目录占位（gRPC CMake 可能检查这些目录是否存在）
    file(MAKE_DIRECTORY ${grpc_SOURCE_DIR}/third_party/envoy-api)
    file(MAKE_DIRECTORY ${grpc_SOURCE_DIR}/third_party/opencensus-proto)
    file(MAKE_DIRECTORY ${grpc_SOURCE_DIR}/third_party/xds)
    file(MAKE_DIRECTORY ${grpc_SOURCE_DIR}/third_party/opentelemetry)
    file(MAKE_DIRECTORY ${grpc_SOURCE_DIR}/third_party/googleapis)
    file(MAKE_DIRECTORY ${grpc_SOURCE_DIR}/third_party/protoc-gen-validate)

    # gRPC 构建选项
    set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)

    # 所有依赖使用 module 模式（从 third_party/ 子目录构建）
    set(gRPC_ABSL_PROVIDER "module" CACHE STRING "" FORCE)
    set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "" FORCE)
    set(gRPC_RE2_PROVIDER "module" CACHE STRING "" FORCE)
    set(gRPC_CARES_PROVIDER "module" CACHE STRING "" FORCE)
    set(gRPC_SSL_PROVIDER "module" CACHE STRING "" FORCE)
    set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "" FORCE)
    set(gRPC_BENCHMARK_PROVIDER "none" CACHE STRING "" FORCE)

    # 构建 gRPC（包含所有子模块），EXCLUDE_FROM_ALL 防止第三方库被安装
    add_subdirectory(${grpc_SOURCE_DIR} ${grpc_BINARY_DIR} EXCLUDE_FROM_ALL)

    if(NOT TARGET gRPC::grpc++)
        add_library(gRPC::grpc++ ALIAS grpc++)
    endif()
    if(NOT TARGET gRPC::grpc_cpp_plugin)
        add_executable(gRPC::grpc_cpp_plugin ALIAS grpc_cpp_plugin)
    endif()

    # 设置 Protobuf 变量供后续 proto 生成使用
    if(NOT TARGET protobuf::protoc)
        add_executable(protobuf::protoc ALIAS protoc)
    endif()
    set(Protobuf_FOUND ON)
    set(PROTOBUF_PROTOC_EXECUTABLE protobuf::protoc CACHE FILEPATH "" FORCE)
    message(STATUS "gRPC + Protobuf 从源码构建完成")
else()
    if(OFFLINE_BUILD)
        find_package(gRPC CONFIG REQUIRED)
        message(STATUS "离线构建: 使用系统 gRPC")
    else()
        find_package(gRPC CONFIG QUIET)
        if(NOT gRPC_FOUND)
            set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
            set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
            set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
            set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
            set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
            set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
            set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
            set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)

            if(Protobuf_FOUND)
                set(gRPC_PROTOBUF_PROVIDER "package" CACHE STRING "" FORCE)
            else()
                set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "" FORCE)
            endif()
            if(c-ares_FOUND)
                set(gRPC_CA_PROVIDER "package" CACHE STRING "" FORCE)
            else()
                set(gRPC_CA_PROVIDER "module" CACHE STRING "" FORCE)
            endif()

            FetchContent_Declare(grpc URL ${TP_DIR}/grpc-v1.65.5.tar.gz)
            FetchContent_MakeAvailable(grpc)

            if(NOT TARGET gRPC::grpc++)
                add_library(gRPC::grpc++ ALIAS grpc++)
            endif()
            if(NOT TARGET gRPC::grpc_cpp_plugin)
                add_executable(gRPC::grpc_cpp_plugin ALIAS grpc_cpp_plugin)
            endif()
        endif()
    endif()
endif()

# ============================================================================
# Drogon (含 trantor 和 jsoncpp)
# Drogon tarball 中 trantor 子目录为空 (git submodule)，
# 需要先 FetchContent trantor 和 jsoncpp，再填充到 Drogon 源码目录
# ============================================================================
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(COZ_EXCLUDED ON CACHE BOOL "" FORCE)

# 先声明 trantor 和 jsoncpp（不立即构建）
FetchContent_Declare(trantor URL ${TP_DIR}/trantor-v1.5.20.tar.gz)
FetchContent_Declare(jsoncpp URL ${TP_DIR}/jsoncpp-1.9.5.tar.gz)

if(OFFLINE_BUILD)
    FetchContent_Declare(drogon URL ${TP_DIR}/drogon-v1.8.7.tar.gz)
else()
    find_package(Drogon CONFIG QUIET)
    if(Drogon_FOUND)
        message(STATUS "Found system Drogon")
    else()
        FetchContent_Declare(drogon URL ${TP_DIR}/drogon-v1.8.7.tar.gz)
    endif()
endif()

# 构建前：提取 trantor/jsoncpp 源码并填充到 Drogon 的子目录
if(NOT Drogon_FOUND)
    # 先构建 jsoncpp（Drogon 依赖它）
    set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(jsoncpp)

    # jsoncpp_static 的 INTERFACE_INCLUDE_DIRECTORIES 包含构建时生成的目录，
    # 在配置阶段该目录不存在会导致 CMake 报错。预先创建它。
    file(MAKE_DIRECTORY "${jsoncpp_BINARY_DIR}/include/json")

    # 提取 trantor 源码（不构建，将复制到 Drogon 子目录）
    FetchContent_Populate(trantor)

    # 提取 Drogon 源码（不构建）
    FetchContent_Populate(drogon)

    # 填充 trantor 子目录
    file(COPY ${trantor_SOURCE_DIR}/ DESTINATION ${drogon_SOURCE_DIR}/trantor)

    # 修补 Drogon 的 FindJsoncpp：使用 FetchContent 的 jsoncpp
    # 直接使用源码目录的 include 路径，避免 jsoncpp 构建目录中的生成路径问题
    file(WRITE ${drogon_SOURCE_DIR}/cmake_modules/FindJsoncpp.cmake "
# Patched FindJsoncpp - use FetchContent jsoncpp
set(JSONCPP_INCLUDE_DIRS \"${jsoncpp_SOURCE_DIR}/include\" CACHE PATH \"\" FORCE)
if(TARGET jsoncpp_static)
  set(JSONCPP_LIBRARIES jsoncpp_static CACHE STRING \"\" FORCE)
elseif(TARGET jsoncpp_lib)
  set(JSONCPP_LIBRARIES jsoncpp_lib CACHE STRING \"\" FORCE)
else()
  find_library(JSONCPP_LIBRARIES NAMES jsoncpp jsoncpp_static)
endif()
set(Jsoncpp_FOUND TRUE CACHE BOOL \"\" FORCE)
if(NOT TARGET Jsoncpp_lib)
  add_library(Jsoncpp_lib INTERFACE IMPORTED)
endif()
set_target_properties(Jsoncpp_lib PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES \"${jsoncpp_SOURCE_DIR}/include\"
  INTERFACE_LINK_LIBRARIES \"\${JSONCPP_LIBRARIES}\")
")

    # 修补 trantor 的 CMakeLists.txt：c-ares 链接兼容性
    # 原始代码使用 c-ares_lib (Findc-ares 模块定义)，但系统 c-ares 可能用不同目标名
    file(READ ${drogon_SOURCE_DIR}/trantor/CMakeLists.txt TRANTOR_CMAKE)
    string(REPLACE
        "target_link_libraries(\${PROJECT_NAME} PRIVATE c-ares_lib)"
        "if(TARGET c-ares_lib)\n    target_link_libraries(\${PROJECT_NAME} PRIVATE c-ares_lib)\n  elseif(TARGET c-ares::c-ares)\n    target_link_libraries(\${PROJECT_NAME} PRIVATE c-ares::c-ares)\n  elseif(TARGET c-ares::cares)\n    target_link_libraries(\${PROJECT_NAME} PRIVATE c-ares::cares)\n  endif()"
        TRANTOR_CMAKE "${TRANTOR_CMAKE}")
    file(WRITE ${drogon_SOURCE_DIR}/trantor/CMakeLists.txt "${TRANTOR_CMAKE}")

    # 现在构建 Drogon（包含已填充的 trantor），EXCLUDE_FROM_ALL 防止第三方库被安装
    add_subdirectory(${drogon_SOURCE_DIR} ${drogon_BINARY_DIR} EXCLUDE_FROM_ALL)
    if(NOT TARGET Drogon::Drogon)
        add_library(Drogon::Drogon ALIAS drogon)
    endif()
endif()

# ============================================================================
# libpqxx
# ============================================================================
find_package(PkgConfig REQUIRED)

# 设置 PostgreSQL 路径（macOS Homebrew 非标准路径）
if(APPLE AND NOT OFFLINE_BUILD)
    execute_process(
        COMMAND brew --prefix postgresql@18
        OUTPUT_VARIABLE PG_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(PG_PREFIX)
    set(PostgreSQL_INCLUDE_DIR "${PG_PREFIX}/include" CACHE PATH "" FORCE)
    set(PostgreSQL_LIBRARY_DIR "${PG_PREFIX}/lib/postgresql" CACHE PATH "" FORCE)
    # CMake FindPostgreSQL 需要 PostgreSQL_LIBRARY 指向 libpq
    find_library(PostgreSQL_LIBRARY NAMES pq PATHS ${PG_PREFIX}/lib/postgresql ${PG_PREFIX}/lib NO_DEFAULT_PATH)
    if(PostgreSQL_LIBRARY)
        set(PostgreSQL_LIBRARY "${PostgreSQL_LIBRARY}" CACHE FILEPATH "" FORCE)
    endif()
    set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${PG_PREFIX}/lib/pkgconfig")
    # libpqxx 需要找到 libpq-fe.h（在 include/postgresql/ 子目录下）
    set(ENV{CPLUS_INCLUDE_PATH} "$ENV{CPLUS_INCLUDE_PATH}:${PG_PREFIX}/include:${PG_PREFIX}/include/postgresql")
    include_directories(${PG_PREFIX}/include ${PG_PREFIX}/include/postgresql)
elseif(APPLE)
    # 离线模式或 brew 不可用时，尝试常见路径
    set(PostgreSQL_INCLUDE_DIR "/usr/local/include/postgresql" CACHE PATH "" FORCE)
    set(PostgreSQL_LIBRARY_DIR "/usr/local/Cellar/postgresql@18/18.3/lib/postgresql" CACHE PATH "" FORCE)
    find_library(PostgreSQL_LIBRARY NAMES pq PATHS /usr/local/Cellar/postgresql@18/18.3/lib/postgresql /usr/local/lib NO_DEFAULT_PATH)
    if(PostgreSQL_LIBRARY)
        set(PostgreSQL_LIBRARY "${PostgreSQL_LIBRARY}" CACHE FILEPATH "" FORCE)
    endif()
    # libpqxx 需要找到 libpq-fe.h
    include_directories(/usr/local/include/postgresql)
endif()

if(OFFLINE_BUILD)
    # 离线模式：查找 PostgreSQL 头文件和库，然后从源码构建 libpqxx
    # 离线环境需要安装 PostgreSQL 开发库 (libpq)
    find_package(Postgres QUIET)
    if(NOT Postgres_FOUND)
        # 尝试常见路径
        find_library(LIBPQ_LIB pq PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu ${PostgreSQL_LIBRARY_DIR})
        find_path(LIBPQ_INCLUDE_DIR libpq-fe.h PATHS /usr/local/include /usr/include /usr/include/postgresql ${PostgreSQL_INCLUDE_DIR})
        if(LIBPQ_LIB AND LIBPQ_INCLUDE_DIR)
            set(Postgres_FOUND ON)
        endif()
    endif()

    FetchContent_Declare(libpqxx URL ${TP_DIR}/libpqxx-7.9.2.tar.gz)
    set(PQXX_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Populate(libpqxx)

    # 修补 libpqxx 的 config.cmake：在 find_package(PostgreSQL) 之前添加包含路径
    if(PG_PREFIX)
        file(READ ${libpqxx_SOURCE_DIR}/cmake/config.cmake LIBPQXX_CONFIG)
        string(CONCAT LIBPQXX_CONFIG
            "# Patched: add PostgreSQL include path\n"
            "include_directories(\"${PG_PREFIX}/include/postgresql\")\n"
            "link_directories(\"${PG_PREFIX}/lib/postgresql\")\n"
            "set(PostgreSQL_INCLUDE_DIR \"${PG_PREFIX}/include\")\n"
            "set(PostgreSQL_LIBRARY \"${PG_PREFIX}/lib/postgresql/libpq.dylib\")\n"
            "set(PostgreSQL_FOUND TRUE)\n\n"
            "${LIBPQXX_CONFIG}")
        file(WRITE ${libpqxx_SOURCE_DIR}/cmake/config.cmake "${LIBPQXX_CONFIG}")
    endif()

    add_subdirectory(${libpqxx_SOURCE_DIR} ${libpqxx_BINARY_DIR} EXCLUDE_FROM_ALL)
    if(NOT TARGET PkgConfig::PQXX AND TARGET pqxx)
        add_library(PkgConfig::PQXX ALIAS pqxx)
    endif()
else()
    pkg_check_modules(PQXX QUIET IMPORTED_TARGET libpqxx)
    if(NOT PQXX_FOUND)
        find_package(Postgres QUIET)
        if(NOT Postgres_FOUND)
            find_library(LIBPQ_LIB pq PATHS ${PostgreSQL_LIBRARY_DIR} /usr/local/lib)
            find_path(LIBPQ_INCLUDE_DIR libpq-fe.h PATHS ${PostgreSQL_INCLUDE_DIR} /usr/local/include)
        endif()

        FetchContent_Declare(libpqxx URL ${TP_DIR}/libpqxx-7.9.2.tar.gz)
        set(PQXX_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(libpqxx)
        if(NOT TARGET PkgConfig::PQXX AND TARGET pqxx)
            add_library(PkgConfig::PQXX ALIAS pqxx)
        endif()
    endif()
endif()

# ============================================================================
# OpenSSL (系统库，离线环境需预装)
# ============================================================================
find_package(OpenSSL REQUIRED)

# ============================================================================
# Proto 生成
# ============================================================================
set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/proto")
set(PROTO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

set(PROTO_FILES "${PROTO_SRC_DIR}/taskflow.proto")

# 确定 protoc 可执行文件路径
if(TARGET protobuf::protoc)
    set(PROTOC_EXECUTABLE protobuf::protoc)
elseif(DEFINED PROTOBUF_PROTOC_EXECUTABLE)
    set(PROTOC_EXECUTABLE ${PROTOBUF_PROTOC_EXECUTABLE})
else()
    find_program(PROTOC_EXECUTABLE protoc REQUIRED)
endif()

# 生成 Protobuf 文件
add_custom_command(
    OUTPUT "${PROTO_GENERATED_DIR}/taskflow.pb.cc" "${PROTO_GENERATED_DIR}/taskflow.pb.h"
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --proto_path=${PROTO_SRC_DIR}
         --cpp_out=${PROTO_GENERATED_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating Protobuf C++ sources"
)

# 生成 gRPC 文件
add_custom_command(
    OUTPUT "${PROTO_GENERATED_DIR}/taskflow.grpc.pb.cc" "${PROTO_GENERATED_DIR}/taskflow.grpc.pb.h"
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --proto_path=${PROTO_SRC_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         --grpc_out=${PROTO_GENERATED_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating gRPC C++ sources"
)

# Proto 库
add_library(taskflow_proto STATIC
    "${PROTO_GENERATED_DIR}/taskflow.pb.cc"
    "${PROTO_GENERATED_DIR}/taskflow.grpc.pb.cc"
)
target_include_directories(taskflow_proto PUBLIC ${PROTO_GENERATED_DIR})
target_link_libraries(taskflow_proto PUBLIC
    ${PROTOBUF_LIBRARIES}
    gRPC::grpc++
)

# ============================================================================
# 子目录
# ============================================================================

# PostgreSQL library path (non-standard on macOS Homebrew)
if(APPLE AND PG_PREFIX)
    link_directories(${PG_PREFIX}/lib/postgresql)
endif()

add_subdirectory(src/common)
add_subdirectory(src/scheduler)
add_subdirectory(src/worker)

# 为项目目标添加 -Werror（不影响第三方库）
target_compile_options(taskflow_common PRIVATE -Werror -Wno-deprecated-declarations -Wno-unused-parameter -Wno-defaulted-function-deleted -Wno-unused-private-field)
target_compile_options(scheduler PRIVATE -Werror -Wno-deprecated-declarations -Wno-unused-parameter -Wno-defaulted-function-deleted -Wno-unused-private-field)
target_compile_options(worker PRIVATE -Werror -Wno-deprecated-declarations -Wno-unused-parameter -Wno-defaulted-function-deleted -Wno-unused-private-field)

# ============================================================================
# Testing
# ============================================================================
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    if(OFFLINE_BUILD)
        FetchContent_Declare(Catch2 URL ${TP_DIR}/Catch2-v3.5.2.tar.gz)
    else()
        FetchContent_Declare(Catch2 URL ${TP_DIR}/Catch2-v3.5.2.tar.gz)
    endif()
    FetchContent_MakeAvailable(Catch2)

    add_subdirectory(tests)
endif()

# ============================================================================
# 安装配置
# 安装目录结构:
#   ${CMAKE_INSTALL_PREFIX}/
#   ├── bin/scheduler
#   ├── bin/worker
#   ├── etc/scheduler.yaml.example
#   ├── etc/worker.yaml.example
#   └── sql/schema.sql
#       sql/migrations/
#
# 使用方式:
#   cmake --install . --prefix /opt/taskflow
#   cmake --install . --prefix /usr/local
#
# 注意: 由于第三方库 (gRPC, Drogon 等) 使用 add_subdirectory 构建，
#       cmake --install 会尝试安装它们的文件。如需只安装项目文件:
#   cmake --install . --prefix /opt/taskflow --component taskflow
# ============================================================================

# 安装可执行文件（指定 COMPONENT 以便选择性安装）
install(TARGETS scheduler worker
    RUNTIME DESTINATION bin
    COMPONENT taskflow
)

# 安装配置文件模板
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler.yaml.example
    ${CMAKE_CURRENT_SOURCE_DIR}/worker.yaml.example
    DESTINATION etc
    COMPONENT taskflow
)

# 安装 SQL 初始化脚本
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/sql/
    DESTINATION sql
    COMPONENT taskflow
    FILES_MATCHING PATTERN "*.sql"
)
