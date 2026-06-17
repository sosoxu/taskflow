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

# 编译选项
add_compile_options(-Wall -Wextra -Werror)
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
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# nlohmann_json - 优先系统包
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# yaml-cpp - 优先系统包
find_package(yaml-cpp CONFIG QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(
        yaml_cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml_cpp)
endif()

# jwt-cpp - 优先系统包
find_package(jwt-cpp CONFIG QUIET)
if(NOT jwt-cpp_FOUND)
    FetchContent_Declare(
        jwt_cpp
        GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
        GIT_TAG v0.7.0
    )
    FetchContent_MakeAvailable(jwt_cpp)
endif()

# Drogon
find_package(Drogon CONFIG REQUIRED)

# gRPC + Protobuf - find gRPC first (it pulls in Protobuf as a dependency)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)
find_package(c-ares CONFIG REQUIRED)

# libpqxx - 使用 pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)

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
add_subdirectory(src/common)
add_subdirectory(src/scheduler)
add_subdirectory(src/worker)

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
