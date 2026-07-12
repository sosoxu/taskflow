add_library(taskflow_common STATIC
    config/scheduler_config.cpp
    config/worker_config.cpp
    database/database_manager.cpp
    util/crypto_util.cpp
    util/token_blacklist.cpp
)

target_include_directories(taskflow_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(taskflow_common PUBLIC
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    yaml-cpp
    OpenSSL::Crypto
    PkgConfig::PQXX
    jwt-cpp::jwt-cpp
)

# 预编译头加速编译
target_precompile_headers(taskflow_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)
