cmake_minimum_required(VERSION 3.20)
project(taskflow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# 第三方依赖 - 优先使用系统包
include(FetchContent)

# FetchContent 优化：断开远程更新检查，使用本地缓存
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Don't check for updates on FetchContent repos" FORCE)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps_cache" CACHE PATH "FetchContent cache directory" FORCE)

# spdlog - 优先系统包
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(
        spdlog
        URL ${CMAKE_CURRENT_SOURCE_DIR}/deps/spdlog-v1.14.1.tar.gz
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# nlohmann_json - 优先系统包
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL ${CMAKE_CURRENT_SOURCE_DIR}/deps/json-v3.11.3.tar.gz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# yaml-cpp - 优先系统包
find_package(yaml-cpp CONFIG QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(
        yaml_cpp
        URL ${CMAKE_CURRENT_SOURCE_DIR}/deps/yaml-cpp-0.8.0.tar.gz
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml_cpp)
endif()

# jwt-cpp - 优先系统包
find_package(jwt-cpp CONFIG QUIET)
if(NOT jwt-cpp_FOUND)
    FetchContent_Declare(
        jwt_cpp
        URL ${CMAKE_CURRENT_SOURCE_DIR}/deps/jwt-cpp-v0.7.0.tar.gz
    )
    FetchContent_MakeAvailable(jwt_cpp)
endif()

# c-ares - 优先系统包，回退 FetchContent
find_package(c-ares CONFIG QUIET)
if(NOT c-ares_FOUND)
    FetchContent_Declare(
        c_ares
        GIT_REPOSITORY https://github.com/c-ares/c-ares.git
        GIT_TAG v1.30.0
    )
    set(CARES_STATIC ON CACHE BOOL "" FORCE)
    set(CARES_SHARED OFF CACHE BOOL "" FORCE)
    set(CARES_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(c_ares)
    if(NOT TARGET c-ares::c-ares)
        add_library(c-ares::c-ares ALIAS c-ares)
    endif()
endif()

# gRPC + Protobuf - 优先系统包，回退 FetchContent
find_package(gRPC CONFIG QUIET)
find_package(Protobuf QUIET)
if(NOT gRPC_FOUND)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
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

    FetchContent_Declare(
        grpc
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG v1.65.5
    )
    FetchContent_MakeAvailable(grpc)

    if(NOT TARGET gRPC::grpc++)
        add_library(gRPC::grpc++ ALIAS grpc++)
    endif()
    if(NOT TARGET gRPC::grpc_cpp_plugin)
        add_executable(gRPC::grpc_cpp_plugin ALIAS grpc_cpp_plugin)
    endif()
endif()
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()

# Drogon - 优先系统包，回退 FetchContent
find_package(Drogon CONFIG QUIET)
if(NOT Drogon_FOUND)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(COZ_EXCLUDED ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        drogon
        URL ${CMAKE_CURRENT_SOURCE_DIR}/deps/drogon-v1.8.7.tar.gz
    )
    FetchContent_MakeAvailable(drogon)
    if(NOT TARGET Drogon::Drogon)
        add_library(Drogon::Drogon ALIAS drogon)
    endif()
endif()

# libpqxx - 优先系统包，回退 FetchContent
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX QUIET IMPORTED_TARGET libpqxx)
if(NOT PQXX_FOUND)
    find_package(Postgres QUIET)
    if(NOT Postgres_FOUND)
        set(Postgres_INCLUDE_DIR "/usr/local/include/postgresql")
        set(Postgres_LIBRARY_DIR "/usr/local/Cellar/postgresql@18/18.3/lib/postgresql")
        find_library(LIBPQ_LIB pq PATHS ${Postgres_LIBRARY_DIR} /usr/local/lib)
        find_path(LIBPQ_INCLUDE_DIR libpq-fe.h PATHS ${Postgres_INCLUDE_DIR} /usr/local/include)
    endif()

    FetchContent_Declare(
        libpqxx
        URL ${CMAKE_CURRENT_SOURCE_DIR}/deps/libpqxx-7.9.2.tar.gz
    )
    set(PQXX_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libpqxx)
    if(NOT TARGET PkgConfig::PQXX AND TARGET pqxx)
        add_library(PkgConfig::PQXX ALIAS pqxx)
    endif()
endif()

# OpenSSL
find_package(OpenSSL REQUIRED)

# Proto 生成
set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/proto")
set(PROTO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

set(PROTO_FILES "${PROTO_SRC_DIR}/taskflow.proto")

# 生成 Protobuf 文件
add_custom_command(
    OUTPUT "${PROTO_GENERATED_DIR}/taskflow.pb.cc" "${PROTO_GENERATED_DIR}/taskflow.pb.h"
    COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
    ARGS --proto_path=${PROTO_SRC_DIR}
         --cpp_out=${PROTO_GENERATED_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating Protobuf C++ sources"
)

# 生成 gRPC 文件
add_custom_command(
    OUTPUT "${PROTO_GENERATED_DIR}/taskflow.grpc.pb.cc" "${PROTO_GENERATED_DIR}/taskflow.grpc.pb.h"
    COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
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

# 子目录

# PostgreSQL library path (non-standard on macOS Homebrew)
if(APPLE)
    execute_process(
        COMMAND brew --prefix postgresql@18
        OUTPUT_VARIABLE PG_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(PG_PREFIX)
        link_directories(${PG_PREFIX}/lib/postgresql)
    endif()
endif()

add_subdirectory(src/common)
add_subdirectory(src/scheduler)
add_subdirectory(src/worker)

# 为项目目标添加 -Werror（不影响第三方库）
target_compile_options(taskflow_common PRIVATE -Werror -Wno-deprecated-declarations -Wno-unused-parameter -Wno-defaulted-function-deleted -Wno-unused-private-field)
target_compile_options(scheduler PRIVATE -Werror -Wno-deprecated-declarations -Wno-unused-parameter -Wno-defaulted-function-deleted -Wno-unused-private-field)
target_compile_options(worker PRIVATE -Werror -Wno-deprecated-declarations -Wno-unused-parameter -Wno-defaulted-function-deleted -Wno-unused-private-field)

# Testing
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)

    add_subdirectory(tests)
endif()
